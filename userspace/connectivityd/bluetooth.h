#ifndef OPENWAVE_CONNECTIVITY_BLUETOOTH_H
#define OPENWAVE_CONNECTIVITY_BLUETOOTH_H

#include "service.h"

#define BLUEZ_NAME "org.bluez"
#define BLUEZ_AGENT_PATH "/org/openwave/Connectivity/BluetoothAgent"

char *adapter_path(Service *service, gboolean *powered, gboolean *discovering);
gboolean bluez_device_paired(Service *service, const char *device);
gboolean handle_bluetooth_method(Service *service, const char *method, GVariant *parameters, GDBusMethodInvocation *invocation);
void watch_bluetooth(Service *service);

#endif
