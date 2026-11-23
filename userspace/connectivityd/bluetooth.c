#include "bluetooth.h"
#include "bluetooth_agent.h"
#include <string.h>

static GVariant *bluez_objects(Service *service)
{
    if (!service->bluez_available || !service->bus)
        return NULL;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        service->bus, BLUEZ_NAME, "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &error);
    if (error) {
        g_clear_error(&error);
        return NULL;
    }
    if (!reply)
        return NULL;
    GVariant *objects = g_variant_get_child_value(reply, 0);
    g_variant_unref(reply);
    return objects;
}

char *adapter_path(Service *service, gboolean *powered,
                          gboolean *discovering)
{
    GVariant *objects = bluez_objects(service);
    if (!objects)
        return NULL;
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char *path;
    GVariant *interfaces;
    char *result = NULL;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant *properties = g_variant_lookup_value(
            interfaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
        if (properties) {
            gboolean value = FALSE;
            if (powered && g_variant_lookup(properties, "Powered", "b", &value))
                *powered = value;
            if (discovering && g_variant_lookup(properties, "Discovering", "b", &value))
                *discovering = value;
            result = g_strdup(path);
            g_variant_unref(properties);
            g_variant_unref(interfaces);
            break;
        }
        g_variant_unref(interfaces);
    }
    g_variant_unref(objects);
    return result;
}

gboolean bluez_device_paired(Service *service, const char *device)
{
    GVariant *objects = bluez_objects(service);
    if (!objects)
        return FALSE;
    gboolean paired = FALSE;
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char *path = NULL;
    GVariant *interfaces = NULL;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        if (g_strcmp0(path, device) == 0) {
            GVariant *properties = g_variant_lookup_value(
                interfaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
            if (properties) {
                g_variant_lookup(properties, "Paired", "b", &paired);
                g_variant_unref(properties);
            }
            g_variant_unref(interfaces);
            break;
        }
        g_variant_unref(interfaces);
    }
    g_variant_unref(objects);
    return paired;
}

static void add_string_property(GVariantBuilder *entry, GVariant *properties,
                                const char *output, const char *input)
{
    const char *value = "";
    g_variant_lookup(properties, input, "&s", &value);
    g_variant_builder_add(entry, "{sv}", output, g_variant_new_string(value));
}

static void add_boolean_property(GVariantBuilder *entry, GVariant *properties,
                                 const char *output, const char *input)
{
    gboolean value = FALSE;
    g_variant_lookup(properties, input, "b", &value);
    g_variant_builder_add(entry, "{sv}", output, g_variant_new_boolean(value));
}

static GVariant *list_bluetooth(Service *service)
{
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    GVariant *objects = bluez_objects(service);
    if (!objects)
        return g_variant_builder_end(&list);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const char *path;
    GVariant *interfaces;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant *properties = g_variant_lookup_value(
            interfaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
        if (properties) {
            GVariantBuilder entry;
            g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&entry, "{sv}", "path", g_variant_new_object_path(path));
            add_string_property(&entry, properties, "name", "Name");
            add_string_property(&entry, properties, "alias", "Alias");
            add_string_property(&entry, properties, "address", "Address");
            add_boolean_property(&entry, properties, "paired", "Paired");
            add_boolean_property(&entry, properties, "trusted", "Trusted");
            add_boolean_property(&entry, properties, "connected", "Connected");
            gint16 rssi = 0;
            if (g_variant_lookup(properties, "RSSI", "n", &rssi))
                g_variant_builder_add(&entry, "{sv}", "rssi", g_variant_new_int16(rssi));
            GVariant *uuids = g_variant_lookup_value(properties, "UUIDs", G_VARIANT_TYPE("as"));
            if (uuids) {
                g_variant_builder_add(&entry, "{sv}", "uuids", uuids);
                g_variant_unref(uuids);
            }
            g_variant_builder_add_value(&list, g_variant_builder_end(&entry));
            g_variant_unref(properties);
        }
        g_variant_unref(interfaces);
    }
    g_variant_unref(objects);
    return g_variant_builder_end(&list);
}

