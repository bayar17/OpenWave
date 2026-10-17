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
    gint limit = g_key_file_get_integer(config, "Isolation", "MaxProcesses", NULL);
    m->pids_max = limit ? CLAMP(limit, 64, 65536) : 512;
    m->state_dir = g_strdup("/run/openwave/appd");
    m->launcher_exe = g_strdup("/System/Applications/com.openwave.shell/bin/ocean-launcher");
    g_key_file_unref(config);
    return TRUE;
}

static void app_free(gpointer data)
{
    App *a = data;
    g_free(a->id); g_free(a->name); g_free(a->icon); g_free(a->manifest);
    g_free(a->bundle); g_free(a->digest); g_strfreev(a->permissions); g_strfreev(a->argv); g_free(a->state); g_free(a->error); g_free(a);
}

gboolean appd_validate_bundle(App *app, GError **error)
{
    App *fresh = appd_read_bundle(app->bundle);
    gboolean valid = fresh && !g_strcmp0(fresh->digest, app->digest)
        && !g_strcmp0(fresh->argv[0], app->argv[0]);
    if (fresh) app_free(fresh);
    if (!valid) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
        "Bundle changed or failed validation; refresh the application registry");
    return valid;
}

void appd_registry_scan(Manager *m)
{
    gboolean changed = FALSE;
    if (!m->apps) m->apps = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, app_free);
    GHashTable *found = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *candidates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, app_free);
    const char *roots[] = {"/System/Applications", "/Applications", NULL};
    for (guint i = 0; roots[i]; ++i) {
        GDir *directory = g_dir_open(roots[i], 0, NULL);
        const char *name;
        while (directory && (name = g_dir_read_name(directory))) {
            char *path = g_build_filename(roots[i], name, NULL);
            App *next = appd_read_bundle(path); g_free(path);
            if (!next) continue;
            if (g_hash_table_contains(found, next->id)) {
                g_warning("appd: rejecting duplicate bundle ID %s", next->id);
                g_hash_table_remove(candidates, next->id); app_free(next); continue;
            }
            g_hash_table_add(found, g_strdup(next->id));
            g_hash_table_insert(candidates, g_strdup(next->id), next);
        }
        if (directory) g_dir_close(directory);
    }
    g_hash_table_remove_all(found);
    GHashTableIter candidates_iter; gpointer candidate, candidate_key;
    g_hash_table_iter_init(&candidates_iter, candidates);
    while (g_hash_table_iter_next(&candidates_iter, &candidate_key, &candidate)) {
        App *next = candidate;
        g_hash_table_add(found, g_strdup(next->id));
        g_hash_table_iter_steal(&candidates_iter); g_free(candidate_key);
        App *old = g_hash_table_lookup(m->apps, next->id);
        if (old && (old->removing || appd_instance(m, old))) { app_free(next); continue; }
        changed |= !old || g_strcmp0(old->name, next->name) || g_strcmp0(old->icon, next->icon)
            || !g_strv_equal((const char *const *)old->argv, (const char *const *)next->argv)
            || old->paths != next->paths || old->single != next->single || g_strcmp0(old->digest, next->digest);
        if (old) {
            g_free(next->state); next->state = g_strdup(old->state);
            g_free(next->error); next->error = g_strdup(old->error);
        }
        if (!old || g_strcmp0(old->digest, next->digest)) appd_services_event(m, "ApplicationRegistered", next, NULL);
        g_hash_table_replace(m->apps, next->id, next);
    }
    GHashTableIter iter; gpointer key, value;
    g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, &key, &value))
        if (!g_hash_table_contains(found, key) && !((App *)value)->removing && !appd_instance(m, value)) {
            appd_services_event(m, "ApplicationRemoved", value, NULL);
            g_hash_table_iter_remove(&iter); changed = TRUE;
        }
    g_hash_table_unref(found); g_hash_table_unref(candidates);
    if (changed) appd_emit(m, "RegistryChanged", NULL);
}

static gboolean rescan(gpointer data)
{
    Manager *m = data; m->scan_timer = 0; appd_registry_scan(m); return G_SOURCE_REMOVE;
}
static gboolean poll_registry(gpointer data) { appd_registry_scan(data); return G_SOURCE_CONTINUE; }
static void changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event, gpointer data)
{
    (void)monitor; (void)file; (void)other; (void)event;
    Manager *m = data;
    if (!m->scan_timer) m->scan_timer = g_timeout_add(100, rescan, m);
}
void appd_registry_watch(Manager *m)
{
    const char *roots[] = {"/System/Applications", "/Applications", NULL};
    m->monitors = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = 0; roots[i]; ++i) {
        GFile *directory = g_file_new_for_path(roots[i]);
        GFileMonitor *monitor = g_file_monitor_directory(directory, 0, NULL, NULL);
        g_object_unref(directory);
        if (monitor) { g_signal_connect(monitor, "changed", G_CALLBACK(changed), m); g_ptr_array_add(m->monitors, monitor); }
    }
    // Nested manifest updates are not reported by every filesystem watcher.
    g_timeout_add_seconds(2, poll_registry, m);
}

GVariant *appd_info(Manager *m, App *a, Instance *instance)
{
    if (!instance) instance = appd_instance(m, a);
    GVariantBuilder info;
    g_variant_builder_init(&info, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&info, "{sv}", "appId", g_variant_new_string(a->id));
    g_variant_builder_add(&info, "{sv}", "manifestDigest", g_variant_new_string(a->digest));
    g_variant_builder_add(&info, "{sv}", "declaredPermissions", g_variant_new_strv((const char *const *)a->permissions, -1));
    g_variant_builder_add(&info, "{sv}", "name", g_variant_new_string(a->name));
    g_variant_builder_add(&info, "{sv}", "icon", g_variant_new_string(a->icon));
    g_variant_builder_add(&info, "{sv}", "bundlePath", g_variant_new_string(a->bundle));
    g_variant_builder_add(&info, "{sv}", "isolationProfile", g_variant_new_string(appd_trusted_app(a) ? "trusted-system" : "bubblewrap-landlock-uid-cgroup"));
    g_variant_builder_add(&info, "{sv}", "namespaceSandboxed", g_variant_new_boolean(instance && instance->namespace_sandboxed));
    g_variant_builder_add(&info, "{sv}", "seccompFiltered", g_variant_new_boolean(instance && !appd_trusted_app(a)));
    g_variant_builder_add(&info, "{sv}", "executable", g_variant_new_string(a->argv[0]));
    g_variant_builder_add(&info, "{sv}", "state", g_variant_new_string(a->state));
    g_variant_builder_add(&info, "{sv}", "error", g_variant_new_string(a->error));
    g_variant_builder_add(&info, "{sv}", "pid", g_variant_new_uint32(instance ? instance->pid : 0));
    g_variant_builder_add(&info, "{sv}", "trusted", g_variant_new_boolean(appd_trusted_app(a)));
    g_variant_builder_add(&info, "{sv}", "canUninstall", g_variant_new_boolean(!a->removing && appd_is_webapp(a)));
    g_variant_builder_add(&info, "{sv}", "singleInstance", g_variant_new_boolean(a->single));
    return g_variant_builder_end(&info);
}
