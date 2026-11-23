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
#define LEGACY_GROUP "Defaults"
#define SCHEME_PREFIX "x-scheme-handler/"

static const struct { const char *extension; const char *handler; } builtin[] = {
    {"application/x-rpm", APPD_PACKAGE_INSTALLER},
    {"application/vnd.android.package-archive", APPD_ANDROID_INSTALLER},
};

typedef struct {
    const char *id;
    const char *name;
    const char *types[8];
} Category;

static const Category categories[] = {
    {"web-browser", "Web browser", {"x-scheme-handler/http", "x-scheme-handler/https", "text/html", NULL}},
    {"email", "Email", {"x-scheme-handler/mailto", NULL}},
    {"phone", "Phone", {"x-scheme-handler/tel", NULL}},
    {"messaging", "Messaging", {"x-scheme-handler/sms", NULL}},
    {"photos", "Photos and images", {"image/png", "image/jpeg", "image/webp", "image/gif", "image/svg+xml", NULL}},
    {"music", "Music", {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4", NULL}},
    {"video", "Videos", {"video/mp4", "video/x-matroska", "video/webm", NULL}},
    {"documents", "Documents", {"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "application/vnd.oasis.opendocument.text", "text/plain", NULL}},
    {"pdf", "PDF viewer", {"application/pdf", NULL}},
};

static gboolean builtin_handles(const char *type, const char *id)
{
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i)
        if (!strcmp(builtin[i].extension, type) && !strcmp(builtin[i].handler, id)) return TRUE;
    return FALSE;
}

static gboolean protected_type(const char *type)
{
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i)
        if (!strcmp(builtin[i].extension, type)) return TRUE;
    return FALSE;
}

static gboolean declares(App *app, const char *key)
{
    for (int list = 0; list < 2; ++list)
        for (char **opened = list ? app->mime_types : app->opens; opened && *opened; ++opened) {
            char *canonical = appd_type_key(*opened);
            gboolean same = canonical && !strcmp(canonical, key);
            g_free(canonical);
            if (same) return TRUE;
        }
    if (g_str_has_prefix(key, SCHEME_PREFIX))
        for (char **scheme = app->schemes; scheme && *scheme; ++scheme)
            if (!strcmp(*scheme, key + strlen(SCHEME_PREFIX))) return TRUE;
    return FALSE;
}

static gboolean eligible(App *app, const char *key)
{
    if (!app || app->removing || app->linux_app || !app->desktop_id || !declares(app, key)) return FALSE;
    return app->android || app->paths;
}

static gboolean eligible_or_parent(App *app, const char *key)
{
    if (eligible(app, key)) return TRUE;
    char **parents = appd_mime_parents(key);
    gboolean parent = FALSE;
    for (char **item = parents; parents && *item && !parent; ++item) parent = eligible(app, *item);
    g_strfreev(parents);
    return parent;
}

gboolean appd_handler_valid(Manager *m, const char *type, const char *id)
{
    char *key = appd_type_key(type);
    if (!key) return FALSE;
    gboolean valid;
    if (protected_type(key)) valid = builtin_handles(key, id);
    else valid = eligible_or_parent(appd_lookup(m, id), key);
    g_free(key);
    return valid;
}

static void each_app(Manager *m, void (*visit)(App *app, gpointer data), gpointer data)
{
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, NULL, &value)) visit(value, data);
    if (m->android_apps) {
        g_hash_table_iter_init(&iter, m->android_apps);
        while (g_hash_table_iter_next(&iter, NULL, &value)) visit(value, data);
    }
}

typedef struct { const char *key; GPtrArray *found; } Collect;

static void collect_eligible(App *app, gpointer data)
{
    Collect *collect = data;
    if (eligible(app, collect->key)) g_ptr_array_add(collect->found, app);
}

static gint compare_apps(gconstpointer a, gconstpointer b)
{
    return g_utf8_collate((*(App *const *)a)->name, (*(App *const *)b)->name);
}

