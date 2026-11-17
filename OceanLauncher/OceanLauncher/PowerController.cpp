#include "PowerController.h"
#include <QUuid>
#include <QDebug>
#undef signals
#include <OpenWave/OpenWaveIdentity.h>

void PowerController::ask(const QString &action)
{
    if (m_busy || !m_request.isEmpty() || (action != "poweroff" && action != "reboot")) return;
    const bool reboot = action == "reboot";
    m_action = action;
    m_request = {{"id", QUuid::createUuid().toString()}, {"type", "confirmation"},
        {"title", reboot ? tr("Restart OpenWave") : tr("Shut down OpenWave")},
        {"message", reboot ? tr("Close applications and restart the device?") : tr("Close applications and shut down the device?")},
        {"acceptLabel", reboot ? tr("Restart") : tr("Shut down")}, {"cancelLabel", tr("Cancel")}};
    emit changed();
}

void PowerController::respond(const QString &id, const QString &result, const QString &value)
{
    Q_UNUSED(value)
    if (id.isEmpty() || id != m_request.value("id").toString() || m_busy) return;
    if (result != "accepted" && result != "cancelled") return;
    const QString action = m_action;
    m_request.clear();
    m_busy = result == "accepted";
    emit changed();
    if (!m_busy) return;
    m_pool.start([this, action] {
        GError *error = nullptr;
        auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        char *owner = bus ? openwave_service_owner(bus, "org.openwave.Power", "/run/openwave/powerd.pid", &error) : nullptr;
        auto *reply = owner ? g_dbus_connection_call_sync(bus, owner, "/org/openwave/Power", "org.openwave.Power1",
            action == "reboot" ? "Reboot" : "PowerOff", nullptr, G_VARIANT_TYPE("()"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error) : nullptr;
        const QString message = reply ? QString() : error ? QString::fromUtf8(error->message) : tr("The power service is unavailable.");
        if (reply) g_variant_unref(reply);
        g_free(owner); g_clear_error(&error); if (bus) g_object_unref(bus);
        QMetaObject::invokeMethod(this, [this, message, action] {
            if (!message.isEmpty()) {
                qWarning().noquote() << "Power action failed:" << message;
                m_busy = false; emit changed();
                emit errorOccurred(tr("Power controls are unavailable. Please try again."));
            }
            else qInfo().noquote() << "POWER_UI accepted action=" + action;
        }, Qt::QueuedConnection);
    });
}
