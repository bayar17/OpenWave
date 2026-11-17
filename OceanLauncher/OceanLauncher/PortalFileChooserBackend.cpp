#include "PortalFileChooserBackend.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QDebug>
#include <QImageReader>
#include <QByteArray>
#include <QBuffer>
#include <algorithm>
#undef signals
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <unistd.h>

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
            g_variant_lookup(options, "image_sources", "b", &pending.imageSources);
            const char *imageSource = nullptr;
            if (g_variant_lookup(options, "image_source", "&s", &imageSource))
                pending.imageSource = QString::fromUtf8(imageSource);
            if (!pending.imageSources
                && (pending.mode == QLatin1String("OpenFile")
                    || pending.mode == QLatin1String("OpenFiles"))) {
                pending.imageSources = std::any_of(pending.mimeTypes.cbegin(),
                    pending.mimeTypes.cend(), [](const QString &mime) {
                        return mime.startsWith(QLatin1String("image/"));
                    });
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
            m_imageSources = pending.imageSources;
            m_imageSource = pending.imageSource;
            m_galleryItems.clear();
            m_galleryError.clear();
            m_galleryLoading = false;
            m_galleryLoaded = false;
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
    return m_mimeTypes.contains(mime) || (m_mimeTypes.contains(QStringLiteral("image/*"))
        && mime.startsWith(QStringLiteral("image/")));
}

void PortalFileChooserBackend::choose(const QStringList &paths)
{
    complete(1, paths);
}

void PortalFileChooserBackend::chooseGallery(const QStringList &tokens)
{
    completeGallery(1, tokens);
}

void PortalFileChooserBackend::loadGallery()
{
    if (m_galleryLoading || m_galleryLoaded || !m_imageSources || m_requestId.isEmpty()) return;
    m_galleryLoading = true;
    m_galleryError.clear();
    emit changed();
    const QString id = m_requestId;
    void *connection = m_bus;
    m_pool.start([this, id, connection] {
        QString error;
        QVariantList items;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (!bus) error = QStringLiteral("Portal unavailable.");
        GVariant *reply = bus ? call(bus, "ListGallery",
            g_variant_new("(o)", id.toUtf8().constData()), &error) : nullptr;
        if (reply) {
            GVariant *entries = nullptr;
            g_variant_get(reply, "(@a(ssss))", &entries);
            GVariantIter iter;
            g_variant_iter_init(&iter, entries);
            const char *token = nullptr, *name = nullptr, *album = nullptr, *mime = nullptr;
            while (g_variant_iter_next(&iter, "(&s&s&s&s)", &token, &name, &album, &mime)) {
                items.append(QVariantMap{{"token", QString::fromUtf8(token)},
                    {"name", QString::fromUtf8(name)}, {"album", QString::fromUtf8(album)},
                    {"mime", QString::fromUtf8(mime)}});
            }
            g_variant_unref(entries);
            g_variant_unref(reply);
        }
        QMetaObject::invokeMethod(this, [this, id, items, error] {
            if (m_requestId != id) return;
            m_galleryLoading = false;
            m_galleryLoaded = error.isEmpty();
            m_galleryItems = items;
            m_galleryError = error;
            emit changed();
        }, Qt::QueuedConnection);
    });
}

QString PortalFileChooserBackend::galleryImageUrl(const QString &token) const
{
    if (m_requestId.isEmpty() || token.isEmpty()) return {};
    const QByteArray encoded = m_requestId.toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("image://openwavegallery/%1/%2")
        .arg(QString::fromLatin1(encoded), token);
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

void PortalFileChooserBackend::completeGallery(uint status, const QStringList &tokens)
{
    if (m_busy || m_requestId.isEmpty()) return;
    m_busy = true;
    emit changed();
    const QString id = m_requestId;
    void *connection = m_bus;
    m_pool.start([this, id, status, tokens, connection] {
        QString error;
        auto *bus = static_cast<GDBusConnection *>(connection);
        if (bus) {
            GVariantBuilder selection;
            g_variant_builder_init(&selection, G_VARIANT_TYPE("as"));
            for (const QString &token : tokens)
                g_variant_builder_add(&selection, "s", token.toUtf8().constData());
            GVariant *reply = call(bus, "CompleteGalleryRequest",
                g_variant_new("(ou@as)", id.toUtf8().constData(), status,
                    g_variant_builder_end(&selection)), &error);
            if (reply) g_variant_unref(reply);
        } else {
            error = QStringLiteral("Portal unavailable.");
        }
        QMetaObject::invokeMethod(this, [this, id, error] {
            m_busy = false;
            if (!error.isEmpty()) emit errorOccurred(error);
            else if (m_requestId == id) {
                m_requestId.clear();
                m_galleryItems.clear();
                m_galleryLoaded = false;
                m_imageSources = false;
                m_imageSource.clear();
                emit changed();
                poll();
            }
            emit changed();
        }, Qt::QueuedConnection);
    });
}

QImage PortalFileChooserBackend::galleryImage(const QString &requestId,
    const QString &token, const QSize &requestedSize) const
{
    auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!bus) return {};
    GError *error = nullptr;
    GUnixFDList *outFds = nullptr;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(bus,
        busName, objectPath, interfaceName, "OpenGalleryItem",
        g_variant_new("(os)", requestId.toUtf8().constData(), token.toUtf8().constData()),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
        &outFds, nullptr, &error);
    g_object_unref(bus);
    if (!reply || !outFds) {
        if (error) g_error_free(error);
        if (reply) g_variant_unref(reply);
        if (outFds) g_object_unref(outFds);
        return {};
    }
    gint handle = -1;
    g_variant_get(reply, "(h)", &handle);
    int fd = g_unix_fd_list_get(outFds, handle, &error);
    g_variant_unref(reply);
    g_object_unref(outFds);
    if (fd < 0) {
        if (error) g_error_free(error);
        return {};
    }
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0
        || info.st_size > 64 * 1024 * 1024) {
        close(fd);
        return {};
    }
    QByteArray data;
    data.reserve(static_cast<qsizetype>(info.st_size));
    char buffer[65536];
    ssize_t count = 0;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        if (data.size() + count > 64 * 1024 * 1024) { data.clear(); break; }
        data.append(buffer, static_cast<qsizetype>(count));
    }
    close(fd);
    if (count < 0 || data.isEmpty()) return {};
    QBuffer input(&data);
    if (!input.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (requestedSize.isValid() && original.isValid()
        && (original.width() > requestedSize.width() || original.height() > requestedSize.height()))
        reader.setScaledSize(original.scaled(requestedSize, Qt::KeepAspectRatio));
    return reader.read();
}

PortalGalleryImageProvider::PortalGalleryImageProvider(
    const PortalFileChooserBackend *backend)
    : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend)
{}

QImage PortalGalleryImageProvider::requestImage(const QString &id, QSize *size,
                                                 const QSize &requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 2 || !m_backend) return {};
    const QString request = QString::fromUtf8(QByteArray::fromBase64(
        parts.at(0).toLatin1(), QByteArray::Base64UrlEncoding));
    QImage image = m_backend->galleryImage(request, parts.at(1), requestedSize);
    if (size) *size = image.size();
    return image;
}
