#define _GNU_SOURCE
#include "appd.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

const char *appd_legacy_id(App *app)
{
    if (!g_str_has_prefix(app->bundle, "/System/Applications/")) return NULL;
    if (!strcmp(app->id, "com.openwave.ocean")) return "browser";
    if (!strcmp(app->id, "com.openwave.files")) return "files";
    if (!strcmp(app->id, "com.openwave.settings")) return "settings";
    return NULL;
}

static gboolean merge(const char *source, const char *target, GError **error)
{
    struct stat from, to;
    if (lstat(source, &from)) { if (errno == ENOENT) return TRUE; goto invalid; }
    if (S_ISLNK(from.st_mode) || (!S_ISDIR(from.st_mode) && (!S_ISREG(from.st_mode) || from.st_nlink != 1))) goto invalid;
    if (lstat(target, &to)) {
        if (errno != ENOENT || syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, target, RENAME_NOREPLACE)) goto invalid;
        char *parent = g_path_get_dirname(target);
        int fd = open(parent, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        g_free(parent);
        gboolean okay = fd >= 0 && !fsync(fd); if (fd >= 0) close(fd);
        if (!okay) goto invalid;
        parent = g_path_get_dirname(source);
        fd = open(parent, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC); g_free(parent);
        okay = fd >= 0 && !fsync(fd); if (fd >= 0) close(fd);
        if (!okay) goto invalid;
        return TRUE;
    }
    if (!S_ISDIR(from.st_mode) || !S_ISDIR(to.st_mode)) goto invalid;
    GDir *directory = g_dir_open(source, 0, error);
    if (!directory) return FALSE;
    const char *name; gboolean okay = TRUE;
    while (okay && (name = g_dir_read_name(directory))) {
        char *src = g_build_filename(source, name, NULL), *dst = g_build_filename(target, name, NULL);
        okay = merge(src, dst, error); g_free(src); g_free(dst);
    }
    g_dir_close(directory);
    if (!okay) return FALSE;
    if (rmdir(source)) goto invalid;
    return TRUE;
invalid:
    if (error && !*error) g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
        "Storage migration refused unsafe or conflicting paths: %s -> %s. Both copies were preserved.", source, target);
    return FALSE;
}

static gboolean move_relative(Instance *instance, const char *from, const char *to, GError **error)
{
    char *src = g_build_filename(instance->data, from, NULL), *dst = g_build_filename(instance->data, to, NULL);
    struct stat st; if (!lstat(src, &st)) instance->storage_changed = TRUE;
    gboolean okay = merge(src, dst, error); g_free(src); g_free(dst); return okay;
}

gboolean appd_prepare_storage(Manager *manager, Instance *instance, GError **error)
{
    char *parent = g_strdup_printf("%s/.DataLocation/Applications", manager->home);
    if (g_mkdir_with_parents(parent, 0711)) { g_free(parent); return FALSE; }
    // Parent directories are system managed; do not follow application-supplied links.
    char *canonical = realpath(parent, NULL);
    gboolean okay = canonical && !strcmp(canonical, parent); free(canonical);
    const char *legacy = appd_legacy_id(instance->app);
    if (okay && legacy) {
        char *old = g_build_filename(parent, legacy, NULL);
        struct stat old_stat; if (!lstat(old, &old_stat)) instance->storage_changed = TRUE;
        okay = merge(old, instance->data, error); g_free(old);
    }
    g_free(parent);
    if (!okay) return FALSE;
    struct stat st;
    if (lstat(instance->data, &st)) {
        if (errno != ENOENT || mkdir(instance->data, 0700)) return FALSE;
        instance->storage_changed = TRUE;
    }
    if (lstat(instance->data, &st) || !S_ISDIR(st.st_mode)) return FALSE;
    for (const char **p = (const char *[]) {"Config", "Cache", "Data", NULL}; *p; ++p) {
        char *path = g_build_filename(instance->data, *p, NULL);
        if (!lstat(path, &st)) okay = S_ISDIR(st.st_mode);
        else {
            okay = errno == ENOENT && !mkdir(path, 0700);
            if (okay) instance->storage_changed = TRUE;
        }
        g_free(path); if (!okay) return FALSE;
    }
    if (legacy && !strcmp(legacy, "browser")) {
        const char *moves[][2] = {{"cache", "Cache"}, {"Browser", "Data/Browser"},
            {"Data/Browser/Settings.json", "Config/Settings.json"}, {"Downloads", "Data/Downloads"},
            {"WebEngine", "Data/WebEngine"}, {"cookies", "Data/cookies"}, {"passwords", "Data/passwords"},
            {"tabs.json", "Data/tabs.json"}, {"bookmarks.json", "Data/bookmarks.json"},
            {"history.json", "Data/history.json"}, {"state", "Data/state"}};
        for (guint i = 0; i < G_N_ELEMENTS(moves); ++i) {
            if (!strcmp(moves[i][0], "Downloads")) {
                char *alias = g_build_filename(instance->data, "Downloads", NULL);
                char *target = g_file_read_link(alias, NULL);
                char *expected = g_build_filename(manager->home, "Downloads", NULL);
                gboolean managed = target && !strcmp(target, expected);
                g_free(alias); g_free(target); g_free(expected);
                if (managed) continue;
            }
            if (!move_relative(instance, moves[i][0], moves[i][1], error)) return FALSE;
        }
    }
    return TRUE;
}
