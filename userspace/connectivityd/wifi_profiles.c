#include "wifi_profiles.h"
#include "wifi.h"
#include "network_manager.h"
#include <arpa/inet.h>

NMRemoteConnection *saved_connection(Service *service, const char *ssid)
{
    if (!service->nm)
        return NULL;
    const GPtrArray *connections = nm_client_get_connections(service->nm);
    if (!connections)
        return NULL;
    for (guint i = 0; i < connections->len; ++i) {
        NMRemoteConnection *connection = g_ptr_array_index((GPtrArray *) connections, i);
        NMSettingWireless *setting = nm_connection_get_setting_wireless(NM_CONNECTION(connection));
        if (!setting)
            continue;
        char *name = ssid_text(nm_setting_wireless_get_ssid(setting));
        gboolean match = g_strcmp0(name, ssid) == 0;
        g_free(name);
        if (match)
            return connection;
    }
    return NULL;
}

const char *wifi_security_name(NMRemoteConnection *connection)
{
    NMSettingWirelessSecurity *setting =
        nm_connection_get_setting_wireless_security(NM_CONNECTION(connection));
    if (!setting)
        return "Open";
    const char *management = nm_setting_wireless_security_get_key_mgmt(setting);
    if (g_strcmp0(management, "wpa-psk") == 0)
        return "WPA Personal";
    if (g_strcmp0(management, "sae") == 0)
        return "WPA3 Personal";
    if (g_strcmp0(management, "wpa-eap") == 0 ||
        g_strcmp0(management, "wpa-eap-suite-b-192") == 0)
        return "WPA Enterprise";
    if (g_strcmp0(management, "owe") == 0)
        return "Enhanced Open";
    if (g_strcmp0(management, "none") == 0)
        return "WEP";
    return management ? management : "Unknown";
}

