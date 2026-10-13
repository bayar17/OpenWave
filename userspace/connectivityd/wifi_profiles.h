#ifndef OPENWAVE_CONNECTIVITY_WIFI_PROFILES_H
#define OPENWAVE_CONNECTIVITY_WIFI_PROFILES_H

#include "service.h"

NMRemoteConnection *saved_connection(Service *service, const char *ssid);
const char *wifi_security_name(NMRemoteConnection *connection);
GVariant *saved_wifi_info(Service *service, const char *ssid);
void set_saved_wifi_ip_config(Service *service, GDBusMethodInvocation *invocation, GVariant *parameters);

#endif
