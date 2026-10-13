#define _GNU_SOURCE
#include "appd.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

gboolean appd_verified_file(const char *path, gboolean executable, GError **error)
{
    if (!path || path[0] != '/') {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "An absolute file path is required");
        return FALSE;
    }
    char *canonical = realpath(path, NULL);
    struct stat st;
    gboolean okay = canonical && stat(canonical, &st) == 0 && S_ISREG(st.st_mode)
        && st.st_uid == 0 && !(st.st_mode & 0022) && (!executable || (st.st_mode & 0111));
    char *parent = canonical ? g_path_get_dirname(canonical) : NULL;
    while (okay && parent) {
        okay = stat(parent, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022);
        if (!strcmp(parent, "/")) break;
        char *next = g_path_get_dirname(parent);
        g_free(parent);
        parent = next;
    }
    g_free(parent);
    free(canonical);
    if (!okay) g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
        "Untrusted or unavailable %s: %s", executable ? "executable" : "metadata", path);
    return okay;
}

static gboolean metadata_file(const char *path, GError **error)
{
    if (!appd_verified_file(path, FALSE, error)) return FALSE;
    struct stat st;
    if (stat(path, &st) || st.st_size > 65536) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Application metadata exceeds the 64 KiB limit");
        return FALSE;
    }
    return TRUE;
}

gboolean appd_configuration(Manager *m, const char *path, GError **error)
{
    GKeyFile *config = g_key_file_new();
    if (!metadata_file(path, error) || !g_key_file_load_from_file(config, path, 0, error)) {
        g_key_file_unref(config);
        return FALSE;
    }
    m->user = g_key_file_get_string(config, "Execution", "User", error);
    struct passwd *user = m->user ? getpwnam(m->user) : NULL;
    if (!user) {
        if (!error || !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Execution user is not configured");
        g_key_file_unref(config);
        return FALSE;
    }
    m->uid = user->pw_uid;
    m->gid = user->pw_gid;
    m->home = g_strdup_printf("/Data/Users/%s", user->pw_name);
    m->runtime = g_key_file_get_string(config, "Execution", "RuntimeDirectory", NULL);
    if (!m->runtime) m->runtime = g_strdup("/run/ocean");
    gint deadline = g_key_file_get_integer(config, "Execution", "ShutdownTimeoutMs", NULL);
    m->shutdown_ms = deadline ? CLAMP(deadline, 100, 30000) : 2500;
    m->desktop_dir = g_strdup("/usr/share/applications");
    m->state_dir = g_strdup("/run/openwave/appd");
    m->launcher_exe = g_strdup("/usr/bin/ocean-launcher");
    g_key_file_unref(config);
    return TRUE;
}

static gboolean valid_id(const char *id)
{
    if (!id || !*id || strlen(id) > 128 || !g_ascii_isalnum(*id)) return FALSE;
    for (const char *p = id; *p; ++p)
        if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_' && *p != '.') return FALSE;
    return TRUE;
}

static void app_free(gpointer data)
{
    App *a = data;
    g_free(a->id); g_free(a->name); g_free(a->icon); g_free(a->desktop);
    g_strfreev(a->argv); g_free(a->state); g_free(a->error); g_free(a);
}

static App *read_app(const char *path)
{
    GError *error = NULL;
    if (!metadata_file(path, &error)) {
        g_warning("appd: %s", error->message); g_clear_error(&error); return NULL;
    }
    GKeyFile *file = g_key_file_new();
    App *a = g_new0(App, 1);
    char *exec = NULL, *type = NULL, *icon = NULL;
    if (!g_key_file_load_from_file(file, path, 0, NULL)) goto invalid;
    type = g_key_file_get_string(file, "Desktop Entry", "Type", NULL);
    if (g_strcmp0(type, "Application") || g_key_file_get_boolean(file, "Desktop Entry", "Hidden", NULL)
        || g_key_file_get_boolean(file, "Desktop Entry", "Terminal", NULL)) goto invalid;
    a->id = g_key_file_get_string(file, "Desktop Entry", "X-OpenWave-AppId", NULL);
    if (!valid_id(a->id)) goto invalid;
    a->name = g_key_file_get_locale_string(file, "Desktop Entry", "Name", NULL, NULL);
    if (!a->name || !*a->name) goto invalid;
    exec = g_key_file_get_string(file, "Desktop Entry", "Exec", NULL);
    if (!exec || !g_shell_parse_argv(exec, NULL, &a->argv, NULL) || !a->argv[0]
        || !appd_verified_file(a->argv[0], TRUE, NULL)) goto invalid;
    char *resolved = realpath(a->argv[0], NULL);
    g_free(a->argv[0]); a->argv[0] = resolved;
    if (!g_str_has_prefix(a->argv[0], "/usr/bin/") && !g_str_has_prefix(a->argv[0], "/usr/libexec/")) goto invalid;
    GPtrArray *fixed = g_ptr_array_new_with_free_func(g_free);
    for (char **p = a->argv; *p; ++p) {
        if (!strcmp(*p, "%f") || !strcmp(*p, "%F") || !strcmp(*p, "%u") || !strcmp(*p, "%U")) {
            a->paths = TRUE;
            continue;
        }
        if (strchr(*p, '%')) { g_ptr_array_unref(fixed); goto invalid; }
        g_ptr_array_add(fixed, g_strdup(*p));
    }
    g_ptr_array_add(fixed, NULL);
    g_strfreev(a->argv); a->argv = (char **)g_ptr_array_free(fixed, FALSE);
    a->paths |= g_key_file_get_boolean(file, "Desktop Entry", "X-OpenWave-AcceptsPaths", NULL);
    a->single = !g_key_file_has_key(file, "Desktop Entry", "X-OpenWave-SingleInstance", NULL)
        || g_key_file_get_boolean(file, "Desktop Entry", "X-OpenWave-SingleInstance", NULL);
    icon = g_key_file_get_string(file, "Desktop Entry", "Icon", NULL);
    if (icon && !strchr(icon, '/')) {
        const char *extensions[] = {"", ".svg", ".png", ".jpg", NULL};
        for (int i = 0; extensions[i]; ++i) {
            char *name = g_strconcat(icon, extensions[i], NULL);
            char *candidate = g_build_filename("/usr/share/pixmaps", name, NULL);
            g_free(name);
            if (appd_verified_file(candidate, FALSE, NULL)) { a->icon = g_filename_to_uri(candidate, NULL, NULL); g_free(candidate); break; }
            g_free(candidate);
        }
    } else if (icon && appd_verified_file(icon, FALSE, NULL)) a->icon = g_filename_to_uri(icon, NULL, NULL);
    if (!a->icon) a->icon = g_strdup("");
    a->desktop = g_strdup(path);
    a->state = g_strdup("Stopped"); a->error = g_strdup("");
    g_free(type); g_free(exec); g_free(icon); g_key_file_unref(file);
    return a;
invalid:
    g_free(type); g_free(exec); g_free(icon); g_key_file_unref(file); app_free(a);
    return NULL;
}

void appd_registry_scan(Manager *m)
{
    gboolean changed = FALSE;
    if (!m->apps) m->apps = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, app_free);
    GDir *directory = g_dir_open(m->desktop_dir, 0, NULL);
    if (!directory) return;
    GHashTable *found = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *name;
    while ((name = g_dir_read_name(directory))) {
        if (!g_str_has_suffix(name, ".desktop")) continue;
        char *path = g_build_filename(m->desktop_dir, name, NULL);
        App *next = read_app(path); g_free(path);
        if (!next) continue;
        if (g_hash_table_contains(found, next->id)) { g_warning("appd: duplicate application ID %s", next->id); app_free(next); continue; }
        g_hash_table_add(found, g_strdup(next->id));
        App *old = g_hash_table_lookup(m->apps, next->id);
        if (old && appd_instance(m, old)) { app_free(next); continue; }
        changed |= !old || g_strcmp0(old->name, next->name) || g_strcmp0(old->icon, next->icon)
            || !g_strv_equal((const char *const *)old->argv, (const char *const *)next->argv)
            || old->paths != next->paths || old->single != next->single;
        if (old) {
            g_free(next->state); next->state = g_strdup(old->state);
            g_free(next->error); next->error = g_strdup(old->error);
        }
        g_hash_table_replace(m->apps, next->id, next);
    }
    GHashTableIter iter; gpointer key, value;
    g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, &key, &value))
        if (!g_hash_table_contains(found, key) && !appd_instance(m, value)) { g_hash_table_iter_remove(&iter); changed = TRUE; }
    g_hash_table_unref(found); g_dir_close(directory);
    if (changed) appd_emit(m, "RegistryChanged", NULL);
}

