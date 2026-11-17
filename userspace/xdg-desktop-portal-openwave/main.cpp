#include "SettingsPortal.h"
#include "../preferences.h"
#include <QCoreApplication>
#include <glib-unix.h>
#include <csignal>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString preferences;
    QString interfaces = QStringLiteral("/usr/share/dbus-1/interfaces");
    bool backend = false;
    const auto arguments = application.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == QStringLiteral("--backend")) backend = true;
        else if (arguments[i] == QStringLiteral("--preferences") && i + 1 < arguments.size()) preferences = arguments[++i];
        else if (arguments[i] == QStringLiteral("--interfaces") && i + 1 < arguments.size()) interfaces = arguments[++i];
        else {
            g_printerr("Usage: xdg-desktop-portal-openwave [--backend] [--preferences FILE] [--interfaces DIRECTORY]\n");
            return 2;
        }
    }
    SettingsPortal portal(preferences.isEmpty() ? openwavePreferencesPath() : preferences, interfaces, backend);
    GError *error = nullptr;
    if (!portal.start(&error)) {
        g_printerr("OpenWave settings portal: %s\n", error ? error->message : "initialization failed");
        g_clear_error(&error);
        return 1;
    }
    GMainLoop *loop = g_main_loop_new(nullptr, false);
    const auto stop = [](gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop *>(data));
        return G_SOURCE_REMOVE;
    };
    const guint term = g_unix_signal_add(SIGTERM, stop, loop);
    const guint interrupt = g_unix_signal_add(SIGINT, stop, loop);
    g_print("OpenWave settings portal: %s ready\n", backend ? "backend" : "public API and backend");
    g_main_loop_run(loop);
    if (g_main_context_find_source_by_id(nullptr, term)) g_source_remove(term);
    if (g_main_context_find_source_by_id(nullptr, interrupt)) g_source_remove(interrupt);
    g_main_loop_unref(loop);
    return 0;
}
