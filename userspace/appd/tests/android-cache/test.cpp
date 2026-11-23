#include "../../android-cache.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

std::string loadBack(const std::string &root, gsize limit, bool &ok)
{
    guchar *data = nullptr;
    gsize length = 0;
    ok = appd_android_cache_load(root.c_str(), limit, &data, &length);
    std::string bytes = ok ? std::string(reinterpret_cast<const char *>(data), length) : std::string();
    g_free(data);
    return bytes;
}

bool save(const std::string &root, const std::string &bytes)
{
    return appd_android_cache_save(root.c_str(), reinterpret_cast<const guchar *>(bytes.data()), bytes.size());
}

bool exists(const std::string &path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}

bool writeFile(const std::string &path, const std::string &bytes)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool okay = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return okay;
}
}

int main()
{
    char pattern[] = "/tmp/appd-android-cache-XXXXXX";
    char *base = mkdtemp(pattern);
    check(base != nullptr, "temporary directory");
    if (!base) return 1;
    const std::string root = std::string(base) + "/android";
    const std::string file = root + "/catalogue.json";
    const std::string first = R"({"apps":[{"package":"com.example.one","name":"One"}]})";
    const std::string second = R"({"apps":[{"package":"com.example.two","name":"Two"}]})";
    bool ok = true;

    loadBack(root, 4096, ok);
    check(!ok, "loading before anything was saved fails");

    check(save(root, first), "first save creates the directory and the file");
    check(loadBack(root, 4096, ok) == first && ok, "first save round-trips");
    check(save(root, second), "second save replaces the file");
    check(loadBack(root, 4096, ok) == second && ok, "second save round-trips");
    check(!exists(root + "/.catalogue.json.tmp"), "no temporary file is left behind");

    struct stat st;
    check(stat(file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0644, "file mode is 0644");

    loadBack(root, 10, ok);
    check(!ok, "a file larger than the limit is refused");

    check(!save(root, std::string()), "an empty catalogue is never saved");
    check(loadBack(root, 4096, ok) == second && ok, "an empty save leaves the old file");

    chmod(file.c_str(), 0666);
    loadBack(root, 4096, ok);
    check(!ok, "a world-writable file is refused");
    chmod(file.c_str(), 0644);
    check(loadBack(root, 4096, ok) == second && ok, "the file is trusted again once fixed");

    const std::string elsewhere = std::string(base) + "/elsewhere";
    check(writeFile(elsewhere, first), "write the symlink target");
    check(unlink(file.c_str()) == 0, "remove the file to plant a symlink");
    check(symlink(elsewhere.c_str(), file.c_str()) == 0, "plant a symlink");
    loadBack(root, 4096, ok);
    check(!ok, "a symlinked cache file is refused");
    unlink(file.c_str());

    check(save(root, first), "save works again after the symlink is removed");
    chmod(root.c_str(), 0777);
    loadBack(root, 4096, ok);
    check(!ok, "a world-writable directory is refused for loading");
    check(!save(root, second), "a world-writable directory is refused for saving");
    chmod(root.c_str(), 0755);
    check(loadBack(root, 4096, ok) == first && ok, "the directory is trusted again once fixed");

    unlink(file.c_str());
    unlink(elsewhere.c_str());
    rmdir(root.c_str());
    rmdir(base);

    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("appd android cache tests passed");
    return 0;
}
