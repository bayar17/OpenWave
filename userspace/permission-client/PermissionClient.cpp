#include "PermissionClient.h"
#include <QMetaObject>
#include <thread>
#undef signals
#include "OpenWaveIdentity.h"
#include <gio/gio.h>

namespace {
constexpr auto service = "org.openwave.Permissions";
constexpr auto path = "/org/openwave/Permissions";
constexpr auto interface = "org.openwave.Permissions1";
QVariant decode(GVariant *value)
{
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) return QString::fromUtf8(g_variant_get_string(value, nullptr));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) return bool(g_variant_get_boolean(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) return qint64(g_variant_get_int64(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"))) {
        QVariantMap result; GVariantIter iter; const char *key; GVariant *field;
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_next(&iter, "{&sv}", &key, &field)) { result.insert(QString::fromUtf8(key), decode(field)); g_variant_unref(field); }
        return result;
    }
    QVariantList result;
    for (gsize i = 0; i < g_variant_n_children(value); ++i) {
        auto *child = g_variant_get_child_value(value, i); result.append(decode(child)); g_variant_unref(child);
    }
    return result;
}
}
namespace OpenWave {
struct PermissionClient::Transport {
    PermissionClient *client;
    GMainContext *context = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(context, false);
    GDBusConnection *bus = nullptr;
    GCancellable *cancel = g_cancellable_new();
    QString owner;
    int pending = 0;
    bool stopping = false, refreshAgain = false;
    std::thread thread;
    explicit Transport(PermissionClient *object) : client(object), thread([this] { run(); }) {}
    ~Transport() {
        g_cancellable_cancel(cancel);
        GSource *source = g_idle_source_new();
        g_source_set_callback(source, [](gpointer data) -> gboolean { auto *self = static_cast<Transport *>(data); self->stopping = true;
            if (!self->pending) g_main_loop_quit(self->loop); return G_SOURCE_REMOVE; }, this, nullptr);
        g_source_attach(source, context); g_source_unref(source); thread.join();
        g_object_unref(cancel); g_main_loop_unref(loop); g_main_context_unref(context);
    }
    bool verify(QString *failure) {
        if (!bus || g_dbus_connection_is_closed(bus)) { *failure = "The permission service bus is unavailable."; return false; }
        GError *error = nullptr;
        char *next = openwave_service_owner(bus, service, "/run/openwave/permissiond.pid", &error);
        if (!next) {
            owner.clear(); *failure = error ? QString::fromUtf8(error->message) : QStringLiteral("Permission service unavailable");
            g_clear_error(&error); return false;
        }
        const QString verified = QString::fromUtf8(next); g_free(next);
        if (verified != owner) {
            owner = verified;
            g_dbus_connection_call(bus, owner.toUtf8().constData(), path, interface, "SubscribePermissionChanges", nullptr,
                G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, 2000, cancel, nullptr, nullptr);
        }
        return true;
    }
    static void received(GDBusConnection *, const char *sender, const char *, const char *, const char *, GVariant *args, gpointer data) {
        auto *self = static_cast<Transport *>(data);
        if (self->owner != QString::fromUtf8(sender)) return;
        const auto values = decode(args).toList();
        QMetaObject::invokeMethod(self->client, [object = self->client, values] {
            emit object->permissionChanged(values.value(0).toString(), values.value(1).toString(), values.value(2).toString());
        }, Qt::QueuedConnection);
    }
    static void changed(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, gpointer data) {
        auto *self = static_cast<Transport *>(data); self->owner.clear(); QString error;
        const bool available = self->verify(&error);
        QMetaObject::invokeMethod(self->client, [object = self->client, available] {
            object->m_available = available; emit object->availableChanged();
            if (available && object->m_administrative) object->refreshApplications();
        }, Qt::QueuedConnection);
    }
    void run() {
        g_main_context_push_thread_default(context);
        GError *error = nullptr;
        char *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (address) {
            bus = g_dbus_connection_new_for_address_sync(address, GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error); g_free(address);
        }
        g_clear_error(&error);
        if (bus) {
            g_dbus_connection_set_exit_on_close(bus, false);
            g_dbus_connection_signal_subscribe(bus, service, interface, "PermissionChanged", path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, received, this, nullptr);
            g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                "/org/freedesktop/DBus", service, G_DBUS_SIGNAL_FLAGS_NONE, changed, this, nullptr);
        }
        changed(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, this);
        g_main_loop_run(loop);
        if (bus) { g_dbus_connection_close_sync(bus, nullptr, nullptr); g_object_unref(bus); bus = nullptr; }
        g_main_context_pop_thread_default(context);
    }
};
PermissionClient::PermissionClient(QObject *parent) : QAbstractListModel(parent), m_transport(std::make_unique<Transport>(this))
{
    connect(this, &PermissionClient::permissionChanged, this, [this] { if (m_administrative) refreshApplications(); });
}
PermissionClient::~PermissionClient() = default;
void PermissionClient::call(const QString &method, const QStringList &arguments, Completion completion)
{
    struct Request { Transport *transport; QString method; QStringList arguments; Completion completion; bool submitted = false; };
    auto *request = new Request{m_transport.get(), method, arguments, std::move(completion)};
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
        auto *request = static_cast<Request *>(data); auto *transport = request->transport;
        QString failure;
        if (!transport->verify(&failure)) {
            QMetaObject::invokeMethod(transport->client, [completion = request->completion, failure] { completion({}, failure); }, Qt::QueuedConnection);
            return G_SOURCE_REMOVE;
        }
        GVariantBuilder tuple; g_variant_builder_init(&tuple, G_VARIANT_TYPE_TUPLE);
        for (const auto &argument : request->arguments) g_variant_builder_add_value(&tuple, g_variant_new_string(argument.toUtf8().constData()));
        request->submitted = true;
        ++transport->pending;
        g_dbus_connection_call(transport->bus, transport->owner.toUtf8().constData(), path, interface, request->method.toUtf8().constData(),
            g_variant_builder_end(&tuple), nullptr, G_DBUS_CALL_FLAGS_NONE, request->method == "RequestPermission" ? 130000 : 3000,
            transport->cancel, [](GObject *source, GAsyncResult *result, gpointer data) {
                std::unique_ptr<Request> request(static_cast<Request *>(data)); GError *error = nullptr;
                auto *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
                const auto values = reply ? decode(reply).toList() : QVariantList();
                const auto failure = error ? QString::fromUtf8(error->message) : QString();
                if (reply) g_variant_unref(reply); g_clear_error(&error);
                --request->transport->pending;
                if (request->transport->stopping && !request->transport->pending) g_main_loop_quit(request->transport->loop);
                QMetaObject::invokeMethod(request->transport->client, [completion = request->completion, values, failure] { completion(values, failure); }, Qt::QueuedConnection);
            }, request);
        return G_SOURCE_REMOVE;
    }, request, [](gpointer data) { auto *request = static_cast<Request *>(data); if (!request->submitted) delete request; });
    g_source_attach(source, m_transport->context); g_source_unref(source);
}
int PermissionClient::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_records.size(); }
QHash<int, QByteArray> PermissionClient::roleNames() const
{ return {{ApplicationRole, "applicationId"}, {NameRole, "applicationName"}, {PermissionRole, "permissionId"}, {LabelRole, "permissionLabel"},
    {StateRole, "permissionState"}, {DurationRole, "grantDuration"}, {TrustedRole, "trustedComponent"}}; }
