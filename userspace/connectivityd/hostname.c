#include "hostname.h"
#include "network_manager.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

static char *hostname_from_device_name(const char *device_name)
{
    char *normalized = g_utf8_normalize(device_name ?: "KB Phone", -1, G_NORMALIZE_NFKD);
    GString *label = g_string_sized_new(64);
    for (const char *cursor = normalized; cursor && *cursor && label->len < 63;
         cursor = g_utf8_next_char(cursor)) {
        gunichar character = g_utf8_get_char(cursor);
        if (character < 128 && g_ascii_isalnum((gchar) character))
            g_string_append_c(label, g_ascii_tolower((gchar) character));
    }
    g_free(normalized);
    if (!label->len)
        g_string_assign(label, "openwave");
    return g_string_free(label, FALSE);
}

static char *stored_device_name(void)
{
    const char *config_home = g_getenv("XDG_CONFIG_HOME");
    char *path = g_build_filename(config_home ?: "/var/lib/openwave",
                                  "OpenWave", "settings.ini", NULL);
    GKeyFile *settings = g_key_file_new();
    char *name = NULL;
    if (!g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL)) {
        g_free(path);
        path = g_build_filename(config_home ?: "/var/lib/openwave",
                                "OpenWave", "Ocean Launcher", "settings.ini", NULL);
        g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL);
    }
    if (g_key_file_has_key(settings, "device", "name", NULL))
        name = g_key_file_get_string(settings, "device", "name", NULL);
    g_key_file_unref(settings);
    g_free(path);
    if (!name || !*name) {
        g_free(name);
        return g_strdup("KB Phone");
    }
    return name;
}

static void hostname_published(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply)
        g_warning("Could not publish mDNS hostname: %s",
                  error ? error->message : "unknown error");
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error);
}

static void publish_hostname(Service *service)
{
    if (!service->avahi_available || !service->hostname)
        return;
    g_dbus_connection_call(service->bus, "org.freedesktop.Avahi", "/",
                           "org.freedesktop.Avahi.Server", "SetHostName",
                           g_variant_new("(s)", service->hostname), NULL,
                           G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                           hostname_published, NULL);
}

static void avahi_appeared(GDBusConnection *connection, const char *name,
                           const char *owner, gpointer data)
{
    (void) connection; (void) name; (void) owner;
    Service *service = data;
    service->avahi_available = TRUE;
    publish_hostname(service);
}

static void avahi_vanished(GDBusConnection *connection, const char *name, gpointer data)
{
    (void) connection; (void) name;
    ((Service *) data)->avahi_available = FALSE;
}

typedef struct {
    Service *service;
    GDBusMethodInvocation *invocation;
    char *label;
} PendingHostname;

static void hostname_saved(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingHostname *pending = data;
    GError *error = NULL;
    gboolean saved = nm_client_save_hostname_finish(NM_CLIENT(source), result, &error);
    if (saved) {
        if (sethostname(pending->label, strlen(pending->label)) != 0)
            g_warning("Could not apply hostname to kernel: %s", g_strerror(errno));
        g_free(pending->service->hostname);
        pending->service->hostname = g_strdup(pending->label);
        publish_hostname(pending->service);
        emit_state(pending->service);
        if (pending->invocation) {
            char *local = g_strconcat(pending->label, ".local", NULL);
            g_dbus_method_invocation_return_value(pending->invocation,
                                                   g_variant_new("(s)", local));
            g_free(local);
        }
    } else if (pending->invocation) {
        return_error(pending->invocation, BUS_NAME ".OperationFailed",
                     error ? error->message : "Could not save hostname");
    } else {
        g_warning("Could not restore hostname: %s", error ? error->message : "unknown error");
    }
    g_clear_error(&error);
    if (pending->invocation)
        g_object_unref(pending->invocation);
    g_free(pending->label);
    g_free(pending);
}

void set_device_name(Service *service, GDBusMethodInvocation *invocation,
                            const char *display_name)
{
    if (invocation && !require_nm(service, invocation))
        return;
    if (!service->nm || !nm_client_get_nm_running(service->nm))
        return;
    if (!display_name || !*display_name || g_utf8_strlen(display_name, -1) > 64) {
        if (invocation)
            return_error(invocation, BUS_NAME ".InvalidArgument", "Invalid device name");
        return;
    }
    PendingHostname *pending = g_new0(PendingHostname, 1);
    pending->service = service;
    pending->invocation = invocation ? g_object_ref(invocation) : NULL;
    pending->label = hostname_from_device_name(display_name);
    nm_client_save_hostname_async(service->nm, pending->label, NULL,
                                  hostname_saved, pending);
}

void restore_device_name(Service *service)
{
    if (nm_client_get_nm_running(service->nm) && !service->hostname_initialized) {
        service->hostname_initialized = TRUE;
        char *name = stored_device_name();
        set_device_name(service, NULL, name);
        g_free(name);
    }
}

void watch_hostname(Service *service)
{
    g_bus_watch_name_on_connection(service->bus, "org.freedesktop.Avahi",
                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   avahi_appeared, avahi_vanished, service, NULL);
}