static gboolean rescan(gpointer data)
{
    Manager *m = data; m->scan_timer = 0; appd_registry_scan(m); return G_SOURCE_REMOVE;
}
static void changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer data)
{
    (void)monitor; (void)file; (void)other; (void)event;
    Manager *m = data;
    if (!m->scan_timer) m->scan_timer = g_timeout_add(100, rescan, m);
}
void appd_registry_watch(Manager *m)
{
    GFile *directory = g_file_new_for_path(m->desktop_dir);
    m->monitor = g_file_monitor_directory(directory, 0, NULL, NULL);
    g_object_unref(directory);
    if (m->monitor) g_signal_connect(m->monitor, "changed", G_CALLBACK(changed), m);
}

GVariant *appd_info(Manager *m, App *a, Instance *instance)
{
    if (!instance) instance = appd_instance(m, a);
    GVariantBuilder info;
    g_variant_builder_init(&info, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&info, "{sv}", "appId", g_variant_new_string(a->id));
    g_variant_builder_add(&info, "{sv}", "name", g_variant_new_string(a->name));
    g_variant_builder_add(&info, "{sv}", "icon", g_variant_new_string(a->icon));
    g_variant_builder_add(&info, "{sv}", "executable", g_variant_new_string(a->argv[0]));
    g_variant_builder_add(&info, "{sv}", "state", g_variant_new_string(a->state));
    g_variant_builder_add(&info, "{sv}", "error", g_variant_new_string(a->error));
    g_variant_builder_add(&info, "{sv}", "pid", g_variant_new_uint32(instance ? instance->pid : 0));
    g_variant_builder_add(&info, "{sv}", "singleInstance", g_variant_new_boolean(a->single));
    return g_variant_builder_end(&info);
}
