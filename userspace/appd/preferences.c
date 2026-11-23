#define _GNU_SOURCE
#include "preferences.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const bool_keys[] = {"accessibility/reduceMotion", "accessibility/largeText", NULL};
static const char *const string_keys[] = {"device/name", NULL};
static const char *const list_keys[] = {"quickmenu/applets/order", "appearance/favorites", NULL};
static const char *const reset_groups[] = {"quickmenu/applets", NULL};
static const char state_prefix[] = "quickmenu/applets/state/";

gboolean appd_preference_valid_id(const char *id)
{
    if (!id || !id[0] || id[0] == '.' || strlen(id) > 64) return FALSE;
    for (const char *p = id; *p; ++p)
        if (!g_ascii_isalnum(*p) && *p != '.' && *p != '_' && *p != '-') return FALSE;
    return TRUE;
}

static gboolean listed(const char *const *table, const char *key)
{
    for (guint i = 0; table[i]; ++i) if (!strcmp(table[i], key)) return TRUE;
    return FALSE;
}

gboolean appd_preference_allowed(AppdPreferenceKind kind, const char *key)
{
    if (!key) return FALSE;
    switch (kind) {
    case APPD_PREFERENCE_BOOL:
        if (listed(bool_keys, key)) return TRUE;
        return g_str_has_prefix(key, state_prefix) && appd_preference_valid_id(key + sizeof state_prefix - 1);
    case APPD_PREFERENCE_STRING: return listed(string_keys, key);
    case APPD_PREFERENCE_LIST: return listed(list_keys, key);
    case APPD_PREFERENCE_RESET: return listed(reset_groups, key);
    }
    return FALSE;
}

char *appd_preference_encode_string(const char *value)
{
    if (!value || !g_utf8_validate(value, -1, NULL)) return NULL;
    gsize characters = g_utf8_strlen(value, -1);
    if (characters == 0 || characters > 64) return NULL;
    gboolean quote = g_ascii_isspace(value[0]) || g_ascii_isspace(value[strlen(value) - 1]);
    GString *out = g_string_new("\"");
    for (const char *p = value; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 0x20 || c == 0x7f) { g_string_free(out, TRUE); return NULL; }
        if (c == '"' || c == '\\') { g_string_append_c(out, '\\'); quote = TRUE; }
        else if (c > 0x7e || strchr(",;=#%@", (int)c)) quote = TRUE;
        g_string_append_unichar(out, c);
    }
    if (!quote) { g_string_free(out, TRUE); return g_strdup(value); }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}

static gboolean split_key(const char *key, char **group, char **name)
{
    const char *slash = strchr(key, '/');
    if (!slash || slash == key || !slash[1]) return FALSE;
    *group = g_strndup(key, slash - key);
    char **parts = g_strsplit(slash + 1, "/", -1);
    *name = g_strjoinv("\\", parts);
    g_strfreev(parts);
    return TRUE;
}

static gboolean fail(GError **error, int code, const char *what)
{
    int saved = errno;
    g_set_error(error, G_IO_ERROR, code ? g_io_error_from_errno(code) : G_IO_ERROR_FAILED, "%s%s%s", what, code ? ": " : "", code ? g_strerror(saved) : "");
    return FALSE;
}

static gboolean load_existing(int directory, const char *name, GKeyFile *file, GError **error)
{
    int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? TRUE : fail(error, errno, "Cannot read the preferences");
    struct stat st;
    gboolean okay = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size <= APPD_PREFERENCES_MAX_BYTES;
    char *data = okay ? g_malloc(st.st_size + 1) : NULL;
    gssize got = 0;
    if (okay) {
        while (got < st.st_size) {
            gssize n = read(fd, data + got, st.st_size - got);
            if (n <= 0) { okay = FALSE; break; }
            got += n;
        }
    }
    close(fd);
    if (!okay) { g_free(data); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "The preferences file is not a plain readable file"); return FALSE; }
    GError *parse = NULL;
    okay = got == 0 || g_key_file_load_from_data(file, data, got, G_KEY_FILE_NONE, &parse);
    g_free(data);
    if (!okay) { g_propagate_prefixed_error(error, parse, "The preferences file is unreadable: "); return FALSE; }
    return TRUE;
}

static void reset_group_keys(GKeyFile *file, const char *key)
{
    char *group = NULL, *name = NULL;
    if (!split_key(key, &group, &name)) return;
    if (g_key_file_has_group(file, group)) {
        gsize count = 0;
        char **keys = g_key_file_get_keys(file, group, &count, NULL);
        char *prefix = g_strconcat(name, "\\", NULL);
        for (gsize i = 0; keys && i < count; ++i)
            if (!strcmp(keys[i], name) || g_str_has_prefix(keys[i], prefix))
                g_key_file_remove_key(file, group, keys[i], NULL);
        g_free(prefix); g_strfreev(keys);
        char **left = g_key_file_get_keys(file, group, &count, NULL);
        if (!left || count == 0) g_key_file_remove_group(file, group, NULL);
        g_strfreev(left);
    }
    g_free(group); g_free(name);
}

