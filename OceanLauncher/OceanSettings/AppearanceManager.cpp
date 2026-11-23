#include "AppearanceManager.h"

#include "QuickMenuAppearance.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <functional>
#undef signals
#include <gio/gio.h>

namespace {

constexpr auto BusName = "org.openwave.Wallpaper";
constexpr auto ObjectPath = "/org/openwave/Wallpaper";
constexpr auto InterfaceName = "org.openwave.Wallpaper1";

QString friendlyError(GError *failure)
{
    if (!failure)
        return QStringLiteral("The wallpaper service is unavailable.");
    return QString::fromUtf8(failure->message);
}

GVariant *call(const char *method, GVariant *parameters, const GVariantType *replyType, QString &error)
{
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        error = friendlyError(failure);
        if (failure)
            g_error_free(failure);
        if (parameters)
            g_variant_unref(g_variant_ref_sink(parameters));
        return nullptr;
    }
    GVariant *reply = g_dbus_connection_call_sync(bus, BusName, ObjectPath, InterfaceName, method,
        parameters, replyType, G_DBUS_CALL_FLAGS_NONE, 15000, nullptr, &failure);
    g_object_unref(bus);
    if (!reply)
        error = friendlyError(failure);
    if (failure)
        g_error_free(failure);
    return reply;
}

}

AppearanceManager::AppearanceManager(QuickMenuAppearance *appearance, QObject *parent)
    : QObject(parent)
    , m_appearance(appearance)
{
    m_pool.setMaxThreadCount(1);
    connect(m_appearance, &QuickMenuAppearance::presetsChanged, this, &AppearanceManager::presetsChanged);
    connect(m_appearance, &QuickMenuAppearance::changed, this, &AppearanceManager::currentChanged);
}

AppearanceManager::~AppearanceManager()
{
    m_pool.waitForDone();
}

QVariantList AppearanceManager::presets() const
{
    return m_appearance->presets();
}

QString AppearanceManager::current() const
{
    return m_appearance->current();
}

void AppearanceManager::refresh()
{
    m_appearance->refresh();
}

bool AppearanceManager::setFavorite(const QString &id, bool favorite)
{
    if (m_appearance->setFavorite(id, favorite))
        return true;
    emit failed(QStringLiteral("favorite"), tr("Could not update your favorites. Please try again."));
    return false;
}

QColor AppearanceManager::accentForHue(double hue) const
{
    return QuickMenuAppearance::accentForHue(hue);
}

QVariantMap AppearanceManager::imageSize(const QUrl &url) const
{
    if (!url.isLocalFile())
        return {};
    const QSize size = QImageReader(url.toLocalFile()).size();
    return {{QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}};
}

void AppearanceManager::run(const QString &operation, std::function<Outcome()> work)
{
    if (m_busy) {
        emit failed(operation, tr("Another change is still in progress."));
        return;
    }
    m_busy = true;
    emit busyChanged();
    m_pool.start([this, operation, work] {
        const Outcome outcome = work();
        QMetaObject::invokeMethod(this, [this, operation, outcome] {
            m_busy = false;
            emit busyChanged();
            m_appearance->refresh();
            if (outcome.error.isEmpty())
                emit finished(operation, outcome.id);
            else
                emit failed(operation, outcome.error);
        }, Qt::QueuedConnection);
    });
}

void AppearanceManager::set(const QString &id)
{
    bool known = false;
    for (const QVariant &item : m_appearance->presets())
        known = known || item.toMap().value(QStringLiteral("id")).toString() == id;
    if (!known || !m_appearance->select(id)) {
        emit failed(QStringLiteral("set"), tr("This appearance is not available."));
        return;
    }
    run(QStringLiteral("set"), [id] {
        Outcome outcome;
        outcome.id = id;
        GVariant *reply = call("ApplyAppearance", g_variant_new("(s)", id.toUtf8().constData()),
                               G_VARIANT_TYPE("(s)"), outcome.error);
        if (reply)
            g_variant_unref(reply);
        return outcome;
    });
}

QByteArray AppearanceManager::cropToPng(const QUrl &url, double x, double y, double width, double height,
                                        QString &error)
{
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QImage source = reader.read();
    if (source.isNull() || width < 767.5 || height < 1365.5 || x > 0.5 || y > 0.5
        || x + width < 767.5 || y + height < 1365.5) {
        error = QStringLiteral("Choose a valid image and cover the entire crop area.");
        return {};
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
        error = QStringLiteral("Could not encode the wallpaper.");
        return {};
    }
    return png;
}

void AppearanceManager::save(const QString &id, const QString &name, double hue, const QUrl &image,
                             double x, double y, double width, double height)
{
    run(QStringLiteral("save"), [id, name, hue, image, x, y, width, height] {
        Outcome outcome;
        QByteArray png;
        if (!image.isEmpty()) {
            png = cropToPng(image, x, y, width, height, outcome.error);
            if (!outcome.error.isEmpty())
                return outcome;
        }
        GVariant *array = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, png.constData(),
                                                     static_cast<gsize>(png.size()), sizeof(guint8));
        GVariant *reply = call("SaveAppearance",
            g_variant_new("(ssd@ay)", id.toUtf8().constData(), name.toUtf8().constData(), hue, array),
            G_VARIANT_TYPE("(s)"), outcome.error);
        if (reply) {
            const char *saved = nullptr;
            g_variant_get(reply, "(&s)", &saved);
            outcome.id = QString::fromUtf8(saved);
            g_variant_unref(reply);
        }
        return outcome;
    });
}

void AppearanceManager::remove(const QString &id)
{
    run(QStringLiteral("remove"), [id] {
        Outcome outcome;
        outcome.id = id;
        GVariant *reply = call("DeleteAppearance", g_variant_new("(s)", id.toUtf8().constData()),
                               nullptr, outcome.error);
        if (reply)
            g_variant_unref(reply);
        return outcome;
    });
}
