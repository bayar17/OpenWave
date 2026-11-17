#define _GNU_SOURCE
#include "uninstall-storage.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/openat2.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int directory_at(int parent, const char *name, gboolean child)
{
    struct open_how how = {.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC,
        .resolve = RESOLVE_NO_SYMLINKS | (child ? RESOLVE_BENEATH | RESOLVE_NO_XDEV : 0)};
    return syscall(SYS_openat2, parent, name, &how, sizeof(how));
}
static gboolean remove_entry(int parent, const char *name, unsigned depth)
{
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlinkat(parent, name, 0) == 0;
    if (depth >= 128) { errno = ELOOP; return FALSE; }
    int fd = directory_at(parent, name, TRUE);
    if (fd < 0) return FALSE;
    DIR *directory = fdopendir(fd);
    if (!directory) { close(fd); return FALSE; }
    struct dirent *entry;
    gboolean okay = TRUE;
    errno = 0;
    while (okay && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        okay = remove_entry(fd, entry->d_name, depth + 1);
        if (okay) errno = 0;
    }
    if (okay && errno) okay = FALSE;
    int saved = errno;
    closedir(directory);
    errno = saved;
    return okay && unlinkat(parent, name, AT_REMOVEDIR) == 0;
}
static int protected_directory(const char *path)
{
    int fd = directory_at(AT_FDCWD, path, FALSE);
    struct stat st;
    if (fd >= 0 && (!fstat(fd, &st) && st.st_uid == geteuid() && !(st.st_mode & 0022))) return fd;
    if (fd >= 0) { close(fd); errno = EPERM; }
    return -1;
}
gboolean appd_uninstall_storage(const char *bundles, const char *data, const char *id, GError **error)
{
    const char *prefix = "org.openwave.webapp.h";
    if (!id || !g_str_has_prefix(id, prefix) || strlen(id) != strlen(prefix) + 64) goto invalid;
    for (const char *p = id + strlen(prefix); *p; ++p) if (!g_ascii_isxdigit(*p) || g_ascii_isupper(*p)) goto invalid;
    int apps = protected_directory(bundles);
    if (apps < 0) goto failure;
    char *pending = g_strconcat(".uninstall-", id, NULL);
    struct stat st;
    gboolean staged = fstatat(apps, pending, &st, AT_SYMLINK_NOFOLLOW) == 0;
    gboolean okay = staged ? S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022)
        : errno == ENOENT && !syscall(SYS_renameat2, apps, id, apps, pending, RENAME_NOREPLACE) && !fsync(apps);
    if (staged && !okay) errno = EPERM;
    if (staged && !fstatat(apps, id, &st, AT_SYMLINK_NOFOLLOW)) { errno = EEXIST; okay = FALSE; }
    if (okay) {
        int storage = protected_directory(data);
        if (storage < 0) okay = errno == ENOENT;
        else {
            gboolean quarantined = fstatat(storage, pending, &st, AT_SYMLINK_NOFOLLOW) == 0;
            if (quarantined && !fstatat(storage, id, &st, AT_SYMLINK_NOFOLLOW)) { errno = EEXIST; okay = FALSE; }
            else if (!quarantined && syscall(SYS_renameat2, storage, id, storage, pending, RENAME_NOREPLACE)) okay = errno == ENOENT;
            if (okay) okay = !fsync(storage) && remove_entry(storage, pending, 0) && !fsync(storage);
            int saved = errno; close(storage); errno = saved;
        }
    }
    if (okay) okay = remove_entry(apps, pending, 0) && !fsync(apps);
    int saved = errno; close(apps); g_free(pending); errno = saved;
    if (okay) return TRUE;
failure:
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
        "Web app cleanup could not finish: %s. Pending cleanup will resume when the application service restarts.", g_strerror(errno));
    return FALSE;
invalid:
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Only an installed website app can be uninstalled.");
    return FALSE;
}
