#include "ApplicationsClient.h"
#include <QFile>
#include <QMetaObject>
#include <thread>
#undef signals
#include <gio/gio.h>
#include "OpenWaveIdentity.h"

namespace {
constexpr auto service = "org.openwave.Applications";
constexpr auto path = "/org/openwave/Applications";
constexpr auto interface = "org.openwave.Applications1";

QVariant decode(GVariant *value)
{
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
        GVariant *child = g_variant_get_variant(value); QVariant result = decode(child); g_variant_unref(child); return result;
    }
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) return QString::fromUtf8(g_variant_get_string(value, nullptr));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) return g_variant_get_uint32(value);
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) return quint64(g_variant_get_uint64(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) return qint64(g_variant_get_int64(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) return g_variant_get_int32(value);
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) return bool(g_variant_get_boolean(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"))) {
        QVariantMap result; GVariantIter iter; const char *key; GVariant *field;
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_next(&iter, "{&sv}", &key, &field)) {
            result.insert(QString::fromUtf8(key), decode(field)); g_variant_unref(field);
        }
        return result;
    }
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
        QStringList result; GVariantIter iter; const char *text;
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_next(&iter, "&s", &text)) result.append(QString::fromUtf8(text));
        return result;
    }
    QVariantList result;
    for (gsize i = 0; i < g_variant_n_children(value); ++i) {
        GVariant *child = g_variant_get_child_value(value, i); result.append(decode(child)); g_variant_unref(child);
    }
    return result;
}
GVariant *encode(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QByteArray) {
        const auto bytes = value.toByteArray();
        return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes.constData(), bytes.size(), 1);
    }
    if (value.metaType().id() == QMetaType::Bool) return g_variant_new_boolean(value.toBool());
    if (value.metaType().id() == QMetaType::QStringList) {
        GVariantBuilder array; g_variant_builder_init(&array, G_VARIANT_TYPE("as"));
        for (const auto &item : value.toStringList()) g_variant_builder_add(&array, "s", item.toUtf8().constData());
        return g_variant_builder_end(&array);
    }
    return g_variant_new_string(value.toString().toUtf8().constData());
}
}

struct ApplicationsClient::Transport {
    ApplicationsClient *client;
    GMainContext *context = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(context, false);
    GDBusConnection *bus = nullptr;
    QString owner;
    std::thread thread;
    explicit Transport(ApplicationsClient *object) : client(object), thread([this] { run(); }) {}
    ~Transport() {
        GSource *source = g_idle_source_new();
        g_source_set_callback(source, [](gpointer data) -> gboolean {
            g_main_loop_quit(static_cast<Transport *>(data)->loop); return G_SOURCE_REMOVE;
        }, this, nullptr);
        g_source_attach(source, context); g_source_unref(source);
        thread.join(); g_main_loop_unref(loop); g_main_context_unref(context);
    }
    GVariant *rpc(const char *destination, const char *object, const char *iface, const char *method,
        GVariant *arguments, QString *error) {
        GError *failure = nullptr;
        GVariant *reply = g_dbus_connection_call_sync(bus, destination, object, iface, method,
            arguments, nullptr, G_DBUS_CALL_FLAGS_NONE, !strcmp(method, "UninstallWebApp") ? 120000 : 2000, nullptr, &failure);
        if (failure) { *error = QString::fromUtf8(failure->message); g_clear_error(&failure); }
        return reply;
    }
    bool verify(QString *error) {
        if (!bus || g_dbus_connection_is_closed(bus)) { *error = QStringLiteral("The application service bus is unavailable."); return false; }
        GVariant *reply = rpc("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
            g_variant_new("(s)", service), error);
        if (!reply) { owner.clear(); return false; }
        const char *name; g_variant_get(reply, "(&s)", &name); QString next = QString::fromUtf8(name); g_variant_unref(reply);
        reply = rpc("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionCredentials",
            g_variant_new("(s)", next.toUtf8().constData()), error);
        if (!reply) return false;
        GVariant *credentials = g_variant_get_child_value(reply, 0); guint uid = 1, pid = 0;
        bool okay = g_variant_lookup(credentials, "UnixUserID", "u", &uid) && uid == 0
            && g_variant_lookup(credentials, "ProcessID", "u", &pid);
        g_variant_unref(credentials); g_variant_unref(reply);
        okay = okay && openwave_supervised_process(uid, pid, "/run/openwave/appd.pid");
        if (!okay) { owner.clear(); *error = QStringLiteral("The application service could not be authenticated."); return false; }
        owner = next; return true;
    }
    static void received(GDBusConnection *, const char *sender, const char *, const char *, const char *member,
        GVariant *parameters, gpointer data) {
        auto *self = static_cast<Transport *>(data);
        if (self->owner != QString::fromUtf8(sender)) return;
        const QString signal = QString::fromUtf8(member); const QVariantList values = decode(parameters).toList();
        QMetaObject::invokeMethod(self->client, [client = self->client, signal, values] {
            const QString id = values.value(0).toString();
            if (signal == "ApplicationStarted") emit client->applicationStarted(id, values.value(1).toUInt());
            else if (signal == "ApplicationStopped") emit client->applicationStopped(id, values.value(1).toInt());
            else if (signal == "ApplicationCrashed") emit client->applicationCrashed(id, values.value(1).toInt());
            else if (signal == "ApplicationStateChanged") emit client->applicationStateChanged(id, values.value(1).toString(), values.value(2).toUInt(), values.value(3).toString());
            else if (signal == "ActivationRequested") emit client->activationRequested(id, values.value(1).toUInt());
            else if (signal == "LaunchRequested") emit client->launchRequested(id, values.value(1).toString(), values.value(2).toStringList());
            else if (signal == "OperationFailed") emit client->operationFailed(id, values.value(1).toString());
            else if (signal == "RegistryChanged") emit client->registryChanged();
        }, Qt::QueuedConnection);
    }
    static void changed(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *parameters, gpointer data) {
        auto *self = static_cast<Transport *>(data); const char *name, *old, *next;
        g_variant_get(parameters, "(&s&s&s)", &name, &old, &next);
        Q_UNUSED(name); Q_UNUSED(old); QString error;
        const bool available = *next && self->verify(&error);
        if (!available) self->owner.clear();
        QMetaObject::invokeMethod(self->client, [client = self->client, available] { emit client->serviceChanged(available); }, Qt::QueuedConnection);
    }
    void run() {
        g_main_context_push_thread_default(context);
        GError *error = nullptr;
        char *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (address) {
            bus = g_dbus_connection_new_for_address_sync(address, GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error); g_free(address);
        }
        if (bus) {
            g_dbus_connection_set_exit_on_close(bus, false);
            g_dbus_connection_signal_subscribe(bus, service, interface, nullptr, path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, received, this, nullptr);
            g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                "/org/freedesktop/DBus", service, G_DBUS_SIGNAL_FLAGS_NONE, changed, this, nullptr);
        }
        g_clear_error(&error); QString failure; const bool available = verify(&failure);
        QMetaObject::invokeMethod(client, [object = client, available] { emit object->serviceChanged(available); }, Qt::QueuedConnection);
        g_main_loop_run(loop);
        if (bus) { g_dbus_connection_close_sync(bus, nullptr, nullptr); g_object_unref(bus); }
        g_main_context_pop_thread_default(context);
    }
};