GVariant *saved_wifi_info(Service *service, const char *ssid)
{
    NMRemoteConnection *connection = saved_connection(service, ssid);
    if (!connection)
        return NULL;

    GVariantBuilder info;
    g_variant_builder_init(&info, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&info, "{sv}", "ssid", g_variant_new_string(ssid));
    const char *security = wifi_security_name(connection);
    g_variant_builder_add(&info, "{sv}", "security", g_variant_new_string(security));
    NMSettingConnection *settings = nm_connection_get_setting_connection(NM_CONNECTION(connection));
    g_variant_builder_add(&info, "{sv}", "autoConnect",
                          g_variant_new_boolean(settings && nm_setting_connection_get_autoconnect(settings)));

    const char *password = "";
    gboolean password_stored = FALSE;
    GError *secret_error = NULL;
    GVariant *secrets = nm_remote_connection_get_secrets(
        connection, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL, &secret_error);
    if (secrets) {
        GVariant *wireless = g_variant_lookup_value(
            secrets, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, G_VARIANT_TYPE("a{sv}"));
        if (wireless) {
            g_variant_lookup(wireless, NM_SETTING_WIRELESS_SECURITY_PSK, "&s", &password);
            if (!*password)
                g_variant_lookup(wireless, NM_SETTING_WIRELESS_SECURITY_WEP_KEY0, "&s", &password);
            if (*password) {
                g_variant_builder_add(&info, "{sv}", "password", g_variant_new_string(password));
                password_stored = TRUE;
            }
            g_variant_unref(wireless);
        }
        g_variant_unref(secrets);
    }
    gboolean secret_failed = secret_error != NULL;
    if (secret_error)
        g_clear_error(&secret_error);
    if (!password_stored)
        g_variant_builder_add(&info, "{sv}", "password", g_variant_new_string(""));
    g_variant_builder_add(&info, "{sv}", "passwordStatus",
                          g_variant_new_string(password_stored ? "Stored" :
                              (g_str_equal(security, "Open") || g_str_equal(security, "Enhanced Open"))
                                  ? "No password required" :
                              g_str_equal(security, "WPA Enterprise")
                                  ? "Managed by enterprise authentication" :
                              secret_failed ? "Password unavailable" :
                              "Not stored on this device"));

    NMDeviceWifi *wifi = wifi_device(service);
    NMAccessPoint *match = NULL;
    NMAccessPoint *active = wifi ? nm_device_wifi_get_active_access_point(wifi) : NULL;
    const GPtrArray *points = wifi ? nm_device_wifi_get_access_points(wifi) : NULL;
    for (guint i = 0; points && i < points->len; ++i) {
        NMAccessPoint *point = g_ptr_array_index((GPtrArray *) points, i);
        char *name = ssid_text(nm_access_point_get_ssid(point));
        if (g_strcmp0(name, ssid) == 0 &&
            (!match || nm_access_point_get_strength(point) > nm_access_point_get_strength(match)))
            match = point;
        g_free(name);
    }
    if (active) {
        char *name = ssid_text(nm_access_point_get_ssid(active));
        if (g_strcmp0(name, ssid) == 0)
            match = active;
        g_free(name);
    }
    gboolean connected = wifi && match && match == active &&
        nm_device_get_state(NM_DEVICE(wifi)) == NM_DEVICE_STATE_ACTIVATED;
    g_variant_builder_add(&info, "{sv}", "connected", g_variant_new_boolean(connected));
    g_variant_builder_add(&info, "{sv}", "strength",
                          g_variant_new_uint32(match ? nm_access_point_get_strength(match) : 0));
    g_variant_builder_add(&info, "{sv}", "frequency",
                          g_variant_new_uint32(match ? nm_access_point_get_frequency(match) : 0));
    g_variant_builder_add(&info, "{sv}", "bssid",
                          g_variant_new_string(match ? nm_access_point_get_bssid(match) ?: "" : ""));

    const char *ipv4 = "";
    const char *gateway = "";
    const char *dns = "";
    if (connected) {
        NMIPConfig *config = nm_device_get_ip4_config(NM_DEVICE(wifi));
        if (config) {
            GPtrArray *addresses = nm_ip_config_get_addresses(config);
            if (addresses && addresses->len)
                ipv4 = nm_ip_address_get_address(g_ptr_array_index(addresses, 0));
            gateway = nm_ip_config_get_gateway(config) ?: "";
            const char *const *servers = nm_ip_config_get_nameservers(config);
            if (servers && servers[0])
                dns = servers[0];
        }
    }
    g_variant_builder_add(&info, "{sv}", "ipv4Address", g_variant_new_string(ipv4));
    g_variant_builder_add(&info, "{sv}", "gateway", g_variant_new_string(gateway));
    g_variant_builder_add(&info, "{sv}", "dns", g_variant_new_string(dns));

    NMSettingIPConfig *profile = nm_connection_get_setting_ip4_config(NM_CONNECTION(connection));
    gboolean manual = profile && g_strcmp0(nm_setting_ip_config_get_method(profile),
                                            NM_SETTING_IP4_CONFIG_METHOD_MANUAL) == 0;
    NMIPAddress *configured = profile && nm_setting_ip_config_get_num_addresses(profile)
        ? nm_setting_ip_config_get_address(profile, 0) : NULL;
    g_variant_builder_add(&info, "{sv}", "ipManual", g_variant_new_boolean(manual));
    g_variant_builder_add(&info, "{sv}", "configuredAddress",
                          g_variant_new_string(configured ? nm_ip_address_get_address(configured) : ""));
    g_variant_builder_add(&info, "{sv}", "configuredPrefix",
                          g_variant_new_uint32(configured ? nm_ip_address_get_prefix(configured) : 24));
    g_variant_builder_add(&info, "{sv}", "configuredGateway",
                          g_variant_new_string(profile && nm_setting_ip_config_get_gateway(profile)
                              ? nm_setting_ip_config_get_gateway(profile) : ""));
    GVariantBuilder configured_dns;
    g_variant_builder_init(&configured_dns, G_VARIANT_TYPE("as"));
    guint dns_count = profile ? nm_setting_ip_config_get_num_dns(profile) : 0;
    for (guint i = 0; i < dns_count; ++i)
        g_variant_builder_add(&configured_dns, "s", nm_setting_ip_config_get_dns(profile, i));
    NMSettingIPConfig *profile6 = nm_connection_get_setting_ip6_config(NM_CONNECTION(connection));
    guint dns6_count = profile6 ? nm_setting_ip_config_get_num_dns(profile6) : 0;
    for (guint i = 0; i < dns6_count; ++i)
        g_variant_builder_add(&configured_dns, "s", nm_setting_ip_config_get_dns(profile6, i));
    g_variant_builder_add(&info, "{sv}", "configuredDns", g_variant_builder_end(&configured_dns));
    g_variant_builder_add(&info, "{sv}", "dnsManual",
                          g_variant_new_boolean((profile &&
                              nm_setting_ip_config_get_ignore_auto_dns(profile)) ||
                              (profile6 && nm_setting_ip_config_get_ignore_auto_dns(profile6))));
    return g_variant_builder_end(&info);
}

