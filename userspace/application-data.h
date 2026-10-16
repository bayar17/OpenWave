#pragma once

#include "application-id.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <pwd.h>
#include <unistd.h>

inline QString openwaveApplicationBundlePath()
{
    const QDir binary(QCoreApplication::applicationDirPath());
    const QString root = QDir::cleanPath(binary.filePath(QStringLiteral("..")));
    return QFileInfo::exists(root + QStringLiteral("/manifest.json")) ? root : QString();
}

inline QString openwaveApplicationId()
{
    const QString bundle = openwaveApplicationBundlePath();
    if (!bundle.isEmpty()) {
        QFile manifest(bundle + QStringLiteral("/manifest.json"));
        if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536) return {};
        const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
        return object.value(QStringLiteral("schemaVersion")).toInt() == 1
            ? object.value(QStringLiteral("id")).toString() : QString();
    }
    QString desktop = QGuiApplication::desktopFileName();
    if (!desktop.endsWith(QStringLiteral(".desktop"))) desktop += QStringLiteral(".desktop");
    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktop);
#ifdef OCEAN_BROWSER_DESKTOP_FILE
    if (path.isEmpty()) path = QStringLiteral(OCEAN_BROWSER_DESKTOP_FILE);
#endif
    if (path.isEmpty()) return {};
    QSettings entry(path, QSettings::IniFormat);
    return openwaveCanonicalApplicationId(entry.value(QStringLiteral("Desktop Entry/X-OpenWave-AppId")).toString());
}

inline QString openwaveUserDataPath(const QString &usersPath = QStringLiteral("/Data/Users"),
                                   QString userName = {})
{
    if (userName.isEmpty()) {
        userName = qEnvironmentVariable("OPENWAVE_SESSION_USER");
        if (userName.isEmpty())
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

inline QString openwaveApplicationConfigPath(const QString &id)
{ const auto root = openwaveApplicationDataPath(id); return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("Config")); }
inline QString openwaveApplicationCachePath(const QString &id)
{ const auto root = openwaveApplicationDataPath(id); return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("Cache")); }
inline QString openwaveApplicationStoragePath(const QString &id)
{ const auto root = openwaveApplicationDataPath(id); return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("Data")); }
