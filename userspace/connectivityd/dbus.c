#include <OpenWave/OpenWaveXml.h>
#include "dbus.h"
#include "wifi.h"
#include "bluetooth.h"
#include "hostname.h"

static void handle_method(GDBusConnection *connection, const char *sender,
                          const char *object_path, const char *interface_name,
                          const char *method, GVariant *parameters,
                          GDBusMethodInvocation *invocation, gpointer data)
{
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    Service *service = data;
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
