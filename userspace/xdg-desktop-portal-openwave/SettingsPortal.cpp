#include "SettingsPortal.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <cstring>

namespace {
constexpr auto path = "/org/freedesktop/portal/desktop";
constexpr auto publicInterface = "org.freedesktop.portal.Settings";
constexpr auto backendInterface = "org.freedesktop.impl.portal.Settings";
constexpr auto publicName = "org.freedesktop.portal.Desktop";
constexpr auto backendName = "org.freedesktop.impl.portal.desktop.openwave";

GVariant *variant(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::UInt: return g_variant_new_uint32(value.toUInt());
    case QMetaType::Bool: return g_variant_new_boolean(value.toBool());
    case QMetaType::Double: return g_variant_new_double(value.toDouble());
    default: return g_variant_new_string(value.toString().toUtf8().constData());
    }
}

bool matches(const QString &name, const char *pattern)
{
    const QString filter = QString::fromUtf8(pattern);
    if (filter.isEmpty() || filter == name) return true;
    return filter.endsWith(QStringLiteral(".*")) && !filter.chopped(2).contains('*')
        && name.startsWith(filter.chopped(1));
}
}

SettingsPortal::SettingsPortal(QString preferences, QString interfaces, bool backend)
    : m_preferences(QFileInfo(preferences).absoluteFilePath()), m_interfaces(interfaces), m_backend(backend) {}

SettingsPortal::~SettingsPortal()
{
    if (m_reload) g_source_remove(m_reload);
    if (m_monitor) { g_file_monitor_cancel(m_monitor); g_object_unref(m_monitor); }
    for (guint registration : m_registrations) g_dbus_connection_unregister_object(m_bus, registration);
    if (m_bus) g_object_unref(m_bus);
}

bool SettingsPortal::reload(bool notify)
{
    QSettings settings(m_preferences, QSettings::IniFormat);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        g_warning("OpenWave settings portal: cannot read preferences; retaining last valid settings");
        return false;
    }
    const bool reduced = settings.value(QStringLiteral("accessibility/reduceMotion"), false).toBool();
    const bool large = settings.value(QStringLiteral("accessibility/largeText"), false).toBool();
    Values next = {
        {QStringLiteral("org.freedesktop.appearance"), {
            {QStringLiteral("color-scheme"), 0u},
            {QStringLiteral("contrast"), 0u},
            {QStringLiteral("reduced-motion"), reduced ? 1u : 0u}}},
        {QStringLiteral("org.gnome.desktop.interface"), {
            {QStringLiteral("font-name"), QStringLiteral("Source Sans Pro")},
            {QStringLiteral("color-scheme"), QStringLiteral("default")},
            {QStringLiteral("enable-animations"), !reduced},
            {QStringLiteral("text-scaling-factor"), large ? 1.2 : 1.0}}}
    };
    const Values previous = m_values;
    m_values = next;
    if (notify) {
        for (auto ns = next.cbegin(); ns != next.cend(); ++ns)
            for (auto key = ns->cbegin(); key != ns->cend(); ++key)
                if (previous.value(ns.key()).value(key.key()) != key.value()) {
                    for (const char *interface : {backendInterface, publicInterface}) {
                        if (m_backend && interface == publicInterface) continue;
                        g_dbus_connection_emit_signal(m_bus, nullptr, path, interface, "SettingChanged",
                            g_variant_new("(ssv)", ns.key().toUtf8().constData(),
                                key.key().toUtf8().constData(), variant(key.value())), nullptr);
                    }
                }
    }
    return true;
}

bool SettingsPortal::watch(GError **error)
{
    QString directory = QFileInfo(m_preferences).absolutePath();
    while (!QFileInfo(directory).isDir() && directory != QStringLiteral("/"))
        directory = QFileInfo(directory).absolutePath();
    if (m_monitor && directory == m_watchedDirectory && !g_file_monitor_is_cancelled(m_monitor)) return true;
    if (m_monitor) { g_file_monitor_cancel(m_monitor); g_object_unref(m_monitor); m_monitor = nullptr; }
    GFile *file = g_file_new_for_path(QFile::encodeName(directory).constData());
    m_monitor = g_file_monitor_directory(file, G_FILE_MONITOR_WATCH_MOVES, nullptr, error);
    g_object_unref(file);
    m_watchedDirectory = directory;
    if (m_monitor) g_signal_connect(m_monitor, "changed", G_CALLBACK(changed), this);
    else g_warning("OpenWave settings portal: cannot watch preferences directory");
    return m_monitor != nullptr;
}

