#define _GNU_SOURCE
#include "mimeapps.h"
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULTS "Default Applications"
#define ADDED "Added Associations"
#define REMOVED "Removed Associations"
#define USER_FILE "mimeapps.list"

typedef struct {
    GKeyFile *file;
    char *path;
} Source;

struct MimeApps {
    GPtrArray *sources;
    char *digest;
};

static void source_free(gpointer data)
{
    Source *source = data;
    g_key_file_unref(source->file); g_free(source->path); g_free(source);
}

static gboolean valid_type(const char *mime)
{
    if (!mime || !*mime || strlen(mime) > 255) return FALSE;
    const char *slash = strchr(mime, '/');
    if (!slash || slash == mime || !slash[1] || strchr(slash + 1, '/')) return FALSE;
    for (const char *c = mime; *c; ++c)
        if (!g_ascii_isalnum(*c) && !strchr("/!#$&^_.+-*", *c)) return FALSE;
    return TRUE;
}

static gboolean read_file(int directory, const char *name, uid_t owner, gboolean any_owner, char **contents, gsize *length)
{
    int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > MIMEAPPS_MAX_BYTES
        || (!any_owner && st.st_uid != owner && st.st_uid != 0)) {
        if (fd >= 0) close(fd);
        return FALSE;
    }
    char *buffer = g_malloc(st.st_size + 1);
    gsize used = 0;
    while (used < (gsize)st.st_size) {
        ssize_t count = read(fd, buffer + used, st.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += count;
    }
    close(fd);
    buffer[used] = 0;
    if (strlen(buffer) != used) { g_free(buffer); return FALSE; }
    *contents = buffer; *length = used;
    return TRUE;
}

static void add_source(MimeApps *apps, GChecksum *sum, const char *path, uid_t owner, gboolean any_owner)
{
    char *directory_path = g_path_get_dirname(path);
    char *name = g_path_get_basename(path);
    int directory = open(directory_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char *contents = NULL; gsize length = 0;
    if (directory >= 0 && read_file(directory, name, owner, any_owner, &contents, &length)) {
        GKeyFile *file = g_key_file_new();
        if (g_key_file_load_from_data(file, contents, length, G_KEY_FILE_NONE, NULL)) {
            Source *source = g_new0(Source, 1);
            source->file = file; source->path = g_strdup(path);
            g_ptr_array_add(apps->sources, source);
            g_checksum_update(sum, (const guchar *)path, -1);
            g_checksum_update(sum, (const guchar *)contents, length);
        } else g_key_file_unref(file);
        g_free(contents);
    }
    if (directory >= 0) close(directory);
    g_free(directory_path); g_free(name);
}

char *mimeapps_user_path(const DesktopPaths *paths)
{
    return g_build_filename(paths->config_home, USER_FILE, NULL);
}

MimeApps *mimeapps_load(const DesktopPaths *paths)
{
    MimeApps *apps = g_new0(MimeApps, 1);
    apps->sources = g_ptr_array_new_with_free_func(source_free);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    GPtrArray *places = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *owned = g_ptr_array_new();
    char *specific = g_strdup_printf("%s-mimeapps.list", paths->desktop);
    char *lower = g_ascii_strdown(specific, -1);
    g_free(specific);
    const char *names[] = {lower, USER_FILE, NULL};
    for (guint n = 0; names[n]; ++n) g_ptr_array_add(places, g_build_filename(paths->config_home, names[n], NULL)), g_ptr_array_add(owned, GUINT_TO_POINTER(1));
    for (char **dir = paths->config_dirs; *dir; ++dir)
        for (guint n = 0; names[n]; ++n) g_ptr_array_add(places, g_build_filename(*dir, names[n], NULL)), g_ptr_array_add(owned, GUINT_TO_POINTER(0));
    for (guint n = 0; names[n]; ++n) g_ptr_array_add(places, g_build_filename(paths->data_home, "applications", names[n], NULL)), g_ptr_array_add(owned, GUINT_TO_POINTER(1));
    for (char **dir = paths->data_dirs; *dir; ++dir)
        for (guint n = 0; names[n]; ++n) g_ptr_array_add(places, g_build_filename(*dir, "applications", names[n], NULL)), g_ptr_array_add(owned, GUINT_TO_POINTER(0));
    for (guint i = 0; i < places->len; ++i)
        add_source(apps, sum, g_ptr_array_index(places, i), paths->uid, !GPOINTER_TO_UINT(g_ptr_array_index(owned, i)));
    g_ptr_array_unref(places); g_ptr_array_unref(owned); g_free(lower);
    apps->digest = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
    return apps;
}

void mimeapps_free(MimeApps *apps)
{
    if (!apps) return;
    g_ptr_array_unref(apps->sources); g_free(apps->digest); g_free(apps);
}

const char *mimeapps_digest(const MimeApps *apps)
{
    return apps->digest;
}

static char **list(Source *source, const char *group, const char *mime)
{
    gsize count = 0;
    char **values = g_key_file_get_string_list(source->file, group, mime, &count, NULL);
    GPtrArray *clean = g_ptr_array_new();
    for (gsize i = 0; values && i < count; ++i)
        if (desktop_id_valid(values[i])) g_ptr_array_add(clean, g_strdup(values[i]));
    g_strfreev(values);
    g_ptr_array_add(clean, NULL);
    return (char **)g_ptr_array_free(clean, FALSE);
}

static void append_unique(GPtrArray *out, const char *id)
{
    for (guint i = 0; i < out->len; ++i) if (!strcmp(g_ptr_array_index(out, i), id)) return;
    g_ptr_array_add(out, g_strdup(id));
}

char **mimeapps_default_ids(const MimeApps *apps, const char *mime)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    if (valid_type(mime))
        for (guint i = 0; i < apps->sources->len; ++i) {
            char **ids = list(g_ptr_array_index(apps->sources, i), DEFAULTS, mime);
            for (char **id = ids; *id; ++id) append_unique(out, *id);
            g_strfreev(ids);
        }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

char *mimeapps_default(const MimeApps *apps, const char *mime, MimeAppsUsable usable, gpointer data)
{
    char **ids = mimeapps_default_ids(apps, mime);
    char *chosen = NULL;
    for (char **id = ids; *id && !chosen; ++id)
        if (!usable || usable(*id, data)) chosen = g_strdup(*id);
    g_strfreev(ids);
    return chosen;
}

GPtrArray *mimeapps_associated(const MimeApps *apps, const char *mime, GPtrArray *entries)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    if (!valid_type(mime)) return out;
    GHashTable *removed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < apps->sources->len; ++i) {
        Source *source = g_ptr_array_index(apps->sources, i);
        char **added = list(source, ADDED, mime);
        for (char **id = added; *id; ++id) if (!g_hash_table_contains(removed, *id)) append_unique(out, *id);
        g_strfreev(added);
        char **gone = list(source, REMOVED, mime);
        for (char **id = gone; *id; ++id) g_hash_table_add(removed, g_strdup(*id));
        g_strfreev(gone);
    }
    for (guint i = 0; entries && i < entries->len; ++i) {
        DesktopEntry *entry = g_ptr_array_index(entries, i);
        if (desktop_entry_has_mime(entry, mime) && !g_hash_table_contains(removed, entry->id)) append_unique(out, entry->id);
    }
    g_hash_table_unref(removed);
    return out;
}

