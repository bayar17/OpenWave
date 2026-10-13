#pragma once

#include <QDir>
#include <QFile>
#include <QStandardPaths>

// Shared system preferences, independent of either application's identity.
inline QString openwavePreferencesPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/OpenWave");
    QDir().mkpath(base);
    const QString path = base + QStringLiteral("/settings.ini");
    const QString legacy = base + QStringLiteral("/Ocean Launcher/settings.ini");
    if (!QFile::exists(path) && QFile::exists(legacy) && !QFile::copy(legacy, path))
        return legacy;
    return path;
}
