#include "OpenWavePortals.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTemporaryDir>
#include <unistd.h>
#undef signals
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

namespace {
constexpr auto busName = "org.openwave.portal";
constexpr auto objectPath = "/org/openwave/portal/FileChooser";
constexpr auto interfaceName = "org.openwave.portal.FileChooser";

GDBusConnection *busConnection(QString *error)
{
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        *error = failure ? QString::fromUtf8(failure->message)
                         : QStringLiteral("File chooser service unavailable.");
        g_clear_error(&failure);
    }
    return bus;
}

GVariant *call(GDBusConnection *bus, const char *method, GVariant *parameters,
               QString *error)
{
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(bus, busName, objectPath,
        interfaceName, method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE,
        5000, nullptr, &failure);
    if (failure) {
        *error = QString::fromUtf8(failure->message);
        g_clear_error(&failure);
    }
    return reply;
}

GVariant *requestOptions(const QVariantMap &options)
{
    GVariantBuilder values;
    g_variant_builder_init(&values, G_VARIANT_TYPE("a{sv}"));
    const struct { const char *qml; const char *dbus; } strings[] = {
        {"title", "title"}, {"mutedTitle", "muted_title"},
        {"primaryTitle", "primary_title"},
        {"initialDirectory", "initial_directory"},
        {"suggestedFilename", "suggested_filename"},
        {"parentWindow", "parent_window"}, {"requestIdentifier", "request_identifier"}
    };
    for (const auto &field : strings) {
        const QString value = options.value(QString::fromLatin1(field.qml)).toString();
        if (!value.isEmpty())
            g_variant_builder_add(&values, "{sv}", field.dbus,
                                  g_variant_new_string(value.toUtf8().constData()));
    }
    GVariantBuilder mimes;
    g_variant_builder_init(&mimes, G_VARIANT_TYPE("as"));
    const QVariantList requested = options.value(QStringLiteral("mimeTypes")).toList();
    for (const QVariant &mime : requested)
        g_variant_builder_add(&mimes, "s", mime.toString().toUtf8().constData());
    g_variant_builder_add(&values, "{sv}", "mime_types", g_variant_builder_end(&mimes));
    return g_variant_new("(@a{sv})", g_variant_builder_end(&values));
}
}

PortalGrant::PortalGrant(QString name, int descriptor, bool writable,
                         bool directory, QObject *parent)
    : QObject(parent), m_name(std::move(name)), m_descriptor(descriptor),
      m_writable(writable), m_directory(directory)
{
}

PortalGrant::~PortalGrant()
{
    if (m_descriptor >= 0) close(m_descriptor);
    delete m_temporaryDir;
}

QByteArray PortalGrant::readAll(int limit)
{
    if (m_descriptor < 0 || m_writable || m_directory || limit <= 0
        || limit > 64 * 1024 * 1024) return {};
    if (lseek(m_descriptor, 0, SEEK_SET) < 0) return {};
    QByteArray result;
    char buffer[8192];
    while (result.size() < limit) {
        const ssize_t count = read(m_descriptor, buffer,
            qMin<qsizetype>(sizeof(buffer), limit - result.size()));
        if (count < 0) { result.clear(); break; }
        if (count == 0) break;
        result.append(buffer, count);
    }
    return result;
}

bool PortalGrant::writeAll(const QByteArray &bytes)
{
    if (m_descriptor < 0 || !m_writable || m_directory) return false;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = write(m_descriptor, bytes.constData() + offset,
                                    bytes.size() - offset);
        if (count <= 0) return false;
        offset += count;
    }
    return fsync(m_descriptor) == 0;
}

