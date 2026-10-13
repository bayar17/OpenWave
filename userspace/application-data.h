#pragma once

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <pwd.h>
#include <unistd.h>

// Resolve the installed application's identity rather than its display name.
inline QString openwaveApplicationId()
{
    QString desktop = QGuiApplication::desktopFileName();
    if (!desktop.endsWith(QStringLiteral(".desktop"))) desktop += QStringLiteral(".desktop");
    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktop);
#ifdef OCEAN_BROWSER_DESKTOP_FILE
    if (path.isEmpty()) path = QStringLiteral(OCEAN_BROWSER_DESKTOP_FILE);
#endif
    if (path.isEmpty()) return {};
    QSettings entry(path, QSettings::IniFormat);
    return entry.value(QStringLiteral("Desktop Entry/X-OpenWave-AppId")).toString();
}

inline QString openwaveUserDataPath(const QString &usersPath = QStringLiteral("/Data/Users"),
                                   QString userName = {})
{
    if (userName.isEmpty()) {
        if (const passwd *user = getpwuid(geteuid())) userName = QString::fromLocal8Bit(user->pw_name);
    }
    static const QRegularExpression segment(QStringLiteral("^[a-zA-Z0-9_.-]{1,128}$"));
    if (!segment.match(userName).hasMatch() || userName == "." || userName == "..") return {};
    return QDir(usersPath).filePath(userName);
}

inline QString openwaveApplicationDataPath(const QString &applicationId,
                                          const QString &usersPath = QStringLiteral("/Data/Users"),
                                          QString userName = {})
{
    static const QRegularExpression segment(QStringLiteral("^[a-zA-Z0-9_.-]{1,128}$"));
    const QString user = openwaveUserDataPath(usersPath, userName);
    if (user.isEmpty() || !segment.match(applicationId).hasMatch()
        || applicationId == "." || applicationId == "..") return {};
    return QDir(user).filePath(QStringLiteral(".DataLocation/Applications/") + applicationId);
}
