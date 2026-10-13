#include "PortalFileChooserBackend.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QDebug>
#undef signals
#include <gio/gio.h>

namespace {
constexpr auto busName = "org.openwave.portal";
constexpr auto objectPath = "/org/openwave/portal/FileChooser";
constexpr auto interfaceName = "org.openwave.portal.FileChooser";

GVariant *call(GDBusConnection *bus, const char *method, GVariant *arguments,
               QString *error)
{
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(bus, busName, objectPath,
        interfaceName, method, arguments, nullptr, G_DBUS_CALL_FLAGS_NONE,
        3000, nullptr, &failure);
    if (failure) {
        *error = QString::fromUtf8(failure->message);
        g_clear_error(&failure);
    }
    return reply;
}
}

PortalFileChooserBackend::PortalFileChooserBackend(QObject *parent) : QObject(parent)
{
    m_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    m_pool.setMaxThreadCount(2);
    m_timer.setInterval(350);
    connect(&m_timer, &QTimer::timeout, this, &PortalFileChooserBackend::poll);
    m_timer.start();
    poll();
}

PortalFileChooserBackend::~PortalFileChooserBackend()
{
    m_pool.waitForDone();
    if (m_bus) g_object_unref(m_bus);
}

PortalFileChooserBackend::Pending PortalFileChooserBackend::next(bool *registered,
                                                                void *connection)
{
    Pending pending;
    auto *bus = static_cast<GDBusConnection *>(connection);
    if (!bus) {
        pending.error = QStringLiteral("System D-Bus is unavailable.");
        *registered = false;
        return pending;
    }
    if (!*registered) {
        GVariant *reply = call(bus, "RegisterBackend", nullptr, &pending.error);
        if (!reply) return pending;
        g_variant_unref(reply);
        *registered = true;
    }
    GVariant *reply = call(bus, "NextRequest", nullptr, &pending.error);
    if (reply) {
        const char *id = nullptr, *mode = nullptr;
        GVariant *options = nullptr;
        g_variant_get(reply, "(&o&s@a{sv})", &id, &mode, &options);
        if (id && !g_str_equal(id, "/")) {
            pending.id = QString::fromUtf8(id);
            pending.mode = QString::fromUtf8(mode);
            const char *value = nullptr;
            if (g_variant_lookup(options, "title", "&s", &value)) pending.title = QString::fromUtf8(value);
            if (g_variant_lookup(options, "muted_title", "&s", &value)) pending.mutedTitle = QString::fromUtf8(value);
            if (g_variant_lookup(options, "primary_title", "&s", &value)) pending.primaryTitle = QString::fromUtf8(value);
            if (g_variant_lookup(options, "initial_directory", "&s", &value)) pending.initialDirectory = QString::fromUtf8(value);
            if (g_variant_lookup(options, "suggested_filename", "&s", &value)) pending.suggestedFilename = QString::fromUtf8(value);
            if (g_variant_lookup(options, "application_id", "&s", &value)) pending.applicationId = QString::fromUtf8(value);
            char **mimes = nullptr;
            if (g_variant_lookup(options, "mime_types", "^as", &mimes)) {
                for (char **mime = mimes; *mime; ++mime)
                    pending.mimeTypes.append(QString::fromUtf8(*mime));
                g_strfreev(mimes);
            }
        }
        g_variant_unref(options);
        g_variant_unref(reply);
    } else {
        *registered = false;
    }
    return pending;
}

void PortalFileChooserBackend::poll()
{
    if (m_polling || m_busy) return;
    if (m_bus && g_dbus_connection_is_closed(static_cast<GDBusConnection *>(m_bus))) {
        g_object_unref(m_bus);
        m_bus = nullptr;
        m_registered = false;
    }
    if (!m_bus)
        m_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    m_polling = true;
    const bool registered = m_registered;
    void *bus = m_bus;
    m_pool.start([this, registered, bus] {
        bool nowRegistered = registered;
        const Pending pending = next(&nowRegistered, bus);
        QMetaObject::invokeMethod(this, [this, pending, nowRegistered] {
            m_polling = false;
            m_registered = nowRegistered;
            if (!pending.error.isEmpty()) {
                if (pending.error != m_lastError)
                    qWarning().noquote() << "Ocean file chooser backend:" << pending.error;
                m_lastError = pending.error;
                return;
            }
            m_lastError.clear();
            if (m_requestId == pending.id) return;
            m_requestId = pending.id;
            m_title = pending.title;
            m_mutedTitle = pending.mutedTitle;
            m_primaryTitle = pending.primaryTitle;
            m_mode = pending.mode;
            m_initialDirectory = pending.initialDirectory;
            m_suggestedFilename = pending.suggestedFilename;
            m_applicationId = pending.applicationId;
            m_mimeTypes = pending.mimeTypes;
            emit changed();
        }, Qt::QueuedConnection);
    });
}

bool PortalFileChooserBackend::acceptsPath(const QString &path) const
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink()) return false;
    if (m_mimeTypes.isEmpty()) return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = file.read(32);
    QString mime;
    if (header.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        mime = QStringLiteral("image/png");
    else if (header.startsWith(QByteArray::fromHex("ffd8ff")))
        mime = QStringLiteral("image/jpeg");
    else if (header.size() >= 12 && header.startsWith("RIFF")
             && header.mid(8, 4) == "WEBP")
        mime = QStringLiteral("image/webp");
    else
        mime = QMimeDatabase().mimeTypeForFile(info,
            QMimeDatabase::MatchContent).name();
    return m_mimeTypes.contains(mime);
}

void PortalFileChooserBackend::choose(const QStringList &paths)
{
    complete(1, paths);
}

void PortalFileChooserBackend::cancel()
{
    complete(2, {});
}

void PortalFileChooserBackend::complete(uint status, const QStringList &paths)
{
    if (m_busy || m_requestId.isEmpty()) return;
    m_busy = true;
    emit changed();
    const QString id = m_requestId;
    void *connection = m_bus;
    m_pool.start([this, id, status, paths, connection] {
        QString error;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (!bus) {
            error = QStringLiteral("Portal unavailable.");
        } else {
            GVariantBuilder files;
            g_variant_builder_init(&files, G_VARIANT_TYPE("as"));
            for (const QString &path : paths)
                g_variant_builder_add(&files, "s", path.toUtf8().constData());
            GVariant *reply = call(bus, "CompleteRequest",
                g_variant_new("(ou@as)", id.toUtf8().constData(), status,
                              g_variant_builder_end(&files)), &error);
            if (reply) g_variant_unref(reply);
        }
        QMetaObject::invokeMethod(this, [this, id, error] {
            m_busy = false;
            if (!error.isEmpty()) emit errorOccurred(error);
            else if (m_requestId == id) {
                m_requestId.clear();
                emit changed();
                poll();
            }
            emit changed();
        }, Qt::QueuedConnection);
    });
}
