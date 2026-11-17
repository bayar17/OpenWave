#include "appdeploy/appdeploy.hpp"
#include "util.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <selinux/label.h>
#include <selinux/selinux.h>

namespace appdeploy {

std::string Registry::bundle_dir(const std::string &id) const { return config_.path(config_.applications + "/" + id); }
std::string Registry::manifest_path(const std::string &id) const { return bundle_dir(id) + "/manifest.json"; }
std::string Registry::toml_path(const std::string &id) const { return bundle_dir(id) + "/Application.toml"; }

namespace {
bool sound_file(int bundle_fd, const std::string &relative, bool executable, std::uint32_t owner)
{
    int directory = ::dup(bundle_fd);
    const auto parts = util::split(relative, '/');
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const int next = ::openat(directory, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(directory);
        if (next < 0) return false;
        struct stat st;
        if (::fstat(next, &st) || st.st_uid != owner || (st.st_mode & 022)) { ::close(next); return false; }
        directory = next;
    }
    struct stat st;
    const bool ok = !::fstatat(directory, parts.back().c_str(), &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode)
        && st.st_uid == owner && !(st.st_mode & 022) && st.st_nlink == 1 && (!executable || (st.st_mode & 0100));
    ::close(directory);
    return ok;
}

struct Bundle {
    int fd = -1;
    ~Bundle() { if (fd >= 0) ::close(fd); }
};

ApplicationDescriptor read_descriptor(const Config &config, int bundle_fd, const std::string &id,
                                      const std::string &expected_version)
{
    const std::string text = util::read_regular_file(bundle_fd, "Application.toml", 64 * 1024, config.owner_uid);
    if (text.empty()) throw Error("Application.toml of " + id + " is missing or unsafe");
    ApplicationDescriptor descriptor = ApplicationDescriptor::parse(text, id);
    if (!expected_version.empty() && descriptor.version != expected_version)
        throw Error("Application.toml of " + id + " declares version " + descriptor.version + ", not " + expected_version);
    return descriptor;
}

int open_bundle(const Registry &registry, const Config &config, const std::string &id)
{
    const int fd = util::open_directory_nofollow(registry.bundle_dir(id));
    if (fd < 0) throw Error("the directory of " + id + " is missing or a link");
    struct stat st;
    if (::fstat(fd, &st) || st.st_uid != config.owner_uid || (st.st_mode & 022)) {
        ::close(fd);
        throw Error("the directory of " + id + " is not root-owned or is writable by others");
    }
    return fd;
}
}

bool Registry::register_bundle(const std::string &id, const std::string &expected_version)
{
    Bundle bundle;
    bundle.fd = open_bundle(*this, config_, id);
    const ApplicationDescriptor descriptor = read_descriptor(config_, bundle.fd, id, expected_version);
    if (!descriptor.launchable()) {
        ::unlinkat(bundle.fd, "manifest.json", 0);
        ::unlinkat(bundle.fd, ".manifest.appdeploy", 0);
        return false;
    }
    if (!sound_file(bundle.fd, descriptor.executable, true, config_.owner_uid))
        throw Error("the executable " + descriptor.executable + " of " + id + " is missing or unsafe");
    if (!descriptor.icon.empty() && !sound_file(bundle.fd, descriptor.icon, false, config_.owner_uid))
        throw Error("the icon " + descriptor.icon + " of " + id + " is missing or unsafe");
    util::write_file_atomic(bundle.fd, "manifest.json", descriptor.manifest_json(), 0644);
    util::write_file_atomic(bundle.fd, ".manifest.appdeploy", "appdeploy\n", 0644);
    return true;
}

void Registry::unregister_bundle(const std::string &id)
{
    const int fd = util::open_directory_nofollow(bundle_dir(id));
    if (fd < 0) return;
    ::unlinkat(fd, "manifest.json", 0);
    ::unlinkat(fd, ".manifest.json.tmp", 0);
    ::unlinkat(fd, ".manifest.appdeploy", 0);
    ::unlinkat(fd, ".manifest.appdeploy.tmp", 0);
    ::fsync(fd);
    ::close(fd);
}

bool Registry::registered(const std::string &id) const
{
    const int fd = util::open_directory_nofollow(bundle_dir(id));
    if (fd < 0) return false;
    struct stat st;
    const bool present = !::fstatat(fd, "manifest.json", &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode);
    ::close(fd);
    return present;
}

Registry::State Registry::state(const std::string &id, const std::string &expected_version, std::string *detail) const
{
    Bundle bundle;
    bundle.fd = util::open_directory_nofollow(bundle_dir(id));
    if (bundle.fd < 0) { if (detail) *detail = "bundle directory is missing"; return State::Missing; }
    ApplicationDescriptor descriptor;
    try {
        descriptor = read_descriptor(config_, bundle.fd, id, expected_version);
    } catch (const Error &error) {
        if (detail) *detail = error.what();
        return State::Invalid;
    }
    struct stat st;
    const bool exists = !::fstatat(bundle.fd, "manifest.json", &st, AT_SYMLINK_NOFOLLOW);
    if (!descriptor.launchable()) {
        if (detail && exists) *detail = "a library must not have a manifest";
        return exists ? State::Stale : State::Current;
    }
    if (!exists) { if (detail) *detail = "manifest.json is missing"; return State::Missing; }
    const std::string current = util::read_regular_file(bundle.fd, "manifest.json", 64 * 1024, config_.owner_uid);
    if (current != descriptor.manifest_json()) { if (detail) *detail = "manifest.json differs from Application.toml"; return State::Stale; }
    return State::Current;
}

struct Labeler::Impl {
    const Config *config = nullptr;
    struct selabel_handle *handle = nullptr;
};

Labeler::Labeler(const Config &config) : p(new Impl)
{
    p->config = &config;
    if (is_selinux_enabled() != 1 && config.selinux_file_contexts.empty()) {
        if (config.root.empty() && ::geteuid() == 0)
            throw Error("SELinux is not reachable from the package helper; refusing to install unlabelled files");
        return;
    }
    if (!config.selinux_file_contexts.empty()) {
        struct selinux_opt options[] = {{SELABEL_OPT_PATH, config.selinux_file_contexts.c_str()}};
        p->handle = selabel_open(SELABEL_CTX_FILE, options, 1);
    } else p->handle = selabel_open(SELABEL_CTX_FILE, nullptr, 0);
    if (!p->handle) throw Error(std::string("cannot open the SELinux file contexts: ") + std::strerror(errno));
}

Labeler::~Labeler()
{
    if (p && p->handle) selabel_close(p->handle);
}

bool Labeler::enabled() const { return p->handle != nullptr; }

void Labeler::label(const std::string &absolute_path, std::uint32_t mode) const
{
    if (!p->handle) return;
    char *context = nullptr;
    if (selabel_lookup_raw(p->handle, &context, absolute_path.c_str(), int(mode & S_IFMT)))
        throw Error("no SELinux label is defined for " + absolute_path);
    const std::string real = p->config->path(absolute_path);
    const int rc = ::lsetxattr(real.c_str(), "security.selinux", context, std::strlen(context) + 1, 0);
    const int saved = errno;
    freecon(context);
    if (rc) throw Error("cannot label " + absolute_path + ": " + util::errno_text(saved));
}

std::string Labeler::expected(const std::string &absolute_path, std::uint32_t mode) const
{
    if (!p->handle) return {};
    char *context = nullptr;
    if (selabel_lookup_raw(p->handle, &context, absolute_path.c_str(), int(mode & S_IFMT))) return {};
    std::string out = context;
    freecon(context);
    return out;
}

std::string Labeler::current(const std::string &absolute_path) const
{
    char buffer[256];
    const ssize_t size = ::lgetxattr(p->config->path(absolute_path).c_str(), "security.selinux", buffer, sizeof(buffer) - 1);
    if (size <= 0) return {};
    return std::string(buffer, buffer + (buffer[size - 1] == '\0' ? size - 1 : size));
}

namespace {
void walk(const Labeler &labeler, const Config &config, const std::string &logical, int directory_fd, std::size_t depth)
{
    if (depth > 64) throw Error("directory tree is too deep");
    DIR *stream = ::fdopendir(directory_fd);
    if (!stream) { ::close(directory_fd); return; }
    std::vector<std::string> names;
    while (dirent *entry = ::readdir(stream))
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) names.emplace_back(entry->d_name);
    for (const auto &name : names) {
        struct stat st;
        if (::fstatat(dirfd(stream), name.c_str(), &st, AT_SYMLINK_NOFOLLOW)) continue;
        const std::string child = logical + "/" + name;
        labeler.label(child, st.st_mode);
        if (S_ISDIR(st.st_mode)) {
            const int next = ::openat(dirfd(stream), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next >= 0) walk(labeler, config, child, next, depth + 1);
        }
    }
    ::closedir(stream);
}
}

void Labeler::label_tree(const std::string &absolute_dir) const
{
    if (!p->handle) return;
    const int fd = util::open_directory_nofollow(p->config->path(absolute_dir));
    if (fd < 0) throw Error("cannot open " + absolute_dir + " to label it");
    struct stat st;
    if (!::fstat(fd, &st)) label(absolute_dir, st.st_mode);
    walk(*this, *p->config, absolute_dir, fd, 0);
}

}
