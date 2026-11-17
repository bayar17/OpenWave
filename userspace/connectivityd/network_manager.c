#include "network_manager.h"
#include "hostname.h"

gboolean require_nm(Service *service, GDBusMethodInvocation *invocation)
{
    if (service->nm && nm_client_get_nm_running(service->nm))
        return TRUE;
    return_error(invocation, BUS_NAME ".Unavailable", "NetworkManager is unavailable");
    return FALSE;
}

static void nm_notify(GObject *object, GParamSpec *property, gpointer data)
{
    Service *service = data;
    if (object == (GObject *) service->nm &&
        g_strcmp0(property->name, "nm-running") == 0 &&
        nm_client_get_nm_running(service->nm) && !service->hostname_initialized) {
        restore_device_name(service);
    }
    emit_state(service);
}

static void wifi_ap_changed(NMDeviceWifi *device, NMAccessPoint *ap, gpointer data)
{
    (void) device;
    (void) ap;
    Service *service = data;
    emit_signal(service, "WifiNetworksChanged");
    emit_state(service);
}

static void nm_device_added(NMClient *client, NMDevice *device, gpointer data)
{
    (void) client;
    Service *service = data;
    g_signal_connect(device, "notify", G_CALLBACK(nm_notify), service);
    if (NM_IS_DEVICE_WIFI(device)) {
        g_signal_connect(device, "access-point-added", G_CALLBACK(wifi_ap_changed), service);
        g_signal_connect(device, "access-point-removed", G_CALLBACK(wifi_ap_changed), service);
    }
    emit_state(service);
}

static void nm_device_removed(NMClient *client, NMDevice *device, gpointer data)
{
    (void) client;
    (void) device;
    Service *service = data;
    emit_state(service);
    emit_signal(service, "WifiNetworksChanged");
}

static gboolean try_nm(gpointer data)
{
    Service *service = data;
    if (service->nm)
        return G_SOURCE_REMOVE;
    GError *error = NULL;
    service->nm = nm_client_new(NULL, &error);
    if (!service->nm) {
        g_warning("NetworkManager client unavailable: %s", error->message);
        g_clear_error(&error);
        return G_SOURCE_CONTINUE;
    }
    g_signal_connect(service->nm, "notify", G_CALLBACK(nm_notify), service);
    restore_device_name(service);
    g_signal_connect(service->nm, "device-added", G_CALLBACK(nm_device_added), service);
    g_signal_connect(service->nm, "device-removed", G_CALLBACK(nm_device_removed), service);
    const GPtrArray *devices = nm_client_get_devices(service->nm);
    for (guint i = 0; devices && i < devices->len; ++i)
        nm_device_added(service->nm, g_ptr_array_index((GPtrArray *) devices, i), service);
    emit_state(service);
    return G_SOURCE_REMOVE;
}

void start_network_manager(Service *service)
{
    try_nm(service);
    if (!service->nm)
        g_timeout_add_seconds(3, try_nm, service);
}
