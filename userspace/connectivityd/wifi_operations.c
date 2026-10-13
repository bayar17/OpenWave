#include "wifi.h"
#include "wifi_profiles.h"
#include "network_manager.h"
#include <string.h>

typedef enum {
    OP_SCAN,
    OP_ACTIVATE,
    OP_ADD_ACTIVATE,
    OP_ADD_HIDDEN,
    OP_DEACTIVATE,
    OP_FORGET,
} NmOperation;

typedef struct {
    Service *service;
    GDBusMethodInvocation *invocation;
    NmOperation operation;
} PendingNm;

static PendingNm *pending_nm(Service *service, GDBusMethodInvocation *invocation,
                             NmOperation operation)
{
    PendingNm *pending = g_new0(PendingNm, 1);
    pending->service = service;
    pending->invocation = g_object_ref(invocation);
    pending->operation = operation;
    return pending;
}

static void nm_operation_done(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingNm *pending = data;
    GError *error = NULL;
    gboolean ok = TRUE;
    NMActiveConnection *active = NULL;
    switch (pending->operation) {
    case OP_SCAN:
        ok = nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(source), result, &error);
        break;
    case OP_ACTIVATE:
        active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
        ok = active != NULL;
        break;
    case OP_ADD_ACTIVATE:
        active = nm_client_add_and_activate_connection_finish(NM_CLIENT(source), result, &error);
        ok = active != NULL;
        break;
    case OP_ADD_HIDDEN: {
        GVariant *details = NULL;
        active = nm_client_add_and_activate_connection2_finish(NM_CLIENT(source), result,
                                                                &details, &error);
        g_clear_pointer(&details, g_variant_unref);
        ok = active != NULL;
        break;
    }
    case OP_DEACTIVATE:
        ok = nm_client_deactivate_connection_finish(NM_CLIENT(source), result, &error);
        break;
    case OP_FORGET:
        ok = nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result, &error);
        break;
    }
    if (active)
        g_object_unref(active);
    if (ok)
        g_dbus_method_invocation_return_value(pending->invocation, NULL);
    else if (pending->operation == OP_SCAN && error &&
             (g_error_matches(error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_NOT_ALLOWED) ||
              g_str_has_prefix(error->message, "Scanning not allowed")))
        return_error(pending->invocation, BUS_NAME ".ScanUnavailable",
                     "Wi-Fi scanner is not ready");
    else if (pending->operation == OP_ADD_HIDDEN) {
        g_warning("Could not add Wi-Fi network: %s", error ? error->message : "unknown error");
        return_error(pending->invocation, BUS_NAME ".OperationFailed",
                     "Could not add or connect to the network");
    } else
        return_error(pending->invocation, BUS_NAME ".OperationFailed",
                     error ? error->message : "NetworkManager operation failed");
    g_clear_error(&error);
    emit_state(pending->service);
    emit_signal(pending->service, "WifiNetworksChanged");
    g_object_unref(pending->invocation);
    g_free(pending);
}

