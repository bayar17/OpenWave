#include "TimeBackend.h"

#include <QMetaObject>
#include <QTimeZone>
#include <cstdlib>
#include <ctime>
#undef signals
#include <gio/gio.h>

namespace {
constexpr auto busName = "org.openwave.Time";
constexpr auto objectPath = "/org/openwave/Time";
constexpr auto interfaceName = "org.openwave.Time1";

GVariant *call(GDBusConnection *bus, const char *method, GVariant *arguments,
               QString *error)
{
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        bus, busName, objectPath, interfaceName, method, arguments, nullptr,
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &failure);
    if (failure) {
        *error = QString::fromUtf8(failure->message);
        g_error_free(failure);
    }
    return reply;
}
}

TimeBackend::TimeBackend(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    m_refreshTimer.setInterval(5000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &TimeBackend::refresh);
    m_refreshTimer.start();
    refresh();
}

TimeBackend::~TimeBackend()
{
    m_pool.waitForDone();
}

void TimeBackend::refresh()
{
    if (!m_inFlight)
        start({});
}

void TimeBackend::setTimezone(const QString &zone)
{
    if (!m_available || !m_zones.contains(zone)) {
        emit errorOccurred(QStringLiteral("Select a listed time zone."));
        return;
    }
    if (m_inFlight) {
        m_pendingZone = zone;
        return;
    }
    start(zone);
}

void TimeBackend::start(const QString &zone)
{
    m_inFlight = true;
    m_busy = !zone.isEmpty();
    emit changed();
    const bool fetchZones = m_zones.isEmpty();
    m_pool.start([this, zone, fetchZones] {
        const Result result = execute(zone, fetchZones);
        QMetaObject::invokeMethod(this, [this, result, zone] {
            m_inFlight = false;
            m_busy = false;
            m_available = result.available;
            if (!result.timezone.isEmpty() && result.timezone != m_timezone) {
                m_timezone = result.timezone;
                const QByteArray name = m_timezone.toUtf8();
                ::setenv("TZ", name.constData(), 1);
                ::tzset();
            }
            if (!result.zones.isEmpty()) m_zones = result.zones;
            emit changed();
            if (!result.error.isEmpty() && !zone.isEmpty())
                emit errorOccurred(result.error);
            if (!m_pendingZone.isEmpty()) {
                const QString pending = m_pendingZone;
                m_pendingZone.clear();
                start(pending);
            }
        }, Qt::QueuedConnection);
    });
}

TimeBackend::Result TimeBackend::execute(const QString &zone, bool fetchZones)
{
    Result result;
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        if (failure) g_error_free(failure);
        if (!zone.isEmpty()) result.error = QStringLiteral("Time service is unavailable.");
        return result;
    }
    if (!zone.isEmpty()) {
        GVariant *reply = call(bus, "SetTimezone", g_variant_new("(s)", zone.toUtf8().constData()),
                               &result.error);
        if (reply) g_variant_unref(reply);
    }
    GVariant *reply = call(bus, "GetTimezone", nullptr, &result.error);
    if (reply) {
        const char *name = nullptr;
        g_variant_get(reply, "(&s)", &name);
        result.timezone = QString::fromUtf8(name);
        result.available = true;
        g_variant_unref(reply);
    }
    if (result.available && fetchZones) {
        reply = call(bus, "ListTimezones", nullptr, &result.error);
        if (reply) {
            GVariant *list = g_variant_get_child_value(reply, 0);
            GVariantIter iter;
            const char *name = nullptr;
            g_variant_iter_init(&iter, list);
            while (g_variant_iter_next(&iter, "&s", &name))
                result.zones.append(QString::fromUtf8(name));
            g_variant_unref(list);
            g_variant_unref(reply);
        }
    }
    g_object_unref(bus);
    return result;
}
