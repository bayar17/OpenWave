#include <OpenWave/OpenWaveXml.h>
#include "bluetooth_agent.h"
#include "bluetooth.h"

static void agent_method(GDBusConnection *connection, const char *sender,
                         const char *object_path, const char *interface_name,
                         const char *method, GVariant *parameters,
                         GDBusMethodInvocation *invocation, gpointer data)
{
    (void) connection;
    (void) object_path;
    (void) interface_name;
    Service *service = data;
    if (g_strcmp0(sender, service->bluez_owner) != 0) {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected",
                                                    "Only BlueZ may invoke this agent");
        return;
    }
    if (g_str_equal(method, "Release") || g_str_equal(method, "Cancel")) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    const char *device = NULL;
    if (g_variant_n_children(parameters) > 0)
        g_variant_get_child(parameters, 0, "&o", &device);
    const gboolean initiated = device &&
        g_strcmp0(device, service->pairing_device) == 0;
    if ((g_str_equal(method, "RequestAuthorization") && initiated) ||
        (g_str_equal(method, "AuthorizeService") &&
         (initiated || bluez_device_paired(service, device)))) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected",
        "Pairing needs user confirmation or was not started from Settings");
}

static const GDBusInterfaceVTable agent_vtable = {
    .method_call = agent_method,
};

static void agent_registered(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                     result, &error);
    if (reply) {
        g_message("Bluetooth pairing agent registered with BlueZ");
        g_variant_unref(reply);
    } else {
        g_warning("BlueZ pairing agent registration failed: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }
}

gboolean register_bluetooth_agent(Service *service)
{
    GError *error = NULL;
    service->agent_node = openwave_xml_load_dbus_interface("org.bluez.Agent1", &error);
    if (!service->agent_node) {
        g_printerr("connectivityd: invalid Bluetooth agent: %s\n", error->message);
        g_clear_error(&error);
        return FALSE;
    }
    service->agent_registration = g_dbus_connection_register_object(
        service->bus, BLUEZ_AGENT_PATH, service->agent_node->interfaces[0],
        &agent_vtable, service, NULL, &error);
    if (!service->agent_registration) {
        g_printerr("connectivityd: Bluetooth agent registration failed: %s\n",
                   error->message);
        g_clear_error(&error);
        return FALSE;
    }
    return TRUE;
}

void register_bluez_agent(Service *service, GDBusConnection *connection)
{
    g_dbus_connection_call(connection, BLUEZ_NAME, "/org/bluez",
                           "org.bluez.AgentManager1", "RegisterAgent",
                           g_variant_new("(os)", BLUEZ_AGENT_PATH, "NoInputNoOutput"),
                           NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                           agent_registered, service);
}
