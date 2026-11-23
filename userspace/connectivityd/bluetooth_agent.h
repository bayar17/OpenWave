#ifndef OPENWAVE_CONNECTIVITY_BLUETOOTH_AGENT_H
#define OPENWAVE_CONNECTIVITY_BLUETOOTH_AGENT_H

#include "service.h"

gboolean register_bluetooth_agent(Service *service);
void register_bluez_agent(Service *service, GDBusConnection *connection);

#endif