static GPtrArray *eligible_apps(Manager *m, const char *key)
{
    Collect collect = {key, g_ptr_array_new()};
    each_app(m, collect_eligible, &collect);
    if (!collect.found->len) {
        char **parents = appd_mime_parents(key);
        for (char **parent = parents; parents && *parent && !collect.found->len; ++parent) {
            collect.key = *parent;
            each_app(m, collect_eligible, &collect);
        }
        g_strfreev(parents);
    }
    g_ptr_array_sort(collect.found, compare_apps);
    return collect.found;
}

static MimeApps *load_associations(Manager *m)
{
    return mimeapps_load(m->desktop);
}

typedef struct { Manager *m; const char *key; } Usable;

static gboolean usable(const char *desktop_id, gpointer data)
{
    Usable *context = data;
    App *app = appd_app_for_desktop_id(context->m, desktop_id);
    return app && eligible_or_parent(app, context->key);
}

static const char *effective(Manager *m, MimeApps *apps, const char *key, char **desktop_out, const char **state)
{
    Usable context = {m, key};
    char **ids = mimeapps_default_ids(apps, key);
    char *chosen = mimeapps_default(apps, key, usable, &context);
    App *app = chosen ? appd_app_for_desktop_id(m, chosen) : NULL;
    *state = app ? "set" : (*ids ? "unavailable" : "unset");
    if (desktop_out) *desktop_out = chosen; else g_free(chosen);
    g_strfreev(ids);
    return app ? app->id : "";
}

static void add_declared(App *app, gpointer data)
{
    GHashTable *types = data;
    if (app->removing || app->linux_app || !app->desktop_id || !(app->android || app->paths)) return;
    for (int list = 0; list < 2; ++list)
        for (char **opened = list ? app->mime_types : app->opens; opened && *opened; ++opened) {
            char *key = appd_type_key(*opened);
            if (key) g_hash_table_add(types, key);
        }
    for (char **scheme = app->schemes; scheme && *scheme; ++scheme)
        g_hash_table_add(types, g_strconcat(SCHEME_PREFIX, *scheme, NULL));
}

GVariant *appd_file_associations(Manager *m)
{
    GVariantBuilder defaults, handlers;
    g_variant_builder_init(&defaults, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_init(&handlers, G_VARIANT_TYPE("a{sas}"));
    GHashTable *types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i) g_hash_table_add(types, g_strdup(builtin[i].extension));
    each_app(m, add_declared, types);
    MimeApps *apps = load_associations(m);
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, types);
    while (g_hash_table_iter_next(&iter, &value, NULL)) {
        const char *key = value;
        GPtrArray *ids = g_ptr_array_new();
        for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i)
            if (!strcmp(builtin[i].extension, key)) g_ptr_array_add(ids, (gpointer)builtin[i].handler);
        GPtrArray *apps_for = eligible_apps(m, key);
        for (guint i = 0; i < apps_for->len; ++i) g_ptr_array_add(ids, ((App *)g_ptr_array_index(apps_for, i))->id);
        g_ptr_array_add(ids, NULL);
        g_variant_builder_add(&handlers, "{s@as}", key, g_variant_new_strv((const char *const *)ids->pdata, (gssize)ids->len - 1));
        g_ptr_array_free(ids, TRUE); g_ptr_array_unref(apps_for);
        if (!protected_type(key)) {
            const char *state;
            const char *chosen = effective(m, apps, key, NULL, &state);
            if (*chosen) g_variant_builder_add(&defaults, "{ss}", key, chosen);
        }
    }
    mimeapps_free(apps);
    g_hash_table_unref(types);
    return g_variant_new("(@a{ss}@a{sas})", g_variant_builder_end(&defaults), g_variant_builder_end(&handlers));
}

