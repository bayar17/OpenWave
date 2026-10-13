#ifndef OPENWAVE_CONNECTIVITY_HOSTNAME_H
#define OPENWAVE_CONNECTIVITY_HOSTNAME_H

#include "service.h"

void set_device_name(Service *service, GDBusMethodInvocation *invocation, const char *display_name);
void restore_device_name(Service *service);
void watch_hostname(Service *service);

#endif
