#include "PromptTransport.h"
#include <QFile>
#include <QMetaObject>
#include <unistd.h>
#undef signals
#include <gio/gio.h>

namespace {
constexpr auto busName = "org.openwave.Prompt";
constexpr auto objectPath = "/org/openwave/Prompt";
constexpr auto interfaceName = "org.openwave.Prompt1";

GVariant *encode(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        GVariantBuilder fields;
        g_variant_builder_init(&fields, G_VARIANT_TYPE("a{sv}"));
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            g_variant_builder_add(&fields, "{sv}", it.key().toUtf8().constData(), encode(it.value()));
        return g_variant_builder_end(&fields);
    }
    if (value.metaType().id() == QMetaType::Bool) return g_variant_new_boolean(value.toBool());
    if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::UInt
        || value.metaType().id() == QMetaType::Double) {
        const double number = value.toDouble();
        // Preserve invalid numeric options as strings so promptd rejects them,
        // rather than accidentally wrapping a negative value to an unsigned int.
        if (number >= 0 && number <= 4294967295.0 && number == quint32(number))
            return g_variant_new_uint32(quint32(number));
    }
    return g_variant_new_string(value.toString().toUtf8().constData());
}
QVariant decode(GVariant *value)
{
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
        return QString::fromUtf8(g_variant_get_string(value, nullptr));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) return g_variant_get_uint32(value);
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) return bool(g_variant_get_boolean(value));
    if (g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"))) {
        QVariantMap fields;
        GVariantIter iter; const char *key; GVariant *field;
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_next(&iter, "{&sv}", &key, &field)) {
            fields.insert(QString::fromUtf8(key), decode(field));
            g_variant_unref(field);
        }
        return fields;
    }
    return {};
}
GVariant *rpc(GDBusConnection *bus, const char *destination, const char *path,
    const char *interface, const char *method, GVariant *arguments, QString *error)
{
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(bus, destination, path, interface,
        method, arguments, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &failure);
    if (failure) { *error = QString::fromUtf8(failure->message); g_clear_error(&failure); }
    return reply;
}
QString daemonOwner(GDBusConnection *bus, QString *error)
{
    GVariant *reply = rpc(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", busName), error);
    if (!reply) return {};
    const char *name;
    g_variant_get(reply, "(&s)", &name);
    const QString owner = QString::fromUtf8(name);
    g_variant_unref(reply);
    reply = rpc(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionCredentials",
        g_variant_new("(s)", owner.toUtf8().constData()), error);
    if (!reply) return {};
    GVariant *credentials = g_variant_get_child_value(reply, 0);
    guint uid = 0, pid = 0;
    bool okay = g_variant_lookup(credentials, "UnixUserID", "u", &uid)
        && g_variant_lookup(credentials, "ProcessID", "u", &pid) && uid == getuid();
    g_variant_unref(credentials); g_variant_unref(reply);
    QFile pidfile(QStringLiteral("/run/openwave/promptd.pid"));
    okay = okay && pidfile.open(QIODevice::ReadOnly) && pidfile.readAll().trimmed().toUInt() == pid;
    const QString executable = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    if (!okay || executable != QStringLiteral("/usr/libexec/promptd")) {
        *error = QStringLiteral("The session prompt service could not be verified.");
        return {};
    }
    return owner;
}
}

PromptTransport::PromptTransport(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}
PromptTransport::~PromptTransport()
{
    m_pool.waitForDone();
    if (m_bus) {
        auto *bus = static_cast<GDBusConnection *>(m_bus);
        g_dbus_connection_close_sync(bus, nullptr, nullptr);
        g_object_unref(bus);
    }
}
void PromptTransport::call(const QString &method, const QVariantList &arguments, Completion completion)
{
    m_pool.start([this, method, arguments, completion = std::move(completion)] {
        auto *bus = static_cast<GDBusConnection *>(m_bus);
        QString error;
        if (bus && g_dbus_connection_is_closed(bus)) {
            g_object_unref(bus); m_bus = nullptr; bus = nullptr;
        }
        if (!bus) {
            GError *failure = nullptr;
            char *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &failure);
            if (address) {
                bus = g_dbus_connection_new_for_address_sync(address,
                    GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                        | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &failure);
                g_free(address);
            }
            if (failure) { error = QString::fromUtf8(failure->message); g_clear_error(&failure); }
            m_bus = bus;
            if (bus) g_dbus_connection_set_exit_on_close(bus, FALSE);
        }
        QVariantList values;
        const QString owner = bus ? daemonOwner(bus, &error) : QString();
        if (!owner.isEmpty()) {
            GVariantBuilder tuple;
            g_variant_builder_init(&tuple, G_VARIANT_TYPE_TUPLE);
            for (const auto &argument : arguments) g_variant_builder_add_value(&tuple, encode(argument));
            GVariant *reply = rpc(bus, owner.toUtf8().constData(), objectPath, interfaceName,
                method.toUtf8().constData(), g_variant_builder_end(&tuple), &error);
            if (reply) {
                for (gsize i = 0; i < g_variant_n_children(reply); ++i) {
                    GVariant *field = g_variant_get_child_value(reply, i);
                    values.append(decode(field)); g_variant_unref(field);
                }
                g_variant_unref(reply);
            }
        }
        if (owner.isEmpty() && error.isEmpty()) error = QStringLiteral("The prompt service is unavailable.");
        QMetaObject::invokeMethod(this, [completion, values, error] { completion(values, error); }, Qt::QueuedConnection);
    });
}
