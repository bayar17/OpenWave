#define _GNU_SOURCE
#include "appd.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef APPD_DEFAULTS_DIRECTORY
#define APPD_DEFAULTS_DIRECTORY "/var/lib/openwave/app-identities"
#endif
static char *defaults_path(Manager *m) { return g_strdup_printf(APPD_DEFAULTS_DIRECTORY "/file-defaults-%u.ini", m->uid); }
#define DEFAULTS_GROUP "Defaults"

static const struct { const char *extension; const char *handler; } builtin[] = {
    {"application/x-rpm", APPD_PACKAGE_INSTALLER},
    {"application/vnd.android.package-archive", APPD_ANDROID_INSTALLER},
};

static gboolean builtin_handles(const char *extension, const char *id)
{
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i)
        if (!strcmp(builtin[i].extension, extension) && !strcmp(builtin[i].handler, id)) return TRUE;
    return FALSE;
}

static gboolean app_handles(App *app, const char *extension)
{
    for (int i = 0; i < 2; ++i)
        for (char **opened = i ? app->mime_types : app->opens; opened && *opened; ++opened) {
            char *key = appd_type_key(*opened);
            gboolean same = key && !strcmp(key, extension);
            g_free(key);
            if (same) return TRUE;
        }
    return FALSE;
}

gboolean appd_handler_valid(Manager *m, const char *type, const char *id)
{
    char *key = appd_type_key(type);
    if (!key) return FALSE;
    gboolean valid = FALSE;
    // Installation handlers are fixed service routes, never ordinary app defaults.
    if (!strcmp(key, "application/x-rpm") || !strcmp(key, "application/vnd.android.package-archive"))
        valid = builtin_handles(key, id);
    else {
        App *app = g_hash_table_lookup(m->apps, id);
        valid = app && !app->removing && app->paths && app_handles(app, key);
    }
    g_free(key);
    return valid;
}

static gboolean save_defaults(Manager *m, GKeyFile *file, GError **error);

static void migrate_defaults(Manager *m)
{
    char *path = defaults_path(m);
    const char *legacy = APPD_DEFAULTS_DIRECTORY "/file-defaults.ini";
    struct stat st;
    if (lstat(path, &st) == 0 || lstat(legacy, &st) || !S_ISREG(st.st_mode)
        || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size > 65536) { g_free(path); return; }
    GKeyFile *old = g_key_file_new(), *file = g_key_file_new();
    if (g_key_file_load_from_file(old, legacy, G_KEY_FILE_NONE, NULL)) {
        char **keys = g_key_file_get_keys(old, DEFAULTS_GROUP, NULL, NULL);
        for (char **key = keys; key && *key; ++key) {
            char *chosen = g_key_file_get_string(old, DEFAULTS_GROUP, *key, NULL);
            char *type = appd_type_key(*key);
            if (type && chosen && appd_handler_valid(m, type, chosen)
                && !builtin_handles(type, APPD_PACKAGE_INSTALLER) && !builtin_handles(type, APPD_ANDROID_INSTALLER))
                g_key_file_set_string(file, DEFAULTS_GROUP, type, chosen);
            g_free(type); g_free(chosen);
        }
        g_strfreev(keys);
        GError *error = NULL;
        if (save_defaults(m, file, &error)) {
            // Move the old global file once, so a second user cannot inherit it.
            if (rename(legacy, APPD_DEFAULTS_DIRECTORY "/file-defaults.legacy") != 0)
                g_warning("Could not archive legacy file defaults");
        } else { g_warning("Could not migrate defaults: %s", error->message); g_clear_error(&error); }
    }
    g_key_file_free(file); g_key_file_free(old); g_free(path);
}

static GKeyFile *load_defaults(Manager *m)
{
    GKeyFile *file = g_key_file_new();
    char *path = defaults_path(m);
    struct stat st;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size > 65536
        || !g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(file);
        file = g_key_file_new();
    }
    g_free(path);
    return file;
}

GVariant *appd_file_associations(Manager *m)
{
    GVariantBuilder defaults, handlers;
    g_variant_builder_init(&defaults, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_init(&handlers, G_VARIANT_TYPE("a{sas}"));
    GHashTable *extensions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i) {
        GPtrArray *ids = g_hash_table_lookup(extensions, builtin[i].extension);
        if (!ids) { ids = g_ptr_array_new_with_free_func(g_free); g_hash_table_insert(extensions, g_strdup(builtin[i].extension), ids); }
        g_ptr_array_add(ids, g_strdup(builtin[i].handler));
    }
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        App *app = value;
        if (app->removing) continue;
        for (int list = 0; list < 2; ++list)
        for (char **opened = list ? app->mime_types : app->opens; opened && *opened; ++opened) {
            char *key = appd_type_key(*opened);
            if (!key || !appd_handler_valid(m, key, app->id)) { g_free(key); continue; }
            GPtrArray *ids = g_hash_table_lookup(extensions, key);
            if (!ids) { ids = g_ptr_array_new_with_free_func(g_free); g_hash_table_insert(extensions, g_strdup(key), ids); }
            if (!g_ptr_array_find_with_equal_func(ids, app->id, (GEqualFunc)g_str_equal, NULL))
                g_ptr_array_add(ids, g_strdup(app->id));
            g_free(key);
        }
    }
    GKeyFile *file = load_defaults(m);
    g_hash_table_iter_init(&iter, extensions);
    while (g_hash_table_iter_next(&iter, &value, NULL)) {
        const char *extension = value;
        GPtrArray *ids = g_hash_table_lookup(extensions, extension);
        g_ptr_array_add(ids, NULL);
        g_variant_builder_add(&handlers, "{s@as}", extension, g_variant_new_strv((const char *const *)ids->pdata, (gssize)ids->len - 1));
        char *chosen = g_key_file_get_string(file, DEFAULTS_GROUP, extension, NULL);
        if (chosen && appd_handler_valid(m, extension, chosen)) g_variant_builder_add(&defaults, "{ss}", extension, chosen);
        g_free(chosen);
    }
    g_key_file_free(file);
    g_hash_table_unref(extensions);
    return g_variant_new("(@a{ss}@a{sas})", g_variant_builder_end(&defaults), g_variant_builder_end(&handlers));
}

