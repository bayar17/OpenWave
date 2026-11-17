#ifndef OPENWAVE_CONNECTIVITY_NETWORK_MANAGER_H
#define OPENWAVE_CONNECTIVITY_NETWORK_MANAGER_H

#include "service.h"

gboolean require_nm(Service *service, GDBusMethodInvocation *invocation);
void start_network_manager(Service *service);

#endif
