#include "SettingsEnvironment.h"

#include "SettingsBackend.h"

SettingsEnvironment::SettingsEnvironment(SettingsBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    if (m_backend)
        connect(m_backend, &SettingsBackend::preferencesChanged, this, &SettingsEnvironment::changed);
}

QString SettingsEnvironment::deviceName() const
{
    return m_backend ? m_backend->deviceName() : QString();
}

bool SettingsEnvironment::largeText() const
{
    return m_backend && m_backend->largeText();
}

bool SettingsEnvironment::reduceMotion() const
{
    return m_backend && m_backend->reduceMotion();
}
