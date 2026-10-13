#include "wifi.h"
#include "wifi_profiles.h"

NMDeviceWifi *wifi_device(Service *service)
{
    if (!service->nm)
        return NULL;
    const GPtrArray *devices = nm_client_get_devices(service->nm);
    if (!devices)
        return NULL;
    for (guint i = 0; i < devices->len; ++i) {
        NMDevice *device = g_ptr_array_index((GPtrArray *) devices, i);
        if (NM_IS_DEVICE_WIFI(device))
            return NM_DEVICE_WIFI(device);
    }
    return NULL;
}

gboolean wifi_scan_available(Service *service)
{
    if (!service->nm || !nm_client_get_nm_running(service->nm) ||
        !nm_client_wireless_get_enabled(service->nm))
        return FALSE;
    NMDeviceWifi *wifi = wifi_device(service);
    if (!wifi)
        return FALSE;
    switch (nm_device_get_state(NM_DEVICE(wifi))) {
    case NM_DEVICE_STATE_DISCONNECTED:
    case NM_DEVICE_STATE_NEED_AUTH:
    case NM_DEVICE_STATE_ACTIVATED:
    case NM_DEVICE_STATE_FAILED:
        return TRUE;
    default:
        return FALSE;
    }
}

char *ssid_text(GBytes *ssid)
{
    if (!ssid)
        return g_strdup("");
    gsize length = 0;
    const guint8 *data = g_bytes_get_data(ssid, &length);
    return nm_utils_ssid_to_utf8(data, length);
}

char *active_wifi_ssid(NMDeviceWifi *wifi)
{
    if (!wifi || nm_device_get_state(NM_DEVICE(wifi)) < NM_DEVICE_STATE_PREPARE ||
        nm_device_get_state(NM_DEVICE(wifi)) > NM_DEVICE_STATE_ACTIVATED)
        return g_strdup("");
    NMAccessPoint *ap = nm_device_wifi_get_active_access_point(wifi);
    if (ap) {
        char *name = ssid_text(nm_access_point_get_ssid(ap));
        if (name && *name)
            return name;
        g_free(name);
    }
    NMActiveConnection *active = nm_device_get_active_connection(NM_DEVICE(wifi));
    NMRemoteConnection *profile = active ? nm_active_connection_get_connection(active) : NULL;
    NMSettingWireless *wireless = profile
        ? nm_connection_get_setting_wireless(NM_CONNECTION(profile)) : NULL;
    return wireless ? ssid_text(nm_setting_wireless_get_ssid(wireless)) : g_strdup("");
}

static void append_wifi_ap(Service *service, GVariantBuilder *list,
                           GHashTable *seen, NMAccessPoint *ap, const char *active_ssid,
                           NMDeviceState device_state)
{
    char *ssid = ssid_text(nm_access_point_get_ssid(ap));
    if (!ssid || !*ssid || g_hash_table_contains(seen, ssid)) {
        g_free(ssid);
        return;
    }
    const char *security = "Open";
    if (nm_access_point_get_rsn_flags(ap) || nm_access_point_get_wpa_flags(ap))
        security = "WPA";
    else if (nm_access_point_get_flags(ap) & NM_802_11_AP_FLAGS_PRIVACY)
        security = "WEP";
    GVariantBuilder entry;
    g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&entry, "{sv}", "ssid", g_variant_new_string(ssid));
    g_variant_builder_add(&entry, "{sv}", "bssid",
                          g_variant_new_string(nm_access_point_get_bssid(ap) ?: ""));
    g_variant_builder_add(&entry, "{sv}", "strength",
                          g_variant_new_uint32(nm_access_point_get_strength(ap)));
    g_variant_builder_add(&entry, "{sv}", "frequency",
                          g_variant_new_uint32(nm_access_point_get_frequency(ap)));
    g_variant_builder_add(&entry, "{sv}", "security", g_variant_new_string(security));
    gboolean current = active_ssid && *active_ssid &&
                       g_strcmp0(ssid, active_ssid) == 0;
    g_variant_builder_add(&entry, "{sv}", "connected",
                          g_variant_new_boolean(current &&
                                                device_state == NM_DEVICE_STATE_ACTIVATED));
    g_variant_builder_add(&entry, "{sv}", "connecting",
                          g_variant_new_boolean(current &&
                                                device_state >= NM_DEVICE_STATE_PREPARE &&
                                                device_state < NM_DEVICE_STATE_ACTIVATED));
    g_variant_builder_add(&entry, "{sv}", "saved",
                          g_variant_new_boolean(saved_connection(service, ssid) != NULL));
    g_variant_builder_add_value(list, g_variant_builder_end(&entry));
    g_hash_table_add(seen, ssid);
}

