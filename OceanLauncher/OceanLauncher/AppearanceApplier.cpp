#include "AppearanceApplier.h"

#include <QMetaObject>
#include <QRegularExpression>
#undef signals
#include <gio/gio.h>

AppearanceApplier::AppearanceApplier(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

AppearanceApplier::~AppearanceApplier()
{
    m_pool.waitForDone();
}

void AppearanceApplier::apply(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]{0,31}$"));
    if (m_busy || !pattern.match(id).hasMatch()) {
        if (!m_busy)
            emit failed(id, QStringLiteral("Invalid appearance id."));
        return;
    }
    m_busy = true;
    emit busyChanged();
    m_pool.start([this, id] {
        QString error;
        GError *failure = nullptr;
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
        if (!bus) {
            error = failure ? QString::fromUtf8(failure->message) : QStringLiteral("Wallpaper service unavailable");
        } else {
            GVariant *reply = g_dbus_connection_call_sync(bus, "org.openwave.Wallpaper",
                "/org/openwave/Wallpaper", "org.openwave.Wallpaper1", "ApplyAppearance",
                g_variant_new("(s)", id.toUtf8().constData()), G_VARIANT_TYPE("(s)"),
                G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &failure);
            if (reply)
                g_variant_unref(reply);
            else
                error = failure ? QString::fromUtf8(failure->message) : QStringLiteral("Could not apply the appearance.");
            g_object_unref(bus);
        }
        if (failure)
            g_error_free(failure);
        QMetaObject::invokeMethod(this, [this, id, error] {
            m_busy = false;
            emit busyChanged();
            if (error.isEmpty())
                emit applied(id);
            else
                emit failed(id, error);
        }, Qt::QueuedConnection);
    });
}