static gboolean save_defaults(Manager *m, GKeyFile *file, GError **error)
{
    gsize length;
    char *data = g_key_file_to_data(file, &length, NULL);
    if (length > 65536) {
        g_free(data);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "The file defaults database is full");
        return FALSE;
    }
    char *path = defaults_path(m);
    gboolean saved = g_file_set_contents_full(path, data, (gssize)length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
    g_free(path); g_free(data);
    if (saved) appd_emit(m, "FileDefaultsChanged", NULL);
    return saved;
}

gboolean appd_set_file_default(Manager *m, const char *type, const char *id, GError **error)
{
    char *key = appd_type_key(type);
    if (!key || builtin_handles(key, APPD_PACKAGE_INSTALLER) || builtin_handles(key, APPD_ANDROID_INSTALLER)
        || (*id && !appd_handler_valid(m, key, id))) {
        g_free(key);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid type, incompatible application, or protected package type");
        return FALSE;
    }
    GKeyFile *file = load_defaults(m);
    if (*id) g_key_file_set_string(file, DEFAULTS_GROUP, key, id);
    else g_key_file_remove_key(file, DEFAULTS_GROUP, key, NULL);
    g_free(key);
    gboolean saved = save_defaults(m, file, error);
    g_key_file_free(file);
    return saved;
}

gboolean appd_clear_file_defaults(Manager *m, const char *id, GError **error)
{
    GKeyFile *file = load_defaults(m);
    char **keys = g_key_file_get_keys(file, DEFAULTS_GROUP, NULL, NULL);
    for (char **key = keys; key && *key; ++key) {
        char *chosen = g_key_file_get_string(file, DEFAULTS_GROUP, *key, NULL);
        if (!g_strcmp0(chosen, id)) g_key_file_remove_key(file, DEFAULTS_GROUP, *key, NULL);
        g_free(chosen);
    }
    g_strfreev(keys);
    gboolean saved = save_defaults(m, file, error);
    g_key_file_free(file);
    return saved;
}

void appd_prune_file_defaults(Manager *m)
{
    migrate_defaults(m);
    GKeyFile *file = load_defaults(m);
    char **keys = g_key_file_get_keys(file, DEFAULTS_GROUP, NULL, NULL);
    gboolean changed = FALSE;
    for (char **key = keys; key && *key; ++key) {
        char *chosen = g_key_file_get_string(file, DEFAULTS_GROUP, *key, NULL);
        App *pending = chosen ? g_hash_table_lookup(m->apps, chosen) : NULL;
        if (pending && pending->removing) { g_free(chosen); continue; }
        if (!chosen || !appd_handler_valid(m, *key, chosen)) {
            g_key_file_remove_key(file, DEFAULTS_GROUP, *key, NULL); changed = TRUE;
        }
        g_free(chosen);
    }
    if (changed) {
        GError *error = NULL;
        if (!save_defaults(m, file, &error)) { g_warning("Cannot prune file defaults: %s", error->message); g_clear_error(&error); }
    }
    g_strfreev(keys); g_key_file_free(file);
}

GVariant *appd_resolve_file(Manager *m, const char *path, GError **error)
{
    char *canonical = realpath(path, NULL);
    char *prefix = g_strconcat(m->home, "/", NULL);
    gboolean safe = canonical && !strcmp(canonical, path) && g_str_has_prefix(canonical, prefix)
        && !strstr(canonical + strlen(prefix), "/.") && canonical[strlen(prefix)] != '.';
    free(canonical); g_free(prefix);
    if (!safe) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Only shared files in the session user's storage can be opened");
        return NULL;
    }
    char *type = appd_detect_type(path, error);
    if (!type) return NULL;
    GVariant *associations = g_variant_ref_sink(appd_file_associations(m));
    GVariant *defaults = g_variant_get_child_value(associations, 0), *handlers = g_variant_get_child_value(associations, 1);
    const char *chosen = "";
    g_variant_lookup(defaults, type, "&s", &chosen);
    if (builtin_handles(type, APPD_PACKAGE_INSTALLER)) chosen = APPD_PACKAGE_INSTALLER;
    if (builtin_handles(type, APPD_ANDROID_INSTALLER)) chosen = APPD_ANDROID_INSTALLER;
    GVariant *ids = g_variant_lookup_value(handlers, type, G_VARIANT_TYPE("as"));
    GVariant *result = g_variant_new("(ss@as)", type, chosen, ids ? ids : g_variant_new_strv(NULL, 0));
    g_variant_unref(defaults); g_variant_unref(handlers); g_variant_unref(associations); g_free(type);
    return result;
}
