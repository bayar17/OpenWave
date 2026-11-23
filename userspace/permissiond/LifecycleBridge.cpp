#include "LifecycleBridge.h"
#include <QUuid>
#include <QDebug>
#include <unistd.h>

namespace {
constexpr auto path = "/org/openwave/Applications";
constexpr auto interface = "org.openwave.ApplicationServices1";
QString field(GVariant *row, const char *key)
{ const char *value = ""; g_variant_lookup(row, key, "&s", &value); return QString::fromUtf8(value); }
}
LifecycleBridge::LifecycleBridge(GDBusConnection *bus, QString owner, Event event)
    : m_bus(bus), m_owner(std::move(owner)), m_event(std::move(event)) {}
LifecycleBridge::~LifecycleBridge()
{ if (m_subscription) g_dbus_connection_signal_unsubscribe(m_bus, m_subscription); }
bool LifecycleBridge::application(GVariant *row, RegisteredApplication *app)
{
    app->id = field(row, "appId"); app->name = field(row, "name"); app->digest = field(row, "manifestDigest");
    gboolean trusted = false; g_variant_lookup(row, "trusted", "b", &trusted); app->trusted = trusted;
    auto *permissions = g_variant_lookup_value(row, "declaredPermissions", G_VARIANT_TYPE("as"));
    if (!permissions || app->id.isEmpty() || app->digest.size() != 64) { if (permissions) g_variant_unref(permissions); return false; }
    GVariantIter iter; const char *permission; g_variant_iter_init(&iter, permissions);
    while (g_variant_iter_next(&iter, "&s", &permission)) app->permissions.append(QString::fromUtf8(permission));
    g_variant_unref(permissions); return true;
}
bool LifecycleBridge::start(guint user, GError **error)
{
    m_correlation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_subscription = g_dbus_connection_signal_subscribe(m_bus, m_owner.toUtf8().constData(), interface, "LifecycleEvent",
        path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, received, this, nullptr);
    auto *reply = g_dbus_connection_call_sync(m_bus, m_owner.toUtf8().constData(), path, interface, "Subscribe",
        g_variant_new("(s)", m_correlation.toUtf8().constData()), G_VARIANT_TYPE("(tusaa{sv}aa{sv})"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, error);
    if (!reply) return false;
    guint snapshotUser; const char *session; GVariant *apps, *instances;
    g_variant_get(reply, "(tu&s@aa{sv}@aa{sv})", &m_sequence, &snapshotUser, &session, &apps, &instances);
    bool okay = snapshotUser == user && *session;
    GVariantIter iter; GVariant *row; g_variant_iter_init(&iter, apps);
    while ((row = g_variant_iter_next_value(&iter))) {
        RegisteredApplication app; okay &= application(row, &app);
        if (okay) m_apps.insert(app.id, app);
        g_variant_unref(row);
    }
    g_variant_iter_init(&iter, instances);
    while ((row = g_variant_iter_next_value(&iter))) {
        m_instances.insert(field(row, "instance"), field(row, "appId")); g_variant_unref(row);
    }
    g_variant_unref(apps); g_variant_unref(instances); g_variant_unref(reply);
    if (!okay) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid application services snapshot");
    return okay;
}
void LifecycleBridge::received(GDBusConnection *, const char *sender, const char *, const char *, const char *, GVariant *args, void *data)
{
    auto *self = static_cast<LifecycleBridge *>(data);
    if (self->m_owner != QString::fromUtf8(sender)) return;
    const char *correlation, *event; guint64 sequence; GVariant *fields;
    g_variant_get(args, "(&st&s@a{sv})", &correlation, &sequence, &event, &fields);
    if (self->m_correlation != QString::fromUtf8(correlation) || sequence <= self->m_sequence) { g_variant_unref(fields); return; }
    if (sequence != self->m_sequence + 1) {
        qCritical() << "permissiond: application lifecycle gap; restarting safely";
        _exit(1);
    }
    self->m_sequence = sequence;
    const QString app = field(fields, "appId"), instance = field(fields, "instance"), operation = QString::fromUtf8(event);
    bool replaced = false;
    if (operation == "ApplicationRegistered") {
        RegisteredApplication next;
        if (!self->application(fields, &next)) _exit(1);
        replaced = self->m_apps.contains(app) && self->m_apps.value(app).digest != next.digest;
        self->m_apps.insert(app, next);
    } else if (operation == "ApplicationRemoved") self->m_apps.remove(app);
    if (operation == "ApplicationLaunched") self->m_instances.insert(instance, app);
    if (operation == "ApplicationTerminated") self->m_instances.remove(instance);
    self->m_event(operation, app, instance, replaced);
    g_variant_unref(fields);
}