static void connect_wifi(Service *service, GDBusMethodInvocation *invocation,
                         const char *ssid, const char *passphrase)
{
    if (!require_nm(service, invocation))
        return;
    if (!ssid || !*ssid) {
        return_error(invocation, BUS_NAME ".InvalidArgument", "SSID is empty");
        return;
    }
    NMDeviceWifi *wifi = wifi_device(service);
    if (!wifi) {
        return_error(invocation, BUS_NAME ".Unavailable", "No Wi-Fi device is available");
        return;
    }
    NMRemoteConnection *saved = saved_connection(service, ssid);
    if (saved) {
        nm_client_activate_connection_async(service->nm, NM_CONNECTION(saved),
                                            NM_DEVICE(wifi), NULL, NULL,
                                            nm_operation_done,
                                            pending_nm(service, invocation, OP_ACTIVATE));
        return;
    }
    const GPtrArray *points = nm_device_wifi_get_access_points(wifi);
    NMAccessPoint *match = NULL;
    for (guint i = 0; points && i < points->len; ++i) {
        NMAccessPoint *ap = g_ptr_array_index((GPtrArray *) points, i);
        char *name = ssid_text(nm_access_point_get_ssid(ap));
        if (g_strcmp0(name, ssid) == 0)
            match = ap;
        g_free(name);
        if (match)
            break;
    }
    if (!match) {
        return_error(invocation, BUS_NAME ".NotFound", "SSID is not in the current scan");
        return;
    }
    if ((nm_access_point_get_rsn_flags(match) ||
         nm_access_point_get_wpa_flags(match) ||
         (nm_access_point_get_flags(match) & NM_802_11_AP_FLAGS_PRIVACY)) &&
        (!passphrase || !*passphrase)) {
        return_error(invocation, BUS_NAME ".CredentialsRequired",
                     "This network requires credentials");
        return;
    }
    NMConnection *connection = nm_simple_connection_new();
    NMSetting *identity = nm_setting_connection_new();
    g_object_set(identity, NM_SETTING_CONNECTION_ID, ssid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
                 NM_SETTING_CONNECTION_AUTOCONNECT, TRUE, NULL);
    nm_connection_add_setting(connection, identity);
    NMSetting *wireless = nm_setting_wireless_new();
    GBytes *ssid_bytes = g_bytes_new(ssid, strlen(ssid));
    g_object_set(wireless, NM_SETTING_WIRELESS_SSID, ssid_bytes, NULL);
    g_bytes_unref(ssid_bytes);
    nm_connection_add_setting(connection, wireless);
    if (passphrase && *passphrase) {
        NMSetting *security = nm_setting_wireless_security_new();
        g_object_set(security,
                     NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                     NM_SETTING_WIRELESS_SECURITY_PSK, passphrase, NULL);
        nm_connection_add_setting(connection, security);
    }
    NMSetting *ipv4 = nm_setting_ip4_config_new();
    g_object_set(ipv4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
    nm_connection_add_setting(connection, ipv4);
    NMSetting *ipv6 = nm_setting_ip6_config_new();
    g_object_set(ipv6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_AUTO, NULL);
    nm_connection_add_setting(connection, ipv6);
    nm_client_add_and_activate_connection_async(
        service->nm, connection, NM_DEVICE(wifi), nm_object_get_path(NM_OBJECT(match)),
        NULL, nm_operation_done, pending_nm(service, invocation, OP_ADD_ACTIVATE));
    g_object_unref(connection);
}

static void add_wifi_network(Service *service, GDBusMethodInvocation *invocation,
                             const char *ssid, gboolean secured, const char *passphrase)
{
    if (!require_nm(service, invocation))
        return;
    gsize ssid_length = ssid ? strlen(ssid) : 0;
    gsize password_length = passphrase ? strlen(passphrase) : 0;
    if (!ssid_length || ssid_length > 32 ||
        (secured && (password_length < 8 || password_length > 63)) ||
        (!secured && password_length)) {
        return_error(invocation, BUS_NAME ".InvalidArgument",
                     "Enter a network name and valid security credentials");
        return;
    }
    if (saved_connection(service, ssid)) {
        return_error(invocation, BUS_NAME ".AlreadyExists", "This network is already saved");
        return;
    }
    NMDeviceWifi *wifi = wifi_device(service);
    if (!wifi || !nm_client_wireless_get_enabled(service->nm)) {
        return_error(invocation, BUS_NAME ".Unavailable", "Wi-Fi is unavailable");
        return;
    }

    NMConnection *connection = nm_simple_connection_new();
    NMSetting *identity = nm_setting_connection_new();
    g_object_set(identity, NM_SETTING_CONNECTION_ID, ssid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
                 NM_SETTING_CONNECTION_AUTOCONNECT, TRUE, NULL);
    nm_connection_add_setting(connection, identity);
    NMSetting *wireless = nm_setting_wireless_new();
    GBytes *ssid_bytes = g_bytes_new(ssid, ssid_length);
    g_object_set(wireless, NM_SETTING_WIRELESS_SSID, ssid_bytes,
                 NM_SETTING_WIRELESS_HIDDEN, TRUE, NULL);
    g_bytes_unref(ssid_bytes);
    nm_connection_add_setting(connection, wireless);
    if (secured) {
        NMSetting *security = nm_setting_wireless_security_new();
        g_object_set(security, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                     NM_SETTING_WIRELESS_SECURITY_PSK, passphrase, NULL);
        nm_connection_add_setting(connection, security);
    }
    NMSetting *ipv4 = nm_setting_ip4_config_new();
    g_object_set(ipv4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
    nm_connection_add_setting(connection, ipv4);
    NMSetting *ipv6 = nm_setting_ip6_config_new();
    g_object_set(ipv6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_AUTO, NULL);
    nm_connection_add_setting(connection, ipv6);

    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "persist", g_variant_new_string("disk"));
    nm_client_add_and_activate_connection2(service->nm, connection, NM_DEVICE(wifi),
        NULL, g_variant_builder_end(&options), NULL, nm_operation_done,
        pending_nm(service, invocation, OP_ADD_HIDDEN));
    g_object_unref(connection);
}

gboolean handle_wifi_method(Service *service, const char *method, GVariant *parameters,
                            GDBusMethodInvocation *invocation)
{
    if (g_str_equal(method, "ListWifiNetworks")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@aa{sv})", list_wifi(service)));
        return TRUE;
    }
    if (g_str_equal(method, "GetSavedWifiInfo")) {
        if (!require_nm(service, invocation))
            return TRUE;
        const char *ssid;
        g_variant_get(parameters, "(&s)", &ssid);
        GVariant *info = saved_wifi_info(service, ssid);
        if (!info) {
            return_error(invocation, BUS_NAME ".NotFound", "Saved Wi-Fi profile not found");
            return TRUE;
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a{sv})", info));
        return TRUE;
    }
    if (g_str_equal(method, "SetSavedWifiIpConfig")) {
        set_saved_wifi_ip_config(service, invocation, parameters);
        return TRUE;
    }
    if (g_str_equal(method, "SetWifiEnabled")) {
        if (!require_nm(service, invocation))
            return TRUE;
        gboolean enabled;
        g_variant_get(parameters, "(b)", &enabled);
        nm_client_wireless_set_enabled(service->nm, enabled);
        g_dbus_method_invocation_return_value(invocation, NULL);
        emit_state(service);
        return TRUE;
    }
    if (g_str_equal(method, "RequestWifiScan")) {
        if (!require_nm(service, invocation))
            return TRUE;
        if (!wifi_scan_available(service)) {
            return_error(invocation, BUS_NAME ".ScanUnavailable",
                         "Wi-Fi scanner is not ready");
            return TRUE;
        }
        NMDeviceWifi *wifi = wifi_device(service);
        nm_device_wifi_request_scan_async(
            wifi, NULL, nm_operation_done, pending_nm(service, invocation, OP_SCAN));
        return TRUE;
    }
    if (g_str_equal(method, "ConnectWifi")) {
        const char *ssid, *password;
        g_variant_get(parameters, "(&s&s)", &ssid, &password);
        connect_wifi(service, invocation, ssid, password);
        return TRUE;
    }
    if (g_str_equal(method, "AddWifiNetwork")) {
        const char *ssid, *password;
        gboolean secured;
        g_variant_get(parameters, "(&sb&s)", &ssid, &secured, &password);
        add_wifi_network(service, invocation, ssid, secured, password);
        return TRUE;
    }
    if (g_str_equal(method, "DisconnectWifi")) {
        if (!require_nm(service, invocation))
            return TRUE;
        NMDeviceWifi *wifi = wifi_device(service);
        NMActiveConnection *active = wifi ? nm_device_get_active_connection(NM_DEVICE(wifi)) : NULL;
        if (!active) {
            return_error(invocation, BUS_NAME ".NotFound", "No Wi-Fi connection is active");
            return TRUE;
        }
        nm_client_deactivate_connection_async(
            service->nm, active, NULL, nm_operation_done,
            pending_nm(service, invocation, OP_DEACTIVATE));
        return TRUE;
    }
    if (g_str_equal(method, "ForgetWifi")) {
        if (!require_nm(service, invocation))
            return TRUE;
        const char *ssid;
        g_variant_get(parameters, "(&s)", &ssid);
        NMRemoteConnection *saved = saved_connection(service, ssid);
        if (!saved) {
            return_error(invocation, BUS_NAME ".NotFound", "No saved profile has that SSID");
            return TRUE;
        }
        nm_remote_connection_delete_async(
            saved, NULL, nm_operation_done,
            pending_nm(service, invocation, OP_FORGET));
        return TRUE;
    }
    return FALSE;
}
