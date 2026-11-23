#include "appd.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

namespace {
// Bounded, descriptor-relative traversal: never follow application-controlled links.
guint64 sizeOf(int fd, guint &remaining, bool &complete, unsigned depth = 0)
{
    if (depth > 64 || !remaining) { close(fd); complete = false; return 0; }
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); complete = false; return 0; }
    guint64 bytes = 0;
    while (auto *entry = readdir(dir)) {
        if (entry->d_name[0] == '.' && (!entry->d_name[1] || (entry->d_name[1] == '.' && !entry->d_name[2]))) continue;
        if (!remaining) { complete = false; break; }
        --remaining;
        struct stat st{};
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) { complete = false; continue; }
        if (S_ISREG(st.st_mode)) bytes += st.st_size;
        else if (S_ISDIR(st.st_mode)) {
            int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (child < 0) complete = false;
            else bytes += sizeOf(child, remaining, complete, depth + 1);
        }
    }
    closedir(dir);
    return bytes;
}
guint64 directorySize(const char *path, bool &complete)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { if (errno != ENOENT) complete = false; return 0; }
    guint limit = 50000;
    return sizeOf(fd, limit, complete);
}
}
extern "C" GVariant *appd_storage_info(Manager *m, App *app)
{
    bool complete = !app->android;
    guint64 bytes = app->bundle && *app->bundle ? directorySize(app->bundle, complete) : 0;
    char *path = g_build_filename(m->home, ".DataLocation", "Applications", app->id, NULL);
    bytes += directorySize(path, complete);
    g_free(path);
    return g_variant_new("(tb)", bytes, complete);
}
