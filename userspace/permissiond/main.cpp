#include "PermissionService.h"
#include <QCoreApplication>
#include <csignal>
#include <unistd.h>
#include <glib-unix.h>
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 1 || geteuid()) return 2;
    PermissionService service("/var/lib/openwave/permissions");
    GError *error = nullptr;
    if (!service.start(&error)) {
        g_printerr("permissiond: %s\n", error ? error->message : "initialization failed"); g_clear_error(&error); return 1;
    }
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_unix_signal_add(SIGTERM, [](gpointer data) -> gboolean { g_main_loop_quit(static_cast<GMainLoop *>(data)); return G_SOURCE_REMOVE; }, loop);
    g_unix_signal_add(SIGINT, [](gpointer data) -> gboolean { g_main_loop_quit(static_cast<GMainLoop *>(data)); return G_SOURCE_REMOVE; }, loop);
    g_print("permissiond: ready\n"); g_main_loop_run(loop); g_main_loop_unref(loop); return 0;
}