QUrl PortalGrant::temporaryCopy()
{
    if (m_descriptor < 0 || m_writable || m_directory) return {};
    if (!m_temporaryUrl.isEmpty()) return m_temporaryUrl;
    m_temporaryDir = new QTemporaryDir(QStringLiteral("/tmp/openwave-portal-XXXXXX"));
    if (!m_temporaryDir->isValid()) return {};
    const QString filename = QFileInfo(m_name).fileName();
    const QString path = m_temporaryDir->filePath(filename.isEmpty()
        ? QStringLiteral("selection") : filename);
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || lseek(m_descriptor, 0, SEEK_SET) < 0) return {};
    char buffer[8192];
    qint64 total = 0;
    for (;;) {
        const ssize_t count = read(m_descriptor, buffer, sizeof(buffer));
        if (count < 0 || total + count > 64 * 1024 * 1024) {
            output.remove(); return {};
        }
        if (count == 0) break;
        if (output.write(buffer, count) != count) { output.remove(); return {}; }
        total += count;
    }
    output.close();
    m_temporaryUrl = QUrl::fromLocalFile(path);
    return m_temporaryUrl;
}

OpenWavePortals::OpenWavePortals(QObject *parent) : QObject(parent)
{
    QString error;
    m_bus = busConnection(&error);
    m_pool.setMaxThreadCount(2);
    m_timer.setInterval(300);
    connect(&m_timer, &QTimer::timeout, this, &OpenWavePortals::poll);
}

OpenWavePortals::~OpenWavePortals()
{
    m_pool.waitForDone();
    if (m_bus) g_object_unref(m_bus);
}

void OpenWavePortals::openFile(const QVariantMap &options) { start(QStringLiteral("OpenFile"), options); }
void OpenWavePortals::openFiles(const QVariantMap &options) { start(QStringLiteral("OpenFiles"), options); }
void OpenWavePortals::saveFile(const QVariantMap &options) { start(QStringLiteral("SaveFile"), options); }
void OpenWavePortals::selectDirectory(const QVariantMap &options) { start(QStringLiteral("SelectDirectory"), options); }

void OpenWavePortals::start(const QString &method, const QVariantMap &options)
{
    if (m_busy) return;
    if (m_bus && g_dbus_connection_is_closed(static_cast<GDBusConnection *>(m_bus))) {
        g_object_unref(m_bus);
        m_bus = nullptr;
    }
    if (!m_bus) {
        QString connectionError;
        m_bus = busConnection(&connectionError);
        if (!m_bus) {
            emit errorOccurred(connectionError);
            return;
        }
    }
    qDeleteAll(m_grants);
    m_grants.clear();
    m_method = method;
    m_busy = true;
    m_cancelWhenReady = false;
    emit busyChanged();
    void *connection = m_bus;
    m_pool.start([this, method, options, connection] {
        QString error, id;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (bus) {
            GVariant *reply = call(bus, method.toLatin1().constData(),
                                   requestOptions(options), &error);
            if (reply) {
                const char *path = nullptr;
                g_variant_get(reply, "(&o)", &path);
                id = QString::fromUtf8(path);
                g_variant_unref(reply);
            }
        }
        QMetaObject::invokeMethod(this, [this, id, error] {
            if (id.isEmpty()) {
                m_busy = false;
                emit busyChanged();
                emit errorOccurred(error.isEmpty()
                    ? QStringLiteral("The file chooser could not start.") : error);
            } else {
                m_requestId = id;
                m_timer.start();
                if (m_cancelWhenReady) cancelRequest();
            }
        }, Qt::QueuedConnection);
    });
}

void OpenWavePortals::cancelRequest()
{
    if (!m_busy) return;
    if (m_requestId.isEmpty()) { m_cancelWhenReady = true; return; }
    const QString id = m_requestId;
    void *connection = m_bus;
    m_pool.start([id, connection] {
        QString error;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (!bus) return;
        GVariant *reply = call(bus, "CancelRequest",
            g_variant_new("(o)", id.toUtf8().constData()), &error);
        if (reply) g_variant_unref(reply);
    });
}

