#include "util.hpp"

#include <cerrno>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

namespace appdeploy::util {

std::string json_escape(std::string_view text)
{
    std::string out;
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else out += char(c);
        }
    }
    return out;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;) {
        const auto end = text.find(separator, begin);
        parts.emplace_back(text.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos) return parts;
        begin = end + 1;
    }
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string strict_absolute(std::string_view path)
{
    if (path.empty() || path[0] != '/' || path.size() >= 4096) return {};
    if (path == "/") return "/";
    if (path.back() == '/') return {};
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7f) return {};
    const auto parts = split(path.substr(1), '/');
    for (const auto &part : parts)
        if (part.empty() || part == "." || part == ".." || part.size() > 255) return {};
    return std::string(path);
}

bool within(std::string_view path, std::string_view directory)
{
    if (directory == "/") return true;
    return path == directory || (starts_with(path, directory) && path.size() > directory.size()
                                 && path[directory.size()] == '/');
}

std::string resolve_link(std::string_view link_path, std::string_view target)
{
    if (target.empty() || target[0] == '/' || target.find('\0') != std::string_view::npos) return {};
    std::vector<std::string> parts;
    const auto base = split(std::string(link_path).substr(1), '/');
    for (std::size_t i = 0; i + 1 < base.size(); ++i) parts.push_back(base[i]);
    for (const auto &part : split(target, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (parts.empty()) return {};
            parts.pop_back();
        } else parts.push_back(part);
    }
    std::string out;
    for (const auto &part : parts) out += "/" + part;
    return out.empty() ? "/" : out;
}

int open_directory_nofollow(const std::string &path, int flags)
{
    std::vector<std::string> parts;
    for (auto &part : split(path, '/'))
        if (!part.empty()) parts.push_back(std::move(part));
    if (parts.empty()) return ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | flags);
    int directory = ::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        const int next = ::openat(directory, parts[i].c_str(),
                                  (last ? O_RDONLY | flags : O_PATH) | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int saved = errno;
        ::close(directory);
        if (next < 0) { errno = saved; return -1; }
        directory = next;
    }
    return directory;
}

std::string read_regular_file(int directory_fd, const char *name, std::size_t limit, std::uint32_t owner, bool check_owner)
{
    const int fd = ::openat(directory_fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return {};
    struct stat st;
    std::string data;
    if (!::fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size >= 0
        && std::size_t(st.st_size) <= limit && (!check_owner || (st.st_uid == owner && !(st.st_mode & 022)))) {
        data.resize(std::size_t(st.st_size));
        std::size_t done = 0;
        while (done < data.size()) {
            const auto count = ::read(fd, data.data() + done, data.size() - done);
            if (count <= 0) { data.clear(); break; }
            done += std::size_t(count);
        }
    }
    ::close(fd);
    return data;
}

void write_file_atomic(int directory_fd, const char *name, const std::string &data, unsigned mode)
{
    const std::string temporary = std::string(".") + name + ".tmp";
    ::unlinkat(directory_fd, temporary.c_str(), 0);
    const int fd = ::openat(directory_fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) throw std::runtime_error("cannot create " + temporary + ": " + errno_text(errno));
    std::size_t done = 0;
    bool ok = true;
    while (done < data.size()) {
        const auto count = ::write(fd, data.data() + done, data.size() - done);
        if (count <= 0) { ok = false; break; }
        done += std::size_t(count);
    }
    ok = ok && !::fchmod(fd, mode) && !::fsync(fd);
    ::close(fd);
    if (!ok || ::renameat(directory_fd, temporary.c_str(), directory_fd, name)) {
        const int saved = errno;
        ::unlinkat(directory_fd, temporary.c_str(), 0);
        throw std::runtime_error(std::string("cannot write ") + name + ": " + errno_text(saved));
    }
    ::fsync(directory_fd);
}

void remove_tree(int parent_fd, const char *name)
{
    struct stat st;
    if (::fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW)) return;
    if (S_ISDIR(st.st_mode)) {
        const int fd = ::openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) return;
        struct stat inside;
        if (::fstat(fd, &inside) || inside.st_dev != st.st_dev || inside.st_ino != st.st_ino) { ::close(fd); return; }
        DIR *stream = ::fdopendir(fd);
        if (!stream) { ::close(fd); return; }
        std::vector<std::string> names;
        while (dirent *entry = ::readdir(stream)) {
            if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) names.emplace_back(entry->d_name);
        }
        for (const auto &child : names) {
            struct stat info;
            if (!::fstatat(dirfd(stream), child.c_str(), &info, AT_SYMLINK_NOFOLLOW) && info.st_dev == st.st_dev)
                remove_tree(dirfd(stream), child.c_str());
        }
        ::closedir(stream);
        ::unlinkat(parent_fd, name, AT_REMOVEDIR);
    } else ::unlinkat(parent_fd, name, 0);
}

std::uint64_t now_seconds() { return std::uint64_t(::time(nullptr)); }

std::string errno_text(int error) { return std::strerror(error); }

}
