#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <pwd.h>
#include "dbus.h"
#include "wifi.h"
#include "bluetooth.h"
#include "hostname.h"

static gboolean authorized_control(GDBusConnection *bus, const char *sender, const char *method)
{
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionCredentials",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(a{sv})"), 0, 1000, NULL, NULL);
    if (!reply) return FALSE;
    GVariant *fields = g_variant_get_child_value(reply, 0);
    guint uid = 0, pid = 0;
    gboolean valid = g_variant_lookup(fields, "UnixUserID", "u", &uid)
        && g_variant_lookup(fields, "ProcessID", "u", &pid);
    g_variant_unref(fields); g_variant_unref(reply);
    if (!valid) return FALSE;
    if (uid == 0) return TRUE; /* Existing root maintenance interface. */
    struct passwd *ocean = getpwnam("ocean");
    if (!ocean || uid != ocean->pw_uid) return FALSE;
    if ((g_str_equal(method, "SetWifiEnabled") || g_str_equal(method, "RequestWifiScan")
         || g_str_equal(method, "SetBluetoothEnabled"))
        && openwave_supervised_process_uid(uid, pid, "/run/openwave/ocean.pid", ocean->pw_uid))
        return TRUE;
    GVariant *identity = openwave_resolve_caller(bus, sender, NULL);
    const char *app = NULL;
    gboolean trusted = FALSE;
    gboolean allowed = identity && g_variant_lookup(identity, "appId", "&s", &app)
        && g_str_equal(app, "com.openwave.settings")
        && g_variant_lookup(identity, "trusted", "b", &trusted) && trusted;
    if (identity) g_variant_unref(identity);
    return allowed;
}

static void handle_method(GDBusConnection *connection, const char *sender,
                          const char *object_path, const char *interface_name,
                          const char *method, GVariant *parameters,
                          GDBusMethodInvocation *invocation, gpointer data)
{
    (void) object_path;
    (void) interface_name;
    Service *service = data;
    if (!g_str_equal(method, "GetState") && !g_str_equal(method, "ListWifiNetworks")
        && !g_str_equal(method, "ListBluetoothDevices")
        && !authorized_control(connection, sender, method)) {
        return_error(invocation, BUS_NAME ".Unauthorized",
            "Only managed Settings or supervised Ocean controls may change connectivity.");
        return;
    }
    if (g_str_equal(method, "GetState")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@a{sv})", get_state(service)));
        return;
    }
    if (g_str_equal(method, "SetDeviceName")) {
        const char *name;
        g_variant_get(parameters, "(&s)", &name);
        set_device_name(service, invocation, name);
        return;
    }
    if (handle_wifi_method(service, method, parameters, invocation) ||
        handle_bluetooth_method(service, method, parameters, invocation))
        return;
    return_error(invocation, BUS_NAME ".UnknownMethod", "Unknown method");
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method,
};

gboolean register_connectivity_service(Service *service)
{
    GError *error = NULL;
    service->node = openwave_xml_load_dbus_interface(INTERFACE, &error);
    if (!service->node) {
        g_printerr("connectivityd: invalid interface: %s\n", error->message);
        g_clear_error(&error);
        return FALSE;
    }
    service->registration = g_dbus_connection_register_object(
        service->bus, OBJECT_PATH, service->node->interfaces[0],
        &interface_vtable, service, NULL, &error);
    if (!service->registration) {
        g_printerr("connectivityd: registration failed: %s\n", error->message);
        g_clear_error(&error);
        return FALSE;
    }
    return TRUE;
}
