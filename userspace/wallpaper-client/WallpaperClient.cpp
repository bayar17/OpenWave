#include "WallpaperClient.h"

#include <QBuffer>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QUrl>
#undef signals
#include <gio/gio.h>

namespace {
constexpr auto busName = "org.openwave.Wallpaper";
constexpr auto objectPath = "/org/openwave/Wallpaper";
constexpr auto interfaceName = "org.openwave.Wallpaper1";

GVariant *call(const char *method, GVariant *arguments, QString *error)
{
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        *error = failure ? QString::fromUtf8(failure->message)
                         : QStringLiteral("Wallpaper service unavailable");
        g_clear_error(&failure);
        return nullptr;
    }
    GVariant *reply = g_dbus_connection_call_sync(bus, busName, objectPath,
        interfaceName, method, arguments, nullptr, G_DBUS_CALL_FLAGS_NONE,
        5000, nullptr, &failure);
    if (failure) {
        *error = QString::fromUtf8(failure->message);
        g_clear_error(&failure);
    }
    g_object_unref(bus);
    return reply;
}

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString();
}
}

WallpaperClient::WallpaperClient(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
    m_refreshTimer.setInterval(2000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &WallpaperClient::refresh);
    m_refreshTimer.start();
    refresh();
}

WallpaperClient::~WallpaperClient()
{
    m_pool.waitForDone();
}

WallpaperClient::Result WallpaperClient::getCurrent()
{
    Result result;
    GVariant *reply = call("GetCurrent", nullptr, &result.error);
    if (reply) {
        const char *path = nullptr;
        const char *hash = nullptr;
        g_variant_get(reply, "(&s&s)", &path, &hash);
        result.path = QString::fromUtf8(path);
        result.hash = QString::fromUtf8(hash);
        g_variant_unref(reply);
    }
    return result;
}

void WallpaperClient::refresh()
{
    if (m_refreshing || m_busy) return;
    m_refreshing = true;
    m_pool.start([this] {
        const Result result = getCurrent();
        QMetaObject::invokeMethod(this, [this, result] {
            m_refreshing = false;
            const QUrl source = result.path.isEmpty() ? QUrl() : QUrl::fromLocalFile(result.path);
            if (source != m_source || result.hash != m_hash) {
                m_source = source;
                m_hash = result.hash;
                emit changed();
            }
        }, Qt::QueuedConnection);
    });
}

QVariantMap WallpaperClient::imageSize(const QUrl &url) const
{
    const QString path = localPath(url);
    if (path.isEmpty()) return {};
    const QSize size = QImageReader(path).size();
    return {{QStringLiteral("width"), size.width()},
            {QStringLiteral("height"), size.height()}};
}

WallpaperClient::Result WallpaperClient::saveCrop(const QUrl &url, double x, double y,
                                                  double width, double height)
{
    Result result;
    const QString path = localPath(url);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage source = reader.read();
    if (source.isNull() || width < 767.5 || height < 1365.5 || x > 0.5 || y > 0.5
            || x + width < 767.5 || y + height < 1365.5) {
        result.error = QStringLiteral("Choose a valid image and cover the entire crop area.");
        return result;
    }
    QImage output(768, 1366, QImage::Format_RGB32);
    output.fill(Qt::black);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(x, y, width, height), source);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !output.save(&buffer, "PNG")) {
        result.error = QStringLiteral("Could not encode the cropped wallpaper.");
        return result;
    }
    GVariant *array = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        png.constData(), static_cast<gsize>(png.size()), sizeof(guint8));
    GVariant *reply = call("SetWallpaper", g_variant_new("(@ay)", array), &result.error);
    if (reply) {
        const char *hash = nullptr;
        g_variant_get(reply, "(&s)", &hash);
        result.hash = QString::fromUtf8(hash);
        g_variant_unref(reply);
    }
    return result;
}

void WallpaperClient::setCroppedWallpaper(const QUrl &url, double x, double y,
                                           double width, double height)
{
    if (m_busy) return;
    m_busy = true;
    emit changed();
    m_pool.start([this, url, x, y, width, height] {
        const Result result = saveCrop(url, x, y, width, height);
        QMetaObject::invokeMethod(this, [this, result] {
            m_busy = false;
            emit changed();
            if (result.hash.isEmpty())
                emit errorOccurred(result.error.isEmpty()
                    ? QStringLiteral("Could not save wallpaper.") : result.error);
            else {
                refresh();
                emit saved();
            }
        }, Qt::QueuedConnection);
    });
}
