#ifndef OPENWAVE_CONNECTIVITY_SERVICE_H
#define OPENWAVE_CONNECTIVITY_SERVICE_H

#include <gio/gio.h>
#include <libnm/NetworkManager.h>

#define BUS_NAME "org.openwave.Connectivity"
#define OBJECT_PATH "/org/openwave/Connectivity"
#define INTERFACE "org.openwave.Connectivity1"

typedef struct {
    GMainLoop *loop;
    GDBusConnection *bus;
    GDBusNodeInfo *node;
    NMClient *nm;
    char *hostname;
    gboolean hostname_initialized;
    gboolean avahi_available;
    gboolean bluez_available;
    guint registration;
    GDBusNodeInfo *agent_node;
    guint agent_registration;
    char *pairing_device;
    char *bluez_owner;
} Service;

void emit_signal(Service *service, const char *name);
void emit_state(Service *service);
void return_error(GDBusMethodInvocation *invocation, const char *name, const char *message);
GVariant *get_state(Service *service);

#endif
