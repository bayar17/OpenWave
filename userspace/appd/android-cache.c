#define _GNU_SOURCE
#include "android-cache.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CACHE_NAME "catalogue.json"
#define CACHE_TEMPORARY ".catalogue.json.tmp"

static gboolean directory_sound(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022);
}

gboolean appd_android_cache_save(const char *root, const guchar *data, gsize length)
{
    if (!root || !data || !length) return FALSE;
    if (g_mkdir_with_parents(root, 0755) != 0 || !directory_sound(root)) return FALSE;
    char *path = g_build_filename(root, CACHE_NAME, NULL);
    char *temporary = g_build_filename(root, CACHE_TEMPORARY, NULL);
    unlink(temporary);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    gboolean okay = fd >= 0;
    gsize written = 0;
    while (okay && written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = FALSE; else written += (gsize)count;
    }
    okay = okay && fsync(fd) == 0 && rename(temporary, path) == 0;
    if (fd >= 0) close(fd);
    if (!okay) unlink(temporary);
    g_free(path);
    g_free(temporary);
    return okay;
}

gboolean appd_android_cache_load(const char *root, gsize limit, guchar **data, gsize *length)
{
    if (!root || !data || !length) return FALSE;
    *data = NULL;
    *length = 0;
    if (!directory_sound(root)) return FALSE;
    char *path = g_build_filename(root, CACHE_NAME, NULL);
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    g_free(path);
    if (fd < 0) return FALSE;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)
        || st.st_size <= 0 || (gsize)st.st_size > limit) {
        close(fd);
        return FALSE;
    }
    guchar *buffer = g_malloc((gsize)st.st_size);
    gsize total = 0;
    gboolean okay = TRUE;
    while (okay && total < (gsize)st.st_size) {
        ssize_t count = read(fd, buffer + total, (gsize)st.st_size - total);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = FALSE; else total += (gsize)count;
    }
    close(fd);
    if (!okay) { g_free(buffer); return FALSE; }
    *data = buffer;
    *length = total;
    return TRUE;
}