OpenWavePortals::Result OpenWavePortals::takeResult(const QString &requestId,
                                                  void *connection)
{
    Result result;
    auto *bus = static_cast<GDBusConnection *>(connection);
    if (!bus) return result;
    GError *failure = nullptr;
    GUnixFDList *fdList = nullptr;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(bus,
        busName, objectPath, interfaceName, "TakeResult",
        g_variant_new("(o)", requestId.toUtf8().constData()),
        G_VARIANT_TYPE("(uasah)"), G_DBUS_CALL_FLAGS_NONE, 5000,
        nullptr, &fdList, nullptr, &failure);
    if (!reply) {
        result.error = failure ? QString::fromUtf8(failure->message)
                               : QStringLiteral("Could not retrieve the selected file.");
        g_clear_error(&failure);
    } else {
        GVariant *names = nullptr, *handles = nullptr;
        g_variant_get(reply, "(u@as@ah)", &result.status, &names, &handles);
        gsize count = 0;
        const gchar **labels = g_variant_get_strv(names, &count);
        for (gsize i = 0; i < count; ++i)
            result.names.append(QString::fromUtf8(labels[i]));
        g_free(labels);
        for (gsize i = 0; i < g_variant_n_children(handles); ++i) {
            gint32 handle = -1;
            g_variant_get_child(handles, i, "h", &handle);
            const int descriptor = fdList ? g_unix_fd_list_get(fdList, handle, &failure) : -1;
            if (descriptor < 0) {
                result.error = failure ? QString::fromUtf8(failure->message)
                                       : QStringLiteral("Missing file grant.");
                g_clear_error(&failure);
                break;
            }
            result.descriptors.append(descriptor);
        }
        g_variant_unref(names); g_variant_unref(handles); g_variant_unref(reply);
    }
    if (fdList) g_object_unref(fdList);
    if (!result.error.isEmpty()) {
        for (int descriptor : result.descriptors) close(descriptor);
        result.descriptors.clear();
    }
    return result;
}

void OpenWavePortals::poll()
{
    if (!m_busy || m_polling || m_requestId.isEmpty()) return;
    m_polling = true;
    const QString id = m_requestId;
    void *connection = m_bus;
    m_pool.start([this, id, connection] {
        Result result;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (bus) {
            GVariant *reply = call(bus, "GetStatus",
                g_variant_new("(o)", id.toUtf8().constData()), &result.error);
            if (reply) {
                const char *message = nullptr;
                g_variant_get(reply, "(u&s)", &result.status, &message);
                if (result.status == 3)
                    result.error = QString::fromUtf8(message);
                g_variant_unref(reply);
                if (result.status != 0) {
                    Result taken = takeResult(id, connection);
                    if (taken.error.isEmpty()) {
                        taken.error = result.error;
                        result = taken;
                    }
                    else result.error = taken.error;
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, id, result] {
            m_polling = false;
            if (m_requestId != id) {
                for (int descriptor : result.descriptors) close(descriptor);
                return;
            }
            if (result.status != 0 || !result.error.isEmpty()) finish(result);
        }, Qt::QueuedConnection);
    });
}

void OpenWavePortals::finish(const Result &result)
{
    m_timer.stop();
    m_requestId.clear();
    m_busy = false;
    emit busyChanged();
    if (!result.error.isEmpty()) { emit errorOccurred(result.error); return; }
    if (result.status == 2) { emit cancelled(); return; }
    if (result.status != 1 || result.names.size() != result.descriptors.size()) {
        for (int descriptor : result.descriptors) close(descriptor);
        emit errorOccurred(QStringLiteral("The file chooser returned an invalid result."));
        return;
    }
    QVariantList grants;
    const bool writable = m_method == QStringLiteral("SaveFile");
    const bool directory = m_method == QStringLiteral("SelectDirectory");
    for (qsizetype i = 0; i < result.descriptors.size(); ++i) {
        auto *grant = new PortalGrant(result.names[i], result.descriptors[i],
                                      writable, directory, this);
        m_grants.append(grant);
        grants.append(QVariant::fromValue(static_cast<QObject *>(grant)));
    }
    emit completed(grants);
}
