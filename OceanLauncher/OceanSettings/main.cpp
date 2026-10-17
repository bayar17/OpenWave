#include <QGuiApplication>
#include <OpenWave/FontManager.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "SettingsBackend.h"
#include "ApplicationsBackend.h"
#include <OpenWave/ConnectivityBackend.h>
#include <OpenWave/TimeBackend.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/preferences.h>
#include <OpenWave/OpenWavePromptClient.h>
#include <OpenWave/PermissionClient.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    OpenWave::FontManager::initialize();
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Settings"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ocean-settings"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OceanSettings/assets/icon.png")));

    SettingsBackend settings(openwavePreferencesPath());
    ConnectivityBackend connectivity;
    TimeBackend time;
    WallpaperClient wallpaper;
    OpenWavePortals portals;
    OpenWavePromptClient prompts;
    OpenWave::PermissionClient permissions;
    ApplicationsBackend applications;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.rootContext()->setContextProperty(QStringLiteral("OpenWavePortals"), &portals);
    engine.setInitialProperties({
        {QStringLiteral("settingsBackend"), QVariant::fromValue(&settings)},
        {QStringLiteral("connectivityBackend"), QVariant::fromValue(&connectivity)},
        {QStringLiteral("timeBackend"), QVariant::fromValue(&time)}
        ,{QStringLiteral("promptClient"), QVariant::fromValue(&prompts)}
        ,{QStringLiteral("permissionClient"), QVariant::fromValue(&permissions)}
        ,{QStringLiteral("applicationsBackend"), QVariant::fromValue(&applications)}
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OceanSettings", "Main");

    return app.exec();
}