static void append_wifi_profile(GVariantBuilder *list, GHashTable *seen,
                                NMRemoteConnection *profile, gboolean connected,
                                gboolean connecting)
{
    NMSettingWireless *wireless = nm_connection_get_setting_wireless(NM_CONNECTION(profile));
    if (!wireless)
        return;
    char *ssid = ssid_text(nm_setting_wireless_get_ssid(wireless));
    if (!ssid || !*ssid || g_hash_table_contains(seen, ssid)) {
        g_free(ssid);
        return;
    }
    GVariantBuilder entry;
    g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&entry, "{sv}", "ssid", g_variant_new_string(ssid));
    g_variant_builder_add(&entry, "{sv}", "strength", g_variant_new_uint32(0));
    g_variant_builder_add(&entry, "{sv}", "security",
                          g_variant_new_string(wifi_security_name(profile)));
    g_variant_builder_add(&entry, "{sv}", "connected", g_variant_new_boolean(connected));
    g_variant_builder_add(&entry, "{sv}", "connecting", g_variant_new_boolean(connecting));
    g_variant_builder_add(&entry, "{sv}", "saved", g_variant_new_boolean(TRUE));
    g_variant_builder_add_value(list, g_variant_builder_end(&entry));
    g_hash_table_add(seen, ssid);
}

GVariant *list_wifi(Service *service)
{
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aa{sv}"));
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    NMDeviceWifi *wifi = wifi_device(service);
    if (wifi) {
        NMDeviceState device_state = nm_device_get_state(NM_DEVICE(wifi));
        gboolean connecting = device_state >= NM_DEVICE_STATE_PREPARE &&
                              device_state < NM_DEVICE_STATE_ACTIVATED;
        gboolean connected = device_state == NM_DEVICE_STATE_ACTIVATED;
        char *active_ssid = active_wifi_ssid(wifi);
        NMAccessPoint *active = (connected || connecting)
            ? nm_device_wifi_get_active_access_point(wifi) : NULL;
        if (active)
            append_wifi_ap(service, &list, seen, active, active_ssid, device_state);
        NMActiveConnection *connection = (connected || connecting)
            ? nm_device_get_active_connection(NM_DEVICE(wifi)) : NULL;
        NMRemoteConnection *profile = connection
            ? nm_active_connection_get_connection(connection) : NULL;
        const GPtrArray *access_points = nm_device_wifi_get_access_points(wifi);
        for (guint i = 0; access_points && i < access_points->len; ++i) {
            NMAccessPoint *ap = g_ptr_array_index((GPtrArray *) access_points, i);
            append_wifi_ap(service, &list, seen, ap, active_ssid, device_state);
        }
        if (profile)
            append_wifi_profile(&list, seen, profile, connected, connecting);
        g_free(active_ssid);
    }
    const GPtrArray *profiles = service->nm ? nm_client_get_connections(service->nm) : NULL;
    for (guint i = 0; profiles && i < profiles->len; ++i)
        append_wifi_profile(&list, seen, g_ptr_array_index((GPtrArray *) profiles, i),
                            FALSE, FALSE);
    g_hash_table_unref(seen);
    return g_variant_builder_end(&list);
}
