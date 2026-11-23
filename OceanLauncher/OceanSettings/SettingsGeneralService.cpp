#include "SettingsGeneralService.h"

#include "SettingsBackend.h"

#include <QMetaObject>

SettingsGeneralService::SettingsGeneralService(SettingsBackend *settings, QObject *time,
                                               QObject *connectivity, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_time(time)
    , m_connectivity(connectivity)
{
    if (m_settings) {
        connect(m_settings, &SettingsBackend::preferencesChanged, this, &SettingsGeneralService::changed);
        connect(m_settings, &SettingsBackend::powerStateChanged, this, &SettingsGeneralService::changed);
        connect(m_settings, &SettingsBackend::errorOccurred, this, &SettingsGeneralService::errorOccurred);
    }
    if (m_time) {
        if (m_time->metaObject()->indexOfSignal("changed()") >= 0)
            connect(m_time, SIGNAL(changed()), this, SIGNAL(changed()));
        if (m_time->metaObject()->indexOfSignal("errorOccurred(QString)") >= 0)
            connect(m_time, SIGNAL(errorOccurred(QString)), this, SIGNAL(errorOccurred(QString)));
    }
}

QString SettingsGeneralService::deviceName() const
{
    return m_settings ? m_settings->deviceName() : QString();
}

QVariantMap SettingsGeneralService::systemInfo() const
{
    return m_settings ? m_settings->systemInfo() : QVariantMap();
}

bool SettingsGeneralService::powerActionsAvailable() const
{
    return m_settings && m_settings->powerActionsAvailable();
}

bool SettingsGeneralService::powerBusy() const
{
    return m_settings && m_settings->powerBusy();
}

bool SettingsGeneralService::timeServiceAvailable() const
{
    return m_time && m_time->property("serviceAvailable").toBool();
}

bool SettingsGeneralService::timeBusy() const
{
    return m_time && m_time->property("busy").toBool();
}

QString SettingsGeneralService::timezone() const
{
    return m_time ? m_time->property("timezone").toString() : QString();
}

QStringList SettingsGeneralService::zones() const
{
    return m_time ? m_time->property("zones").toStringList() : QStringList();
}

bool SettingsGeneralService::saveDeviceName(const QString &name)
{
    if (!m_settings || !m_settings->saveDeviceName(name))
        return false;
    if (m_connectivity)
        QMetaObject::invokeMethod(m_connectivity, "setDeviceName", Q_ARG(QString, m_settings->deviceName()));
    return true;
}

bool SettingsGeneralService::setTimezone(const QString &zone)
{
    if (!timeServiceAvailable() || timeBusy() || !zones().contains(zone))
        return false;
    return QMetaObject::invokeMethod(m_time, "setTimezone", Q_ARG(QString, zone));
}

bool SettingsGeneralService::requestPowerAction(const QString &action)
{
    if ((action != QLatin1String("restart") && action != QLatin1String("shutdown")) || !m_settings)
        return false;
    return m_settings->requestPowerAction(action);
}

void SettingsGeneralService::refreshTimeZones()
{
    if (m_time)
        QMetaObject::invokeMethod(m_time, "refresh");
}
