#include "service.h"
#include "wifi.h"
#include "bluetooth.h"

void emit_signal(Service *service, const char *name)
{
    if (service->bus && service->registration)
        g_dbus_connection_emit_signal(service->bus, NULL, OBJECT_PATH,
                                      INTERFACE, name, NULL, NULL);
}

void emit_state(Service *service)
{
    emit_signal(service, "StateChanged");
}

void return_error(GDBusMethodInvocation *invocation,
                         const char *name, const char *message)
{
    g_dbus_method_invocation_return_dbus_error(invocation, name, message);
}

static void add_ip_state(GVariantBuilder *state, NMDevice *device)
{
    const char *ipv4 = "";
    const char *ipv6 = "";
    const char *gateway = "";
    GVariantBuilder dns;
    g_variant_builder_init(&dns, G_VARIANT_TYPE("as"));
    if (device) {
        NMIPConfig *config4 = nm_device_get_ip4_config(device);
        NMIPConfig *config6 = nm_device_get_ip6_config(device);
        if (config4) {
            GPtrArray *addresses = nm_ip_config_get_addresses(config4);
            if (addresses && addresses->len)
                ipv4 = nm_ip_address_get_address(g_ptr_array_index(addresses, 0));
            if (nm_ip_config_get_gateway(config4))
                gateway = nm_ip_config_get_gateway(config4);
            const char *const *servers = nm_ip_config_get_nameservers(config4);
            for (guint i = 0; servers && servers[i]; ++i)
                g_variant_builder_add(&dns, "s", servers[i]);
        }
        if (config6) {
            GPtrArray *addresses = nm_ip_config_get_addresses(config6);
            if (addresses && addresses->len)
                ipv6 = nm_ip_address_get_address(g_ptr_array_index(addresses, 0));
            if (!*gateway && nm_ip_config_get_gateway(config6))
                gateway = nm_ip_config_get_gateway(config6);
            const char *const *servers = nm_ip_config_get_nameservers(config6);
            for (guint i = 0; servers && servers[i]; ++i)
                g_variant_builder_add(&dns, "s", servers[i]);
        }
    }
    g_variant_builder_add(state, "{sv}", "ipv4Address", g_variant_new_string(ipv4));
    g_variant_builder_add(state, "{sv}", "ipv6Address", g_variant_new_string(ipv6));
    g_variant_builder_add(state, "{sv}", "gateway", g_variant_new_string(gateway));
    g_variant_builder_add(state, "{sv}", "dnsServers", g_variant_builder_end(&dns));
}

GVariant *get_state(Service *service)
{
    GVariantBuilder state;
    g_variant_builder_init(&state, G_VARIANT_TYPE("a{sv}"));
    gboolean nm_running = service->nm && nm_client_get_nm_running(service->nm);
    NMDeviceWifi *wifi = wifi_device(service);
    gboolean wifi_enabled = nm_running && nm_client_wireless_get_enabled(service->nm);
    guint32 wifi_strength = 0;
    const char *wifi_state = "Disabled";
    const char *connectivity = "Offline";
    char *active_ssid = g_strdup("");
    NMDevice *active_device = NULL;
    if (wifi_enabled) {
        wifi_state = "Disconnected";
        if (wifi) {
            NMDeviceState device_state = nm_device_get_state(NM_DEVICE(wifi));
            if (device_state >= NM_DEVICE_STATE_PREPARE &&
                device_state < NM_DEVICE_STATE_ACTIVATED)
                wifi_state = "Connecting";
            else if (device_state == NM_DEVICE_STATE_ACTIVATED)
                wifi_state = "Connected";
            if (device_state == NM_DEVICE_STATE_ACTIVATED) {
                NMAccessPoint *ap = nm_device_wifi_get_active_access_point(wifi);
                if (ap)
                    wifi_strength = nm_access_point_get_strength(ap);
            }
            g_free(active_ssid);
            active_ssid = active_wifi_ssid(wifi);
        }
    }
    if (nm_running) {
        NMConnectivityState link = nm_client_get_connectivity(service->nm);
        if (link == NM_CONNECTIVITY_FULL)
            connectivity = "Internet";
        else if (link == NM_CONNECTIVITY_LIMITED || link == NM_CONNECTIVITY_PORTAL)
            connectivity = "Local";
        else if (nm_client_get_state(service->nm) == NM_STATE_CONNECTING)
            connectivity = "Connecting";
        else if (nm_client_get_state(service->nm) >= NM_STATE_CONNECTED_LOCAL)
            connectivity = "Local";
        NMActiveConnection *primary = nm_client_get_primary_connection(service->nm);
        if (primary) {
            const GPtrArray *devices = nm_active_connection_get_devices(primary);
            if (devices && devices->len)
                active_device = g_ptr_array_index((GPtrArray *) devices, 0);
        }
    }
    gboolean bt_powered = FALSE;
    gboolean bt_discovering = FALSE;
    char *adapter = adapter_path(service, &bt_powered, &bt_discovering);
    g_variant_builder_add(&state, "{sv}", "networkManagerAvailable", g_variant_new_boolean(nm_running));
    g_variant_builder_add(&state, "{sv}", "bluezAvailable", g_variant_new_boolean(service->bluez_available));
    g_variant_builder_add(&state, "{sv}", "bluetoothAdapterAvailable",
                          g_variant_new_boolean(adapter != NULL));
    g_variant_builder_add(&state, "{sv}", "wifiDeviceAvailable", g_variant_new_boolean(wifi != NULL));
    g_variant_builder_add(&state, "{sv}", "wifiEnabled", g_variant_new_boolean(wifi_enabled));
    g_variant_builder_add(&state, "{sv}", "wifiScanAvailable", g_variant_new_boolean(wifi_scan_available(service)));
    g_variant_builder_add(&state, "{sv}", "wifiState", g_variant_new_string(wifi_state));
    g_variant_builder_add(&state, "{sv}", "wifiStrength", g_variant_new_uint32(wifi_strength));
    g_variant_builder_add(&state, "{sv}", "connectivity", g_variant_new_string(connectivity));
    g_variant_builder_add(&state, "{sv}", "activeSsid", g_variant_new_string(active_ssid));
    g_variant_builder_add(&state, "{sv}", "hostname",
                          g_variant_new_string(service->hostname ?: "kbphone"));
    char *local_name = g_strconcat(service->hostname ?: "kbphone", ".local", NULL);
    g_variant_builder_add(&state, "{sv}", "localName", g_variant_new_string(local_name));
    g_free(local_name);
    g_variant_builder_add(&state, "{sv}", "bluetoothEnabled", g_variant_new_boolean(bt_powered));
    g_variant_builder_add(&state, "{sv}", "bluetoothDiscovering", g_variant_new_boolean(bt_discovering));
    add_ip_state(&state, active_device);
    g_free(adapter);
    g_free(active_ssid);
    return g_variant_builder_end(&state);
}
