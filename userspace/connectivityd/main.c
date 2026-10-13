#include "service.h"
#include "dbus.h"
#include "bluetooth.h"
#include "bluetooth_agent.h"
#include "hostname.h"
#include "network_manager.h"

static void bus_closed(GDBusConnection *connection, gboolean remote_peer_vanished,
                       GError *error, gpointer data)
{
    (void) connection;
    (void) remote_peer_vanished;
    (void) error;
    Service *service = data;
    if (service->loop)
        g_main_loop_quit(service->loop);
}

int main(void)
{
    Service service = {0};
    GError *error = NULL;
    service.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!service.bus) {
        g_printerr("connectivityd: system bus unavailable: %s\n", error->message);
        g_clear_error(&error);
        return 1;
    }
    if (!register_connectivity_service(&service) || !register_bluetooth_agent(&service))
        return 1;
    service.loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(service.bus, "closed", G_CALLBACK(bus_closed), &service);
    g_bus_own_name_on_connection(service.bus, BUS_NAME,
                                 G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    watch_bluetooth(&service);
    watch_hostname(&service);
    start_network_manager(&service);
    g_main_loop_run(service.loop);
    return 1;
}
