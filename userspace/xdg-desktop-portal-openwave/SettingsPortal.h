#pragma once
#include <OpenWave/OpenWaveXml.h>
#include <QMap>
#include <QString>
#include <QVariant>
#include <vector>

class SettingsPortal {
public:
    SettingsPortal(QString preferences, QString interfaces, bool backend);
    ~SettingsPortal();
    bool start(GError **error);
private:
    using Values = QMap<QString, QMap<QString, QVariant>>;
    bool reload(bool notify);
    bool watch(GError **error = nullptr);
    static void changed(GFileMonitor *, GFile *, GFile *, GFileMonitorEvent, gpointer);
    static void method(GDBusConnection *, const char *, const char *, const char *, const char *,
                       GVariant *, GDBusMethodInvocation *, gpointer);
    static GVariant *property(GDBusConnection *, const char *, const char *, const char *,
                              const char *, GError **, gpointer);
    QString m_preferences, m_interfaces, m_watchedDirectory;
    bool m_backend;
    Values m_values;
    GDBusConnection *m_bus = nullptr;
    GFileMonitor *m_monitor = nullptr;
    std::vector<guint> m_registrations;
    guint m_reload = 0;
};