typedef struct {
    Service *service;
    GDBusMethodInvocation *invocation;
    NMRemoteConnection *profile;
} PendingIpConfig;

static void ip_config_done(PendingIpConfig *pending)
{
    emit_state(pending->service);
    g_object_unref(pending->profile);
    g_object_unref(pending->invocation);
    g_free(pending);
}

static void ip_config_reactivated(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingIpConfig *pending = data;
    GError *error = NULL;
    NMActiveConnection *active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
    if (active) {
        g_dbus_method_invocation_return_value(pending->invocation, g_variant_new("(b)", TRUE));
        g_object_unref(active);
    } else {
        g_warning("Saved Wi-Fi IP settings, but activation failed: %s",
                  error ? error->message : "unknown error");
        return_error(pending->invocation, BUS_NAME ".ApplyFailed",
                     "Settings were saved, but the network could not be reactivated");
    }
    g_clear_error(&error);
    ip_config_done(pending);
}

static void ip_config_reapplied(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingIpConfig *pending = data;
    GError *error = NULL;
    gboolean applied = nm_device_reapply_finish(NM_DEVICE(source), result, &error);
    if (!applied) {
        g_warning("Live Wi-Fi reapply failed; reactivating: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        NMDeviceWifi *wifi = wifi_device(pending->service);
        if (wifi) {
            nm_client_activate_connection_async(pending->service->nm,
                NM_CONNECTION(pending->profile), NM_DEVICE(wifi), NULL, NULL,
                ip_config_reactivated, pending);
            return;
        }
        return_error(pending->invocation, BUS_NAME ".ApplyFailed",
                     "Settings were saved, but the Wi-Fi device is unavailable");
    } else {
        g_dbus_method_invocation_return_value(pending->invocation, g_variant_new("(b)", TRUE));
    }
    ip_config_done(pending);
}

static void ip_config_saved(GObject *source, GAsyncResult *result, gpointer data)
{
    PendingIpConfig *pending = data;
    GError *error = NULL;
    GVariant *reply = nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(source), result, &error);
    if (!reply) {
        g_warning("Could not save Wi-Fi IP settings: %s",
                  error ? error->message : "unknown error");
        return_error(pending->invocation,
                     error && (g_error_matches(error, NM_SETTINGS_ERROR,
                                               NM_SETTINGS_ERROR_PERMISSION_DENIED) ||
                               g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED))
                         ? BUS_NAME ".PermissionDenied" : BUS_NAME ".ApplyFailed",
                     "Could not save network settings");
    } else {
        g_variant_unref(reply);
        NMDeviceWifi *wifi = wifi_device(pending->service);
        NMActiveConnection *active = wifi
            ? nm_device_get_active_connection(NM_DEVICE(wifi)) : NULL;
        if (active && nm_active_connection_get_connection(active) == pending->profile) {
            nm_device_reapply_async(NM_DEVICE(wifi), NULL, 0, 0, NULL,
                                    ip_config_reapplied, pending);
            return;
        }
        g_dbus_method_invocation_return_value(pending->invocation, g_variant_new("(b)", FALSE));
    }
    g_clear_error(&error);
    ip_config_done(pending);
}

static gboolean valid_ipv4(const char *text)
{
    struct in_addr address;
    return text && inet_pton(AF_INET, text, &address) == 1;
}

static gboolean valid_dns_address(const char *text)
{
    struct in_addr address4;
    struct in6_addr address6;
    return text && (inet_pton(AF_INET, text, &address4) == 1 ||
                    inet_pton(AF_INET6, text, &address6) == 1);
}

