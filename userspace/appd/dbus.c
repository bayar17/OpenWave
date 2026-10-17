#include "appd.h"
#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { char *app_id; char **arguments; gint64 created; } Pending;
static void pending_free(gpointer data)
{
    Pending *p = data; g_free(p->app_id); g_strfreev(p->arguments); g_free(p);
}

gboolean appd_credentials(Manager *m, const char *sender, guint *uid, guint *pid)
{
    GVariant *reply = g_dbus_connection_call_sync(m->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionCredentials", g_variant_new("(s)", sender),
        G_VARIANT_TYPE("(a{sv})"), 0, 1000, NULL, NULL);
    if (!reply) return FALSE;
    GVariant *credentials = g_variant_get_child_value(reply, 0);
    gboolean okay = g_variant_lookup(credentials, "UnixUserID", "u", uid)
        && g_variant_lookup(credentials, "ProcessID", "u", pid);
    g_variant_unref(credentials); g_variant_unref(reply); return okay;
}
static void failure(GDBusMethodInvocation *call, const char *message)
{
    g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error", message);
}
static gboolean trusted_renderer(Manager *m, guint uid, guint pid)
{
    char *contents = NULL;
    char *path = g_strdup_printf("/proc/%u/exe", pid);
    char *exe = g_file_read_link(path, NULL); g_free(path);
    gboolean okay = uid == m->uid && g_strcmp0(exe, m->launcher_exe) == 0
        && g_file_get_contents("/run/openwave/ocean.pid", &contents, NULL, NULL)
        && g_ascii_strtoull(contents, NULL, 10) == pid;
    g_free(exe); g_free(contents); return okay;
}
static void method(GDBusConnection *connection, const char *sender, const char *path,
    const char *interface, const char *member, GVariant *parameters, GDBusMethodInvocation *call, gpointer data)
{
    (void)connection; (void)path; (void)interface;
    Manager *m = data; guint uid = 0, pid = 0;
    if (!appd_credentials(m, sender, &uid, &pid)) {
        failure(call, "Application session credentials are required"); return;
    }
    Instance *caller = appd_caller(m, uid, pid);
    if (!strcmp(member, "UninstallWebApp")) {
        const char *id; g_variant_get(parameters, "(&s)", &id);
        appd_uninstall_webapp(m, caller, pid, id, call); return;
    }
    if (!strcmp(member, "UpdateWebAppIcon")) {
        GVariant *bytes; gsize size; GError *error = NULL;
        g_variant_get(parameters, "(@ay)", &bytes);
        const guint8 *icon = g_variant_get_fixed_array(bytes, &size, 1);
        if (appd_update_webapp_icon(m, caller, pid, icon, size, &error))
            g_dbus_method_invocation_return_value(call, NULL);
        else { failure(call, error ? error->message : "Could not update website icon"); g_clear_error(&error); }
        g_variant_unref(bytes); return;
    }
    if (!strcmp(member, "CreateWebApp") || !strcmp(member, "CreateWebAppWithIcon")) {
        const char *name, *url; GError *error = NULL;
        GVariant *bytes = NULL; gsize size = 0; const guint8 *icon = NULL;
        if (!strcmp(member, "CreateWebAppWithIcon")) {
            g_variant_get(parameters, "(&s&s@ay)", &name, &url, &bytes);
            icon = g_variant_get_fixed_array(bytes, &size, 1);
        } else g_variant_get(parameters, "(&s&s)", &name, &url);
        char *id = appd_create_webapp(m, caller, pid, name, url, icon, size, &error);
        if (bytes) g_variant_unref(bytes);
        if (id) { g_dbus_method_invocation_return_value(call, g_variant_new("(s)", id)); g_free(id); }
        else { failure(call, error ? error->message : "Could not create web app"); g_clear_error(&error); }
        return;
    }
    gboolean privileged = openwave_supervised_process(uid, pid, "/run/openwave/ocean.pid")
        || openwave_supervised_process(uid, pid, "/run/openwave/permissiond.pid");
    if (!strcmp(member, "ResolveCaller") || !strcmp(member, "ResolveProcess") || !strcmp(member, "GetOwnIdentity")) {
        guint target_uid = uid, target_pid = pid;
        if (strcmp(member, "GetOwnIdentity")) {
            if (!privileged && !openwave_supervised_process(uid, pid, "/run/openwave/portal.pid")
                && !openwave_supervised_process(uid, pid, "/run/openwave/promptd.pid")
                && !openwave_supervised_process(uid, pid, "/run/openwave/wallpaper.pid")) {
                failure(call, "Only supervised identity consumers may resolve other applications"); return;
            }
            if (!strcmp(member, "ResolveCaller")) {
                const char *unique; g_variant_get(parameters, "(&s)", &unique);
                if (unique[0] != ':' || !appd_credentials(m, unique, &target_uid, &target_pid)) {
                    failure(call, "Caller connection is no longer present"); return;
                }
            } else g_variant_get(parameters, "(uu)", &target_uid, &target_pid);
        }
        Instance *target = appd_caller(m, target_uid, target_pid);
        if (!target) { failure(call, "Process is not a live managed application"); return; }
        g_dbus_method_invocation_return_value(call, g_variant_new("(@a{sv})", appd_identity(m, target, target_pid))); return;
    }
    if (!strcmp(member, "GetSessionIdentity")) {
        if (!openwave_supervised_process(uid, pid, "/run/openwave/permissiond.pid")) {
            failure(call, "Only permissiond may read the session identity"); return;
        }
        g_dbus_method_invocation_return_value(call, g_variant_new("(us)", m->uid, m->session)); return;
    }
    if (!strcmp(member, "RevokeAccess")) {
        if (!openwave_supervised_process(uid, pid, "/run/openwave/permissiond.pid")) {
            failure(call, "Only permissiond may revoke application access"); return;
        }
        const char *id; g_variant_get(parameters, "(&s)", &id);
        App *app = g_hash_table_lookup(m->apps, id);
        if (app && !appd_trusted_app(app)) appd_revoke(m, app);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (!caller && !privileged && uid != 0) { failure(call, "A managed application or privileged system process is required"); return; }
    gboolean renderer = m->renderer && !strcmp(m->renderer, sender) && pid == m->renderer_pid;
    if (!strcmp(member, "RegisterShell")) {
        if (!trusted_renderer(m, uid, pid)) { failure(call, "The supervised Ocean compositor is required"); return; }
        if (m->renderer && !renderer) appd_renderer_lost(m);
        if (!m->renderer) m->renderer = g_strdup(sender);
        m->renderer_pid = pid;
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (!strcmp(member, "SetSessionReady")) {
        if (!renderer) { failure(call, "Only the registered compositor can announce readiness"); return; }
        gboolean ready; g_variant_get(parameters, "(b)", &ready);
        if (ready) {
            char *socket = g_build_filename(m->runtime, "wayland-0", NULL); struct stat st;
            ready = lstat(socket, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_uid == m->uid;
            g_free(socket);
        }
        if (ready) ready = appd_publish_sockets(m);
        m->ready = ready;
        g_dbus_method_invocation_return_value(call, g_variant_new("(b)", ready)); return;
    }
    if (!strcmp(member, "ListApplications") || !strcmp(member, "GetRunningApplications")) {
        GVariantBuilder list; g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
        if (!strcmp(member, "ListApplications")) {
            GHashTableIter iter; gpointer value; g_hash_table_iter_init(&iter, m->apps);
            while (g_hash_table_iter_next(&iter, NULL, &value)) g_variant_builder_add_value(&list, appd_info(m, value, NULL));
        } else for (guint i = 0; i < m->instances->len; ++i) {
            Instance *instance = g_ptr_array_index(m->instances, i);
            g_variant_builder_add_value(&list, appd_info(m, instance->app, instance));
        }
        g_dbus_method_invocation_return_value(call, g_variant_new("(aa{sv})", &list)); return;
    }
    if (!strcmp(member, "CommitLaunch") || !strcmp(member, "CancelLaunch")) {
        const char *ticket; g_variant_get(parameters, "(&s)", &ticket);
        if (!renderer) { failure(call, "Only the compositor can complete a visual launch handoff"); return; }
        Pending *p = g_hash_table_lookup(m->pending, ticket);
        if (!p) { failure(call, "This launch request has expired"); return; }
        App *app = g_hash_table_lookup(m->apps, p->app_id);
        GError *error = NULL; guint process = 0;
        if (!strcmp(member, "CommitLaunch")) {
            if (!app || !appd_launch(m, app, p->arguments, &error)) {
                failure(call, error ? error->message : "Application is no longer installed"); g_clear_error(&error);
                g_hash_table_remove(m->pending, ticket); return;
            }
            Instance *instance = appd_instance(m, app); process = instance ? instance->pid : 0;
        }
        g_hash_table_remove(m->pending, ticket);
        if (!strcmp(member, "CommitLaunch")) g_dbus_method_invocation_return_value(call, g_variant_new("(u)", process));
        else g_dbus_method_invocation_return_value(call, NULL);
        return;
    }
    const char *id; char **arguments = NULL;
    if (!strcmp(member, "Launch")) g_variant_get(parameters, "(&s^as)", &id, &arguments);
    else g_variant_get(parameters, "(&s)", &id);
    App *app = g_hash_table_lookup(m->apps, id);
    if (!app) { failure(call, "Application is not installed"); g_strfreev(arguments); return; }
    if (!strcmp(member, "GetApplicationInfo")) {
        g_dbus_method_invocation_return_value(call, g_variant_new("(@a{sv})", appd_info(m, app, NULL)));
    } else if (!strcmp(member, "Terminate")) {
        if (!privileged && uid != 0 && (!caller || caller->app != app) && !(caller && appd_trusted_app(caller->app))) {
            failure(call, "Applications may only terminate themselves"); g_strfreev(arguments); return;
        }
        GHashTableIter iter; gpointer value;
        g_hash_table_iter_init(&iter, m->pending);
        while (g_hash_table_iter_next(&iter, NULL, &value)) {
            Pending *p = value;
            if (!strcmp(p->app_id, id)) {
                g_hash_table_iter_remove(&iter);
                appd_emit(m, "OperationFailed", g_variant_new("(ss)", id, "The pending application launch was cancelled"));
            }
        }
        appd_terminate(m, app); g_dbus_method_invocation_return_value(call, NULL);
    } else if (!strcmp(member, "Activate")) {
        Instance *instance = appd_instance(m, app);
        if (!instance || instance->stopping || !m->ready) failure(call, "Application is not running in a ready session");
        else { appd_activate(m, instance, NULL); g_dbus_method_invocation_return_value(call, NULL); }
    } else if (!strcmp(member, "Launch")) {
        GError *error = NULL;
        if (!appd_arguments(app, arguments, &error)) { failure(call, error->message); g_clear_error(&error); }
        else if (!m->ready || m->quitting) failure(call, "The Wayland session is not ready");
        else if (renderer) {
            if (!appd_launch(m, app, arguments, &error)) { failure(call, error->message); g_clear_error(&error); }
            else { Instance *instance = appd_instance(m, app); g_dbus_method_invocation_return_value(call, g_variant_new("(u)", instance->pid)); }
        } else if (g_hash_table_size(m->pending)) {
            failure(call, "An application launch handoff is already pending");
        } else {
            char *ticket = g_uuid_string_random();
            Pending *p = g_new0(Pending, 1); p->app_id = g_strdup(id); p->arguments = g_strdupv(arguments);
            p->created = g_get_monotonic_time();
            g_hash_table_insert(m->pending, ticket, p);
            g_dbus_connection_emit_signal(m->bus, m->renderer, APPD_PATH, APPD_INTERFACE, "LaunchRequested",
                g_variant_new("(ss^as)", ticket, id, arguments), NULL);
            g_dbus_method_invocation_return_value(call, g_variant_new("(u)", 0));
        }
    } else failure(call, "Unsupported application operation");
    g_strfreev(arguments);
}
static const GDBusInterfaceVTable vtable = {.method_call = method};

static void owner_changed(GDBusConnection *bus, const char *sender, const char *path,
    const char *interface, const char *signal, GVariant *parameters, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface; (void)signal;
    Manager *m = data; const char *name, *old, *next;
    g_variant_get(parameters, "(&s&s&s)", &name, &old, &next);
    if (m->permission_peer && !strcmp(name, m->permission_peer) && *old && !*next) {
        g_clear_pointer(&m->permission_peer, g_free);
        g_clear_pointer(&m->permission_correlation, g_free);
    }
    if (m->renderer && !strcmp(name, m->renderer) && *old && !*next) appd_renderer_lost(m);
}

static gboolean expire_handoffs(gpointer data)
{
    Manager *m = data;
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->pending);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        Pending *p = value;
        if (g_get_monotonic_time() - p->created > 30 * G_TIME_SPAN_SECOND) {
            appd_emit(m, "OperationFailed", g_variant_new("(ss)", p->app_id, "The visual launch handoff timed out"));
            g_hash_table_iter_remove(&iter);
        }
    }
    return G_SOURCE_CONTINUE;
}

gboolean appd_register_bus(Manager *m, GError **error)
{
    GDBusNodeInfo *node = openwave_xml_load_dbus_interface(APPD_INTERFACE, error);
    if (!node) return FALSE;
    m->registration = g_dbus_connection_register_object(m->bus, APPD_PATH, node->interfaces[0], &vtable, m, NULL, error);
    g_dbus_node_info_unref(node);
    if (!m->registration) return FALSE;
    m->pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, pending_free);
    g_timeout_add(500, expire_handoffs, m);
    m->subscription = g_dbus_connection_signal_subscribe(m->bus, "org.freedesktop.DBus",
        "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", NULL, 0, owner_changed, m, NULL);
    GVariant *reply = g_dbus_connection_call_sync(m->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", APPD_NAME, 4u),
        G_VARIANT_TYPE("(u)"), 0, 1000, NULL, error);
    if (!reply) return FALSE;
    guint result; g_variant_get(reply, "(u)", &result); g_variant_unref(reply);
    if (result != 1) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Another appd already owns the application service"); return FALSE; }
    return appd_register_services(m, error);
}