typedef struct {
    Service *service;
    GDBusMethodInvocation *invocation;
    gboolean pairing;
} PendingBluez;

static void bluez_call_done(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingBluez *pending = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                     result, &error);
    if (reply) {
        g_variant_unref(reply);
        g_dbus_method_invocation_return_value(pending->invocation, NULL);
    } else {
        return_error(pending->invocation, BUS_NAME ".OperationFailed",
                     error ? error->message : "BlueZ operation failed");
    }
    if (pending->pairing)
        g_clear_pointer(&pending->service->pairing_device, g_free);
    g_clear_error(&error);
    emit_state(pending->service);
    emit_signal(pending->service, "BluetoothDevicesChanged");
    g_object_unref(pending->invocation);
    g_free(pending);
}

static void bluez_call(Service *service, GDBusMethodInvocation *invocation,
                       const char *path, const char *interface,
                       const char *method, GVariant *parameters)
{
    if (!service->bluez_available) {
        return_error(invocation, BUS_NAME ".Unavailable", "BlueZ is unavailable");
        return;
    }
    PendingBluez *pending = g_new0(PendingBluez, 1);
    pending->service = service;
    pending->invocation = g_object_ref(invocation);
    pending->pairing = g_str_equal(method, "Pair");
    g_dbus_connection_call(service->bus, BLUEZ_NAME, path, interface, method,
                           parameters, NULL, G_DBUS_CALL_FLAGS_NONE,
                           pending->pairing ? 60000 : 15000,
                           NULL, bluez_call_done, pending);
}

static gboolean validate_device(Service *service, GDBusMethodInvocation *invocation,
                                const char *path)
{
    if (!service->bluez_available) {
        return_error(invocation, BUS_NAME ".Unavailable", "BlueZ is unavailable");
        return FALSE;
    }
    if (!g_variant_is_object_path(path) ||
        !g_str_has_prefix(path, "/org/bluez/") || !strstr(path, "/dev_")) {
        return_error(invocation, BUS_NAME ".InvalidArgument",
                     "Expected a BlueZ device object path");
        return FALSE;
    }
    return TRUE;
}