void set_saved_wifi_ip_config(Service *service, GDBusMethodInvocation *invocation,
                                     GVariant *parameters)
{
    if (!require_nm(service, invocation))
        return;
    const char *ssid, *address, *gateway;
    gboolean manual, dns_manual;
    guint32 prefix;
    GVariant *dns_list;
    g_variant_get(parameters, "(&sb&su&sb@as)", &ssid, &manual, &address, &prefix,
                  &gateway, &dns_manual, &dns_list);
    NMRemoteConnection *saved = saved_connection(service, ssid);
    if (!saved) {
        return_error(invocation, BUS_NAME ".NotFound", "Saved Wi-Fi profile not found");
        g_variant_unref(dns_list);
        return;
    }
    if ((manual && (!valid_ipv4(address) || prefix > 32 ||
                    (*gateway && !valid_ipv4(gateway)))) ||
        (!manual && (*address || *gateway)) ||
        (dns_manual && g_variant_n_children(dns_list) == 0) ||
        (!dns_manual && g_variant_n_children(dns_list) != 0)) {
        return_error(invocation, BUS_NAME ".InvalidArgument", "Invalid IPv4 or DNS configuration");
        g_variant_unref(dns_list);
        return;
    }
    GVariantIter dns_iter;
    const char *dns;
    gboolean has_ipv6_dns = FALSE;
    g_variant_iter_init(&dns_iter, dns_list);
    while (g_variant_iter_next(&dns_iter, "&s", &dns)) {
        if (!valid_dns_address(dns)) {
            return_error(invocation, BUS_NAME ".InvalidArgument",
                         "DNS servers must be IPv4 or IPv6 addresses");
            g_variant_unref(dns_list);
            return;
        }
        struct in6_addr address6;
        if (inet_pton(AF_INET6, dns, &address6) == 1)
            has_ipv6_dns = TRUE;
    }

    NMConnection *updated = nm_simple_connection_new_clone(NM_CONNECTION(saved));
    NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(updated);
    if (!ip4) {
        ip4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
        nm_connection_add_setting(updated, NM_SETTING(ip4));
    }
    nm_setting_ip_config_clear_addresses(ip4);
    nm_setting_ip_config_clear_dns(ip4);
    NMSettingIPConfig *ip6 = nm_connection_get_setting_ip6_config(updated);
    if (!ip6) {
        ip6 = NM_SETTING_IP_CONFIG(nm_setting_ip6_config_new());
        nm_connection_add_setting(updated, NM_SETTING(ip6));
    }
    nm_setting_ip_config_clear_dns(ip6);
    if (has_ipv6_dns) {
        const char *method6 = nm_setting_ip_config_get_method(ip6);
        if (g_strcmp0(method6, NM_SETTING_IP6_CONFIG_METHOD_IGNORE) == 0 ||
            g_strcmp0(method6, NM_SETTING_IP6_CONFIG_METHOD_DISABLED) == 0)
            g_object_set(ip6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_AUTO, NULL);
    }
    g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD,
                 manual ? NM_SETTING_IP4_CONFIG_METHOD_MANUAL : NM_SETTING_IP4_CONFIG_METHOD_AUTO,
                 NM_SETTING_IP_CONFIG_GATEWAY, manual && *gateway ? gateway : NULL,
                 NM_SETTING_IP_CONFIG_IGNORE_AUTO_DNS, dns_manual, NULL);
    g_object_set(ip6, NM_SETTING_IP_CONFIG_IGNORE_AUTO_DNS, dns_manual, NULL);
    GError *error = NULL;
    if (manual) {
        NMIPAddress *ip = nm_ip_address_new(AF_INET, address, prefix, &error);
        if (ip) {
            nm_setting_ip_config_add_address(ip4, ip);
            nm_ip_address_unref(ip);
        }
    }
    g_variant_iter_init(&dns_iter, dns_list);
    while (!error && g_variant_iter_next(&dns_iter, "&s", &dns)) {
        struct in6_addr address6;
        nm_setting_ip_config_add_dns(inet_pton(AF_INET6, dns, &address6) == 1
                                        ? ip6 : ip4, dns);
    }
    g_variant_unref(dns_list);
    if (!error)
        nm_connection_verify(updated, &error);
    if (error) {
        g_warning("Invalid Wi-Fi IP/DNS configuration: %s", error->message);
        return_error(invocation, BUS_NAME ".InvalidArgument", "Invalid IPv4 or DNS configuration");
        g_clear_error(&error);
        g_object_unref(updated);
        return;
    }
    GVariant *settings = nm_connection_to_dbus(updated, NM_CONNECTION_SERIALIZE_WITH_NON_SECRET);
    PendingIpConfig *pending = g_new0(PendingIpConfig, 1);
    pending->service = service;
    pending->invocation = g_object_ref(invocation);
    pending->profile = g_object_ref(saved);
    nm_remote_connection_update2(saved, settings, NM_SETTINGS_UPDATE2_FLAG_TO_DISK,
                                 NULL, NULL, ip_config_saved, pending);
    g_object_unref(updated);
}