static gboolean change_file(GKeyFile *file, const AppdPreferenceChange *change, GError **error)
{
    if (!appd_preference_allowed(change->kind, change->key)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "This preference cannot be changed");
        return FALSE;
    }
    if (change->kind == APPD_PREFERENCE_RESET) { reset_group_keys(file, change->key); return TRUE; }
    char *group = NULL, *name = NULL;
    if (!split_key(change->key, &group, &name)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "This preference cannot be changed");
        return FALSE;
    }
    gboolean okay = TRUE;
    if (change->kind == APPD_PREFERENCE_BOOL) {
        g_key_file_set_value(file, group, name, change->boolean ? "true" : "false");
    } else if (change->kind == APPD_PREFERENCE_STRING) {
        char *encoded = appd_preference_encode_string(change->string);
        if (!encoded) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The value is not acceptable");
            okay = FALSE;
        } else { g_key_file_set_value(file, group, name, encoded); g_free(encoded); }
    } else {
        guint count = 0;
        for (; change->list && change->list[count]; ++count)
            if (!appd_preference_valid_id(change->list[count]) || count >= APPD_PREFERENCES_MAX_LIST) { okay = FALSE; break; }
        if (!okay) g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "The list is not acceptable");
        else if (count == 0) g_key_file_remove_key(file, group, name, NULL);
        else {
            char *joined = g_strjoinv(", ", (char **)change->list);
            g_key_file_set_value(file, group, name, joined);
            g_free(joined);
        }
    }
    g_free(group); g_free(name);
    return okay;
}

gboolean appd_preferences_apply(const char *path, uid_t owner, gid_t group, const AppdPreferenceChange *change, GError **error)
{
    char *directory_path = g_path_get_dirname(path), *name = g_path_get_basename(path);
    gboolean okay = FALSE;
    GKeyFile *file = g_key_file_new();
    char *temporary = NULL;
    int directory = open(directory_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW), fd = -1;
    if (directory < 0 && errno == ENOENT && !g_mkdir_with_parents(directory_path, 0700)) {
        if (chown(directory_path, owner, group)) { fail(error, errno, "Cannot prepare the preferences directory"); goto done; }
        directory = open(directory_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (directory < 0) { fail(error, errno, "Cannot open the preferences directory"); goto done; }
    struct stat dir_stat;
    if (fstat(directory, &dir_stat) || dir_stat.st_uid != owner) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "The preferences directory has the wrong owner");
        goto done;
    }
    if (!load_existing(directory, name, file, error) || !change_file(file, change, error)) goto done;
    gsize length = 0;
    char *data = g_key_file_to_data(file, &length, NULL);
    if (length > APPD_PREFERENCES_MAX_BYTES) {
        g_free(data); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "The preferences file would be too large"); goto done;
    }
    char *template_path = g_strdup_printf("%s/.%s.tmp-XXXXXX", directory_path, name);
    fd = mkostemp(template_path, O_CLOEXEC);
    if (fd < 0) { g_free(data); g_free(template_path); fail(error, errno, "Cannot create the preferences file"); goto done; }
    temporary = g_path_get_basename(template_path);
    g_free(template_path);
    gsize written = 0;
    gboolean wrote = TRUE;
    while (written < length) {
        gssize n = write(fd, data + written, length - written);
        if (n <= 0) { wrote = FALSE; break; }
        written += n;
    }
    g_free(data);
    if (!wrote || fchown(fd, owner, group) || fchmod(fd, 0600) || fsync(fd)) { fail(error, errno, "Cannot write the preferences file"); goto done; }
    close(fd); fd = -1;
    int renamed = renameat(directory, temporary, directory, name);
    if (renamed) { fail(error, errno, "Cannot replace the preferences file"); goto done; }
    g_clear_pointer(&temporary, g_free);
    fsync(directory);
    okay = TRUE;
done:
    if (temporary && directory >= 0) unlinkat(directory, temporary, 0);
    if (fd >= 0) close(fd);
    if (directory >= 0) close(directory);
    g_free(temporary); g_key_file_unref(file); g_free(directory_path); g_free(name);
    return okay;
}

static void copy_group(GKeyFile *from, GKeyFile *to, const char *group, const char *only)
{
    if (!g_key_file_has_group(from, group)) return;
    gsize count = 0;
    char **keys = g_key_file_get_keys(from, group, &count, NULL);
    for (gsize i = 0; keys && i < count; ++i) {
        if (only && strcmp(only, keys[i])) continue;
        char *value = g_key_file_get_value(from, group, keys[i], NULL);
        if (value) g_key_file_set_value(to, group, keys[i], value);
        g_free(value);
    }
    g_strfreev(keys);
}

char *appd_preferences_public(GKeyFile *private_file, gsize *length)
{
    GKeyFile *shared = g_key_file_new();
    const char *flags[] = {"reduceMotion", "largeText", NULL};
    for (guint i = 0; flags[i]; ++i)
        g_key_file_set_boolean(shared, "accessibility", flags[i], g_key_file_get_boolean(private_file, "accessibility", flags[i], NULL));
    copy_group(private_file, shared, "device", "name");
    copy_group(private_file, shared, "appearance", "favorites");
    copy_group(private_file, shared, "quickmenu", NULL);
    char *data = g_key_file_to_data(shared, length, NULL);
    g_key_file_unref(shared);
    return data;
}