ApplicationsClient::ApplicationsClient(QObject *parent) : QObject(parent), m_transport(std::make_unique<Transport>(this)) {}
ApplicationsClient::~ApplicationsClient() = default;
void ApplicationsClient::call(const QString &method, const QVariantList &arguments, Completion completion)
{
    struct Request { Transport *transport; QString method; QVariantList arguments; Completion completion; };
    auto *request = new Request{m_transport.get(), method, arguments, std::move(completion)};
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
        auto *request = static_cast<Request *>(data); auto *transport = request->transport;
        QString error; QVariantList values;
        if (transport->verify(&error)) {
            GVariantBuilder tuple; g_variant_builder_init(&tuple, G_VARIANT_TYPE_TUPLE);
            for (const auto &argument : request->arguments) g_variant_builder_add_value(&tuple, encode(argument));
            GVariant *reply = transport->rpc(transport->owner.toUtf8().constData(), path, interface,
                request->method.toUtf8().constData(), g_variant_builder_end(&tuple), &error);
            if (reply) { values = decode(reply).toList(); g_variant_unref(reply); }
        }
        QMetaObject::invokeMethod(transport->client, [completion = request->completion, values, error] { completion(values, error); }, Qt::QueuedConnection);
        return G_SOURCE_REMOVE;
    }, request, [](gpointer data) { delete static_cast<Request *>(data); });
    g_source_attach(source, m_transport->context); g_source_unref(source);
}
void ApplicationsClient::listApplications(Completion c) { call("ListApplications", {}, std::move(c)); }
void ApplicationsClient::getApplicationInfo(const QString &id, Completion c) { call("GetApplicationInfo", {id}, std::move(c)); }
void ApplicationsClient::getRunningApplications(Completion c) { call("GetRunningApplications", {}, std::move(c)); }
void ApplicationsClient::launch(const QString &id, const QStringList &args, Completion c) { call("Launch", {id, args}, std::move(c)); }
void ApplicationsClient::createWebApp(const QString &name, const QString &url, Completion c) { call("CreateWebApp", {name, url}, std::move(c)); }
void ApplicationsClient::createWebApp(const QString &name, const QString &url, const QByteArray &icon, Completion c) { call("CreateWebAppWithIcon", {name, url, icon}, std::move(c)); }
void ApplicationsClient::updateWebAppIcon(const QByteArray &icon, Completion c) { call("UpdateWebAppIcon", {icon}, std::move(c)); }
void ApplicationsClient::uninstallWebApp(const QString &id, Completion c) { call("UninstallWebApp", {id}, std::move(c)); }
void ApplicationsClient::activate(const QString &id, Completion c) { call("Activate", {id}, std::move(c)); }
void ApplicationsClient::terminate(const QString &id, Completion c) { call("Terminate", {id}, std::move(c)); }
