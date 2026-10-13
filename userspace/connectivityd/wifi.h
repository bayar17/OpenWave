#ifndef OPENWAVE_CONNECTIVITY_WIFI_H
#define OPENWAVE_CONNECTIVITY_WIFI_H

#include "service.h"

NMDeviceWifi *wifi_device(Service *service);
gboolean wifi_scan_available(Service *service);
char *ssid_text(GBytes *ssid);
char *active_wifi_ssid(NMDeviceWifi *wifi);
GVariant *list_wifi(Service *service);
gboolean handle_wifi_method(Service *service, const char *method, GVariant *parameters, GDBusMethodInvocation *invocation);

#endif