GPtrArray *mimeapps_known_types(const MimeApps *apps)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    const char *groups[] = {DEFAULTS, ADDED, NULL};
    for (guint i = 0; i < apps->sources->len; ++i) {
        Source *source = g_ptr_array_index(apps->sources, i);
        for (guint g = 0; groups[g]; ++g) {
            char **keys = g_key_file_get_keys(source->file, groups[g], NULL, NULL);
            for (char **key = keys; keys && *key; ++key) if (valid_type(*key)) append_unique(out, *key);
            g_strfreev(keys);
        }
    }
    return out;
}

static int open_directory(int parent, const char *name, uid_t uid, gid_t gid, gboolean create, GError **error)
{
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT && create) {
        if (mkdirat(parent, name, 0700) == 0) {
            int made = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (made >= 0 && fchown(made, uid, gid) == 0) return made;
            if (made >= 0) close(made);
        }
        fd = -1;
    }
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_uid != uid) {
        if (fd >= 0) close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "The configuration directory %s is missing, is a link, or has the wrong owner", name);
        return -1;
    }
    return fd;
}

static int open_config(const DesktopPaths *paths, gboolean create, GError **error)
{
    int home = open(paths->home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (home < 0 || fstat(home, &st) || st.st_uid != paths->uid) {
        if (home >= 0) close(home);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "The session home directory is unavailable or has the wrong owner");
        return -1;
    }
    int config = open_directory(home, ".config", paths->uid, paths->gid, create, error);
    close(home);
    return config;
}