QVariant PermissionClient::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size()) return {};
    static const QHash<int, QString> names{{ApplicationRole, "application"}, {NameRole, "name"}, {PermissionRole, "permission"},
        {LabelRole, "label"}, {StateRole, "state"}, {DurationRole, "duration"}, {TrustedRole, "trusted"}};
    return m_records.at(index.row()).toMap().value(names.value(role));
}
void PermissionClient::resultError(const QString &error)
{ m_error = error; emit errorChanged(); if (!error.isEmpty()) emit errorOccurred(error); }
void PermissionClient::requestPermission(const QString &permission)
{
    call("RequestPermission", {permission}, [this, permission](const QVariantList &values, const QString &error) {
        resultError(error); emit permissionResult(permission, error.isEmpty() ? values.value(0).toString() : QStringLiteral("denied"));
    });
}
void PermissionClient::checkPermission(const QString &permission) { getPermissionStatus(permission); }
void PermissionClient::requestPermissionAsync(const QString &permission, PermissionCompletion completion)
{
    call("RequestPermission", {permission}, [completion = std::move(completion)](const QVariantList &values, const QString &error) {
        const auto state = values.value(0).toString();
        completion(!error.isEmpty() ? Denied : state == "granted" ? Granted : state == "restricted" ? Restricted
            : state == "denied" ? Denied : NotDetermined, error);
    });
}
void PermissionClient::checkPermissionAsync(const QString &permission, PermissionCompletion completion)
{
    call("GetPermissionStatus", {permission}, [completion = std::move(completion)](const QVariantList &values, const QString &error) {
        const auto state = values.value(0).toString();
        completion(!error.isEmpty() ? Denied : state == "granted" ? Granted : state == "restricted" ? Restricted
            : state == "denied" ? Denied : NotDetermined, error);
    });
}
void PermissionClient::getPermissionStatus(const QString &permission)
{
    call("GetPermissionStatus", {permission}, [this, permission](const QVariantList &values, const QString &error) {
        resultError(error); emit permissionResult(permission, error.isEmpty() ? values.value(0).toString() : QStringLiteral("denied"));
    });
}
void PermissionClient::subscribePermissionChanges() { call("SubscribePermissionChanges", {}, [this](const QVariantList &, const QString &error) { resultError(error); }); }
void PermissionClient::refresh(bool administrative)
{
    m_administrative = administrative;
    if (m_busy) { m_transport->refreshAgain = true; return; }
    m_busy = true; emit busyChanged();
    call(administrative ? "ListApplicationsPermissions" : "ListPermissions", {}, [this](const QVariantList &values, const QString &error) {
        m_busy = false; emit busyChanged(); resultError(error);
        beginResetModel(); m_records = error.isEmpty() ? values.value(0).toList() : QVariantList(); endResetModel(); emit recordsChanged();
        if (m_transport->refreshAgain) { m_transport->refreshAgain = false; refresh(m_administrative); }
    });
}
void PermissionClient::listPermissions() { refresh(false); }
void PermissionClient::refreshApplications() { refresh(true); }
void PermissionClient::revokePermission(const QString &app, const QString &permission)
{
    call("RevokePermission", {app, permission}, [this](const QVariantList &, const QString &error) { resultError(error); if (error.isEmpty()) refreshApplications(); });
}
void PermissionClient::setPermission(const QString &app, const QString &permission, const QString &state, const QString &duration)
{
    call("SetPermission", {app, permission, state, duration}, [this](const QVariantList &, const QString &error) { resultError(error); if (error.isEmpty()) refreshApplications(); });
}
}