void SettingsPortal::changed(GFileMonitor *, GFile *file, GFile *other, GFileMonitorEvent, gpointer data)
{
    auto *self = static_cast<SettingsPortal *>(data);
    bool relevant = false;
    for (auto *entry : {file, other}) {
        if (!entry) continue;
        char *bytes = g_file_get_path(entry);
        const QString name = QFile::decodeName(bytes ? bytes : "");
        g_free(bytes);
        relevant |= !name.isEmpty() && (name == self->m_preferences
            || name == self->m_watchedDirectory || self->m_preferences.startsWith(name + '/'));
    }
    if (!relevant || self->m_reload) return;
    self->m_reload = g_timeout_add(100, [](gpointer context) -> gboolean {
        auto *portal = static_cast<SettingsPortal *>(context);
        portal->m_reload = 0;
        portal->reload(true);
        portal->watch();
        return G_SOURCE_REMOVE;
    }, self);
}

void SettingsPortal::method(GDBusConnection *, const char *, const char *, const char *interface,
                           const char *member, GVariant *parameters, GDBusMethodInvocation *call, gpointer data)
{
    auto *self = static_cast<SettingsPortal *>(data);
    if (!std::strcmp(member, "ReadAll")) {
        char **filters = nullptr;
        g_variant_get(parameters, "(^as)", &filters);
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a{sa{sv}}"));
        for (auto ns = self->m_values.cbegin(); ns != self->m_values.cend(); ++ns) {
            bool include = !filters[0];
            for (char **filter = filters; *filter; ++filter) include |= matches(ns.key(), *filter);
            if (!include) continue;
            GVariantBuilder keys;
            g_variant_builder_init(&keys, G_VARIANT_TYPE_VARDICT);
            for (auto key = ns->cbegin(); key != ns->cend(); ++key)
                g_variant_builder_add(&keys, "{sv}", key.key().toUtf8().constData(), variant(key.value()));
            g_variant_builder_add(&result, "{s@a{sv}}", ns.key().toUtf8().constData(), g_variant_builder_end(&keys));
        }
        g_strfreev(filters);
        g_dbus_method_invocation_return_value(call, g_variant_new("(@a{sa{sv}})", g_variant_builder_end(&result)));
        return;
    }
    const char *ns, *key;
    g_variant_get(parameters, "(&s&s)", &ns, &key);
    const QVariant value = self->m_values.value(QString::fromUtf8(ns)).value(QString::fromUtf8(key));
    if (!value.isValid()) {
        g_dbus_method_invocation_return_dbus_error(call, "org.freedesktop.portal.Error.NotFound", "Requested setting not found");
        return;
    }
    GVariant *answer = variant(value);
    if (!std::strcmp(interface, publicInterface) && !std::strcmp(member, "Read")) answer = g_variant_new_variant(answer);
    g_dbus_method_invocation_return_value(call, g_variant_new("(v)", answer));
}

GVariant *SettingsPortal::property(GDBusConnection *, const char *, const char *, const char *,
                                  const char *, GError **, gpointer)
{
    return g_variant_new_uint32(2);
}

bool SettingsPortal::start(GError **error)
{
    if (!reload(false)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid OpenWave preferences");
        return false;
    }
    m_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, error);
    if (!m_bus) return false;
    static const GDBusInterfaceVTable table = {method, property, nullptr, {nullptr}};
    for (const char *interface : {backendInterface, publicInterface}) {
        if (m_backend && interface == publicInterface) continue;
        const QByteArray contract = QFile::encodeName(QDir(m_interfaces).filePath(QString::fromUtf8(interface) + QStringLiteral(".xml")));
        GDBusNodeInfo *node = openwave_xml_load_dbus_file(contract.constData(), interface, error);
        if (!node) return false;
        const guint registration = g_dbus_connection_register_object(m_bus, path, node->interfaces[0], &table, this, nullptr, error);
        g_dbus_node_info_unref(node);
        if (!registration) return false;
        m_registrations.push_back(registration);
        GVariant *reply = g_dbus_connection_call_sync(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", interface == backendInterface ? backendName : publicName, 4u),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, error);
        if (!reply) return false;
        guint status;
        g_variant_get(reply, "(u)", &status);
        g_variant_unref(reply);
        if (status != 1) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Another settings portal owns this bus name");
            return false;
        }
    }
    return watch(error);
}