static gboolean save(const DesktopPaths *paths, GKeyFile *file, int directory, GError **error)
{
    gsize length = 0;
    char *data = g_key_file_to_data(file, &length, NULL);
    if (length > MIMEAPPS_MAX_BYTES) {
        g_free(data);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "The default applications file is full");
        return FALSE;
    }
    char *temporary = g_strdup_printf(".mimeapps.list.%d.tmp", getpid());
    int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    gboolean okay = fd >= 0 && fchown(fd, paths->uid, paths->gid) == 0;
    gsize written = 0;
    while (okay && written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = FALSE; else written += count;
    }
    okay = okay && fsync(fd) == 0 && renameat(directory, temporary, directory, USER_FILE) == 0;
    if (fd >= 0) close(fd);
    if (!okay) {
        unlinkat(directory, temporary, 0);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "The default applications file could not be saved: %s", g_strerror(errno));
    } else fsync(directory);
    g_free(temporary); g_free(data);
    return okay;
}

static GKeyFile *load_user(int directory)
{
    GKeyFile *file = g_key_file_new();
    char *contents = NULL; gsize length = 0;
    struct stat st;
    if (fstatat(directory, USER_FILE, &st, AT_SYMLINK_NOFOLLOW) == 0 && read_file(directory, USER_FILE, st.st_uid, TRUE, &contents, &length)) {
        if (!g_key_file_load_from_data(file, contents, length, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
            g_key_file_unref(file);
            file = g_key_file_new();
        }
        g_free(contents);
    }
    return file;
}

static void edit_list(GKeyFile *file, const char *group, const char *mime, const char *id, gboolean add, gboolean front)
{
    gsize count = 0;
    char **values = g_key_file_get_string_list(file, group, mime, &count, NULL);
    GPtrArray *next = g_ptr_array_new();
    if (add && front) g_ptr_array_add(next, g_strdup(id));
    for (gsize i = 0; values && i < count; ++i) {
        gboolean same = !strcmp(values[i], id);
        if (!same) g_ptr_array_add(next, g_strdup(values[i]));
        else if (add && !front) { g_ptr_array_add(next, g_strdup(values[i])); add = FALSE; }
    }
    if (add && !front) g_ptr_array_add(next, g_strdup(id));
    g_strfreev(values);
    if (next->len) g_key_file_set_string_list(file, group, mime, (const char *const *)next->pdata, next->len);
    else g_key_file_remove_key(file, group, mime, NULL);
    g_ptr_array_set_free_func(next, g_free);
    g_ptr_array_free(next, TRUE);
}

gboolean mimeapps_set_default(const DesktopPaths *paths, const char *mime, const char *desktop_id, GError **error)
{
    if (!valid_type(mime) || !desktop_id_valid(desktop_id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid MIME type or desktop file ID");
        return FALSE;
    }
    int directory = open_config(paths, TRUE, error);
    if (directory < 0) return FALSE;
    GKeyFile *file = load_user(directory);
    g_key_file_set_string(file, DEFAULTS, mime, desktop_id);
    edit_list(file, ADDED, mime, desktop_id, TRUE, FALSE);
    edit_list(file, REMOVED, mime, desktop_id, FALSE, FALSE);
    gboolean okay = save(paths, file, directory, error);
    g_key_file_unref(file); close(directory);
    return okay;
}

gboolean mimeapps_clear_default(const DesktopPaths *paths, const char *mime, GError **error)
{
    if (!valid_type(mime)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid MIME type");
        return FALSE;
    }
    int directory = open_config(paths, FALSE, NULL);
    if (directory < 0) return TRUE;
    GKeyFile *file = load_user(directory);
    gboolean okay = TRUE;
    if (g_key_file_has_key(file, DEFAULTS, mime, NULL)) {
        g_key_file_remove_key(file, DEFAULTS, mime, NULL);
        okay = save(paths, file, directory, error);
    }
    g_key_file_unref(file); close(directory);
    return okay;
}

gboolean mimeapps_clear_application(const DesktopPaths *paths, const char *desktop_id, GError **error)
{
    if (!desktop_id_valid(desktop_id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid desktop file ID");
        return FALSE;
    }
    int directory = open_config(paths, FALSE, NULL);
    if (directory < 0) return TRUE;
    GKeyFile *file = load_user(directory);
    gboolean changed = FALSE;
    const char *groups[] = {DEFAULTS, ADDED, NULL};
    for (guint g = 0; groups[g]; ++g) {
        char **keys = g_key_file_get_keys(file, groups[g], NULL, NULL);
        for (char **key = keys; keys && *key; ++key) {
            char **values = g_key_file_get_string_list(file, groups[g], *key, NULL, NULL);
            gboolean has = FALSE;
            for (char **value = values; values && *value; ++value) has |= !strcmp(*value, desktop_id);
            g_strfreev(values);
            if (has) { edit_list(file, groups[g], *key, desktop_id, FALSE, FALSE); changed = TRUE; }
        }
        g_strfreev(keys);
    }
    gboolean okay = TRUE;
    if (changed) okay = save(paths, file, directory, error);
    g_key_file_unref(file); close(directory);
    return okay;
}