static void note_digest(Manager *m)
{
    MimeApps *apps = load_associations(m);
    g_free(m->mimeapps_digest);
    m->mimeapps_digest = g_strdup(mimeapps_digest(apps));
    mimeapps_free(apps);
}

gboolean appd_set_file_default(Manager *m, const char *type, const char *id, GError **error)
{
    char *key = appd_type_key(type);
    App *app = *id ? appd_lookup(m, id) : NULL;
    if (!key || protected_type(key) || (*id && !eligible(app, key))) {
        g_free(key);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid type, incompatible application, or protected package type");
        return FALSE;
    }
    gboolean saved = *id ? mimeapps_set_default(m->desktop, key, app->desktop_id, error) : mimeapps_clear_default(m->desktop, key, error);
    g_free(key);
    if (saved) { note_digest(m); appd_emit(m, "FileDefaultsChanged", NULL); }
    return saved;
}

gboolean appd_clear_file_default(Manager *m, const char *type, GError **error)
{
    return appd_set_file_default(m, type, "", error);
}

gboolean appd_clear_file_defaults(Manager *m, const char *id, GError **error)
{
    App *app = appd_lookup(m, id);
    if (!app || !app->desktop_id) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Application has no desktop entry");
        return FALSE;
    }
    gboolean saved = mimeapps_clear_application(m->desktop, app->desktop_id, error);
    if (saved) { note_digest(m); appd_emit(m, "FileDefaultsChanged", NULL); }
    return saved;
}

GVariant *appd_default_file_handler(Manager *m, const char *type, GError **error)
{
    char *key = appd_type_key(type);
    if (!key) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid type");
        return NULL;
    }
    const char *chosen = "", *state = "unset";
    MimeApps *apps = load_associations(m);
    if (protected_type(key)) { chosen = !strcmp(key, "application/x-rpm") ? APPD_PACKAGE_INSTALLER : APPD_ANDROID_INSTALLER; state = "protected"; }
    else chosen = effective(m, apps, key, NULL, &state);
    GVariant *result = g_variant_new("(ss)", chosen, state);
    mimeapps_free(apps);
    g_free(key);
    return result;
}

static gint compare_strings(gconstpointer a, gconstpointer b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

GVariant *appd_supported_types(Manager *m, const char *id, GError **error)
{
    App *app = appd_lookup(m, id);
    if (!app) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Application is not installed");
        return NULL;
    }
    GHashTable *types = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (int list = 0; list < 2; ++list)
        for (char **opened = list ? app->mime_types : app->opens; opened && *opened; ++opened) {
            char *key = appd_type_key(*opened);
            if (key) g_hash_table_add(types, key);
        }
    GPtrArray *names = g_ptr_array_new();
    GHashTableIter iter; gpointer key;
    g_hash_table_iter_init(&iter, types);
    while (g_hash_table_iter_next(&iter, &key, NULL)) g_ptr_array_add(names, key);
    g_ptr_array_sort(names, compare_strings);
    g_ptr_array_add(names, NULL);
    GVariant *result = g_variant_new("(@as@as)", g_variant_new_strv((const char *const *)names->pdata, (gssize)names->len - 1),
        g_variant_new_strv((const char *const *)(app->schemes ? app->schemes : (char *[]){NULL}), -1));
    g_ptr_array_free(names, TRUE);
    g_hash_table_unref(types);
    return result;
}

static const Category *category_named(const char *name)
{
    for (guint i = 0; i < G_N_ELEMENTS(categories); ++i)
        if (!strcmp(categories[i].id, name)) return &categories[i];
    return NULL;
}