gboolean handle_bluetooth_method(Service *service, const char *method, GVariant *parameters,
                            GDBusMethodInvocation *invocation)
{
    if (g_str_equal(method, "ListBluetoothDevices")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@aa{sv})", list_bluetooth(service)));
        return TRUE;
    }
    if (g_str_equal(method, "SetBluetoothEnabled") ||
        g_str_equal(method, "StartBluetoothDiscovery") ||
        g_str_equal(method, "StopBluetoothDiscovery")) {
        char *adapter = adapter_path(service, NULL, NULL);
        if (!adapter) {
            return_error(invocation, BUS_NAME ".Unavailable", "No Bluetooth adapter is available");
            return TRUE;
        }
        if (g_str_equal(method, "SetBluetoothEnabled")) {
            gboolean enabled;
            g_variant_get(parameters, "(b)", &enabled);
            bluez_call(service, invocation, adapter, "org.freedesktop.DBus.Properties",
                       "Set", g_variant_new("(ssv)", "org.bluez.Adapter1",
                                            "Powered", g_variant_new_boolean(enabled)));
        } else {
            bluez_call(service, invocation, adapter, "org.bluez.Adapter1",
                       g_str_equal(method, "StartBluetoothDiscovery")
                           ? "StartDiscovery" : "StopDiscovery", NULL);
        }
        g_free(adapter);
        return TRUE;
    }
    if (g_str_equal(method, "SetDeviceAlias")) {
        const char *path, *alias;
        g_variant_get(parameters, "(&o&s)", &path, &alias);
        if (!validate_device(service, invocation, path))
            return TRUE;
        char *nickname = g_strdup(alias);
        g_strstrip(nickname);
        if (!*nickname || g_utf8_strlen(nickname, -1) > 248 ||
            strchr(nickname, '\n') || strchr(nickname, '\r')) {
            g_free(nickname);
            return_error(invocation, BUS_NAME ".InvalidArgument", "Enter a valid device nickname");
            return TRUE;
        }
        bluez_call(service, invocation, path, "org.freedesktop.DBus.Properties",
                   "Set", g_variant_new("(ssv)", "org.bluez.Device1", "Alias",
                                        g_variant_new_string(nickname)));
        g_free(nickname);
        return TRUE;
    }
    if (g_str_equal(method, "PairDevice") ||
        g_str_equal(method, "CancelPairing") ||
        g_str_equal(method, "TrustDevice") ||
        g_str_equal(method, "ConnectDevice") ||
        g_str_equal(method, "DisconnectDevice") ||
        g_str_equal(method, "RemoveDevice")) {
        const char *path;
        if (g_str_equal(method, "TrustDevice")) {
            gboolean trusted;
            g_variant_get(parameters, "(&ob)", &path, &trusted);
            if (!validate_device(service, invocation, path))
                return TRUE;
            bluez_call(service, invocation, path, "org.freedesktop.DBus.Properties",
                       "Set", g_variant_new("(ssv)", "org.bluez.Device1", "Trusted",
                                            g_variant_new_boolean(trusted)));
            return TRUE;
        }
        g_variant_get(parameters, "(&o)", &path);
        if (!validate_device(service, invocation, path))
            return TRUE;
        if (g_str_equal(method, "RemoveDevice")) {
            char *adapter = g_strdup(path);
            char *device_suffix = strstr(adapter, "/dev_");
            *device_suffix = '\0';
            bluez_call(service, invocation, adapter, "org.bluez.Adapter1",
                       "RemoveDevice", g_variant_new("(o)", path));
            g_free(adapter);
            return TRUE;
        }
        const char *bluez_method = g_str_equal(method, "PairDevice") ? "Pair" :
            g_str_equal(method, "CancelPairing") ? "CancelPairing" :
            g_str_equal(method, "ConnectDevice") ? "Connect" : "Disconnect";
        if (g_str_equal(method, "PairDevice")) {
            if (service->pairing_device) {
                return_error(invocation, BUS_NAME ".Busy", "A Bluetooth pairing is already in progress");
                return TRUE;
            }
            service->pairing_device = g_strdup(path);
        }
        bluez_call(service, invocation, path, "org.bluez.Device1", bluez_method, NULL);
        return TRUE;
    }
    return FALSE;
}

static void bluez_appeared(GDBusConnection *connection, const char *name,
                           const char *owner, gpointer data)
{
    (void) name;
    Service *service = data;
    service->bluez_available = TRUE;
    g_free(service->bluez_owner);
    service->bluez_owner = g_strdup(owner);
    register_bluez_agent(service, connection);
    emit_state(service);
    emit_signal(service, "BluetoothDevicesChanged");
}

static void bluez_vanished(GDBusConnection *connection, const char *name, gpointer data)
{
    (void) connection;
    (void) name;
    Service *service = data;
    service->bluez_available = FALSE;
    g_clear_pointer(&service->bluez_owner, g_free);
    g_clear_pointer(&service->pairing_device, g_free);
    emit_state(service);
    emit_signal(service, "BluetoothDevicesChanged");
}

static void bluez_changed(GDBusConnection *connection, const char *sender,
                          const char *path, const char *interface_name,
                          const char *signal, GVariant *parameters, gpointer data)
{
    (void) connection;
    (void) sender;
    (void) path;
    (void) interface_name;
    (void) signal;
    (void) parameters;
    Service *service = data;
    emit_state(service);
    emit_signal(service, "BluetoothDevicesChanged");
}

void watch_bluetooth(Service *service)
{
    g_bus_watch_name_on_connection(service->bus, BLUEZ_NAME,
                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   bluez_appeared, bluez_vanished, service, NULL);
    g_dbus_connection_signal_subscribe(service->bus, BLUEZ_NAME,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged", NULL, NULL,
                                       G_DBUS_SIGNAL_FLAGS_NONE, bluez_changed,
                                       service, NULL);
    g_dbus_connection_signal_subscribe(service->bus, BLUEZ_NAME,
                                       "org.freedesktop.DBus.ObjectManager",
                                       NULL, NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       bluez_changed, service, NULL);
}
