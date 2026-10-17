#define _GNU_SOURCE
#include "user-folders.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

gboolean appd_user_folders(const char *home, const char *data, uid_t uid, GError **error)
{
    const char *folders[][3] = {{"Desktop", "Desktop", "DESKTOP"}, {"Documents", "Documents", "DOCUMENTS"},
        {"Downloads", "Downloads", "DOWNLOAD"}, {"Pictures", "Pictures", "PICTURES"},
        {"Music", "Musics", "MUSIC"}, {"Musics", "Musics", NULL}, {"Videos", "Videos", "VIDEOS"},
        {"Public", "Public", "PUBLICSHARE"}, {"Templates", "Templates", "TEMPLATES"}};
    int directory = open(data, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) goto failure;
    gboolean okay = TRUE;
    GString *config = g_string_new(NULL);
    for (guint i = 0; okay && i < G_N_ELEMENTS(folders); ++i) {
        char *target = g_build_filename(home, folders[i][1], NULL);
        if (symlinkat(target, directory, folders[i][0])) okay = errno == EEXIST;
        else okay = !fchownat(directory, folders[i][0], uid, uid, AT_SYMLINK_NOFOLLOW);
        g_free(target);
        if (folders[i][2]) g_string_append_printf(config, "XDG_%s_DIR=\"$HOME/%s\"\n", folders[i][2], folders[i][0]);
    }
    int config_fd = okay ? openat(directory, "Config", O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (config_fd < 0) okay = FALSE;
    struct stat st;
    if (okay && fstatat(config_fd, "user-dirs.dirs", &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno != ENOENT) okay = FALSE;
        else {
            char *uuid = g_uuid_string_random();
            char *temporary = g_strconcat(".user-dirs-", uuid, NULL); g_free(uuid);
            int fd = openat(config_fd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            okay = fd >= 0 && !fchown(fd, uid, uid);
            gsize offset = 0;
            while (okay && offset < config->len) {
                ssize_t size = write(fd, config->str + offset, config->len - offset);
                if (size < 0 && errno == EINTR) continue;
                if (size <= 0) okay = FALSE; else offset += size;
            }
            if (okay) okay = !fsync(fd);
            if (fd >= 0) close(fd);
            if (okay) okay = !syscall(SYS_renameat2, config_fd, temporary, config_fd, "user-dirs.dirs", RENAME_NOREPLACE) || errno == EEXIST;
            unlinkat(config_fd, temporary, 0); g_free(temporary);
            if (okay) okay = !fsync(config_fd);
        }
    }
    if (config_fd >= 0) close(config_fd);
    if (okay) okay = !fsync(directory);
    close(directory); g_string_free(config, TRUE);
    if (okay) return TRUE;
failure:
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot prepare application user folders: %s", g_strerror(errno));
    return FALSE;
}