GVariant *appd_default_categories(Manager *m)
{
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    MimeApps *apps = load_associations(m);
    for (guint i = 0; i < G_N_ELEMENTS(categories); ++i) {
        const Category *category = &categories[i];
        GPtrArray *handlers = eligible_apps(m, category->types[0]);
        if (!handlers->len) { g_ptr_array_unref(handlers); continue; }
        GPtrArray *ids = g_ptr_array_new();
        for (guint h = 0; h < handlers->len; ++h) g_ptr_array_add(ids, ((App *)g_ptr_array_index(handlers, h))->id);
        const char *state;
        const char *chosen = effective(m, apps, category->types[0], NULL, &state);
        GVariantBuilder item;
        g_variant_builder_init(&item, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&item, "{sv}", "id", g_variant_new_string(category->id));
        g_variant_builder_add(&item, "{sv}", "name", g_variant_new_string(category->name));
        g_variant_builder_add(&item, "{sv}", "types", g_variant_new_strv(category->types, -1));
        g_variant_builder_add(&item, "{sv}", "handlers", g_variant_new_strv((const char *const *)ids->pdata, ids->len));
        g_variant_builder_add(&item, "{sv}", "default", g_variant_new_string(chosen));
        g_variant_builder_add(&item, "{sv}", "state", g_variant_new_string(state));
        g_variant_builder_add_value(&list, g_variant_builder_end(&item));
        g_ptr_array_free(ids, TRUE); g_ptr_array_unref(handlers);
    }
    mimeapps_free(apps);
    return g_variant_new("(@aa{sv})", g_variant_builder_end(&list));
}

GVariant *appd_get_default_category(Manager *m, const char *name, GError **error)
{
    const Category *category = category_named(name);
    if (!category) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown default application category");
        return NULL;
    }
    return appd_default_file_handler(m, category->types[0], error);
}

gboolean appd_set_default_category(Manager *m, const char *name, const char *id, GError **error)
{
    const Category *category = category_named(name);
    App *app = *id ? appd_lookup(m, id) : NULL;
    if (!category || (*id && !eligible(app, category->types[0]))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown category or an application that cannot handle it");
        return FALSE;
    }
    gboolean saved = TRUE;
    for (guint i = 0; saved && category->types[i]; ++i) {
        if (!*id) saved = mimeapps_clear_default(m->desktop, category->types[i], error);
        else if (eligible(app, category->types[i])) saved = mimeapps_set_default(m->desktop, category->types[i], app->desktop_id, error);
    }
    note_digest(m);
    appd_emit(m, "FileDefaultsChanged", NULL);
    return saved;
}

