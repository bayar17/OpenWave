#pragma once
#include <QMap>
#include <QStringList>
#include <functional>
#undef signals
#include <gio/gio.h>

struct RegisteredApplication {
    QString id, name, digest;
    QStringList permissions;
    bool trusted = false;
};
class LifecycleBridge {
public:
    using Event = std::function<void(const QString &, const QString &, const QString &, bool)>;
    LifecycleBridge(GDBusConnection *bus, QString owner, Event event);
    ~LifecycleBridge();
    bool start(guint user, GError **error);
    const QMap<QString, RegisteredApplication> &applications() const { return m_apps; }
    const QMap<QString, QString> &instances() const { return m_instances; }
private:
    static void received(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, void *);
    bool application(GVariant *row, RegisteredApplication *app);
    GDBusConnection *m_bus;
    QString m_owner, m_correlation;
    Event m_event;
    guint m_subscription = 0;
    guint64 m_sequence = 0;
    QMap<QString, RegisteredApplication> m_apps;
    QMap<QString, QString> m_instances;
};
