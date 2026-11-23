#define _GNU_SOURCE
#include "appd.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

char *appd_deliver_file(Instance *instance, const char *path, GError **error)
{
    struct open_how how = {.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK, .resolve = RESOLVE_NO_SYMLINKS};
    int source = syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how));
    struct stat st;
    if (source < 0 || fstat(source, &st) || !S_ISREG(st.st_mode) || st.st_size > 128 * 1024 * 1024) {
        if (source >= 0) close(source);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Opening requires a regular file of at most 128 MiB");
        return NULL;
    }
    how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
    int runtime = syscall(SYS_openat2, AT_FDCWD, instance->runtime, &how, sizeof(how));
    char *uuid = g_uuid_string_random(), *directory = g_strconcat("OpenWith-", uuid, NULL);
    g_free(uuid);
    int target = -1, output = -1;
    char *name = g_path_get_basename(path), *result = NULL;
    gboolean okay = runtime >= 0 && !mkdirat(runtime, directory, 0700);
    if (okay) target = openat(runtime, directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (target >= 0) output = openat(target, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    okay = okay && output >= 0;
    char buffer[65536]; gsize total = 0;
    while (okay) {
        ssize_t count = read(source, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (!count) break;
        if (count < 0 || (total += count) > 128 * 1024 * 1024) { okay = FALSE; break; }
        for (ssize_t offset = 0; offset < count;) {
            ssize_t written = write(output, buffer + offset, count - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { okay = FALSE; break; }
            offset += written;
        }
    }
    if (okay) okay = !fchown(output, instance->uid, instance->uid) && !fchown(target, instance->uid, instance->uid);
    if (okay) result = g_build_filename(instance->runtime, directory, name, NULL);
    else {
        if (output >= 0) unlinkat(target, name, 0);
        if (runtime >= 0) unlinkat(runtime, directory, AT_REMOVEDIR);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not deliver a private copy to the application");
    }
    if (output >= 0) close(output);
    if (target >= 0) close(target);
    if (runtime >= 0) close(runtime);
    close(source); g_free(name); g_free(directory);
    return result;
}