static void migrate_legacy(Manager *m, const char *legacy)
{
    struct stat st;
    if (lstat(legacy, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size > 65536) return;
    GKeyFile *old = g_key_file_new();
    if (!g_key_file_load_from_file(old, legacy, G_KEY_FILE_NONE, NULL)) { g_key_file_free(old); return; }
    char **keys = g_key_file_get_keys(old, LEGACY_GROUP, NULL, NULL);
    MimeApps *apps = load_associations(m);
    gboolean changed = FALSE;
    for (char **key = keys; key && *key; ++key) {
        char *chosen = g_key_file_get_string(old, LEGACY_GROUP, *key, NULL);
        char *type = appd_type_key(*key);
        App *app = chosen ? appd_lookup(m, chosen) : NULL;
        gboolean consumed = !type || !chosen || protected_type(type);
        if (!consumed && app && !eligible(app, type)) consumed = TRUE;
        else if (!consumed && app) {
            char **existing = mimeapps_default_ids(apps, type);
            gboolean has = *existing != NULL;
            g_strfreev(existing);
            consumed = has || mimeapps_set_default(m->desktop, type, app->desktop_id, NULL);
        }
        if (consumed) { g_key_file_remove_key(old, LEGACY_GROUP, *key, NULL); changed = TRUE; }
        g_free(type); g_free(chosen);
    }
    g_strfreev(keys);
    mimeapps_free(apps);
    if (changed) {
        char **remaining = g_key_file_get_keys(old, LEGACY_GROUP, NULL, NULL);
        gboolean empty = !remaining || !*remaining;
        g_strfreev(remaining);
        if (empty) {
            char *archive = g_strconcat(legacy, ".migrated", NULL);
            if (rename(legacy, archive)) g_warning("appd: could not archive migrated file defaults");
            g_free(archive);
        } else {
            gsize length;
            char *data = g_key_file_to_data(old, &length, NULL);
            GError *error = NULL;
            if (!g_file_set_contents_full(legacy, data, (gssize)length, G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, &error)) {
                g_warning("appd: could not update legacy file defaults: %s", error->message);
                g_clear_error(&error);
            }
            g_free(data);
        }
    }
    g_key_file_free(old);
}

void appd_defaults_refresh(Manager *m)
{
    char *per_user = g_strdup_printf(APPD_DEFAULTS_DIRECTORY "/file-defaults-%u.ini", m->uid);
    migrate_legacy(m, per_user);
    migrate_legacy(m, APPD_DEFAULTS_DIRECTORY "/file-defaults.ini");
    g_free(per_user);
    MimeApps *apps = load_associations(m);
    gboolean changed = m->defaults_ready && g_strcmp0(m->mimeapps_digest, mimeapps_digest(apps));
    g_free(m->mimeapps_digest);
    m->mimeapps_digest = g_strdup(mimeapps_digest(apps));
    m->defaults_ready = TRUE;
    mimeapps_free(apps);
    if (changed) appd_emit(m, "FileDefaultsChanged", NULL);
}

void appd_prune_file_defaults(Manager *m)
{
    appd_defaults_refresh(m);
}

GVariant *appd_resolve_uri(Manager *m, const char *uri, GError **error)
{
    char *scheme = g_uri_parse_scheme(uri);
    if (!scheme || strlen(uri) > 4096) {
        g_free(scheme);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A URI with a scheme is required");
        return NULL;
    }
    char *lower = g_ascii_strdown(scheme, -1);
    char *type = g_strconcat(SCHEME_PREFIX, lower, NULL);
    g_free(scheme); g_free(lower);
    char *key = appd_type_key(type);
    g_free(type);
    if (!key) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid URI scheme");
        return NULL;
    }
    MimeApps *apps = load_associations(m);
    const char *state;
    const char *chosen = effective(m, apps, key, NULL, &state);
    GPtrArray *handlers = eligible_apps(m, key);
    GPtrArray *ids = g_ptr_array_new();
    for (guint i = 0; i < handlers->len; ++i) g_ptr_array_add(ids, ((App *)g_ptr_array_index(handlers, i))->id);
    GVariant *result = g_variant_new("(ss@as)", key, chosen, g_variant_new_strv((const char *const *)ids->pdata, ids->len));
    g_ptr_array_free(ids, TRUE); g_ptr_array_unref(handlers); mimeapps_free(apps); g_free(key);
    return result;
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
    const char *chosen = "";
    const char *state;
    MimeApps *apps = load_associations(m);
    if (!protected_type(type)) chosen = effective(m, apps, type, NULL, &state);
    if (builtin_handles(type, APPD_PACKAGE_INSTALLER)) chosen = APPD_PACKAGE_INSTALLER;
    if (builtin_handles(type, APPD_ANDROID_INSTALLER)) chosen = APPD_ANDROID_INSTALLER;
    GPtrArray *handlers = eligible_apps(m, type);
    GPtrArray *ids = g_ptr_array_new();
    for (guint i = 0; i < G_N_ELEMENTS(builtin); ++i)
        if (!strcmp(builtin[i].extension, type)) g_ptr_array_add(ids, (gpointer)builtin[i].handler);
    for (guint i = 0; i < handlers->len; ++i) g_ptr_array_add(ids, ((App *)g_ptr_array_index(handlers, i))->id);
    GVariant *result = g_variant_new("(ss@as)", type, chosen, g_variant_new_strv((const char *const *)ids->pdata, ids->len));
    g_ptr_array_free(ids, TRUE); g_ptr_array_unref(handlers); mimeapps_free(apps); g_free(type);
    return result;
}
