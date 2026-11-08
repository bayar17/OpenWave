#include <QGuiApplication>
#include "../../userspace/openrc/qa/PortalSettingsHarness.h"
#include "../../userspace/openrc/qa/WifiSettingsHarness.h"
#include "../../userspace/openrc/qa/KeyboardHarness.h"
#include "../../userspace/openrc/qa/SettingsPowerHarness.h"
#include <OpenWave/Application.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "SettingsBackend.h"
#include "ApplicationsBackend.h"
#include "../../userspace/openrc/qa/SettingsUninstallHarness.h"
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
    OpenWave::Application::initialize(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.rootContext()->setContextProperty(QStringLiteral("OpenWavePortals"), &portals);
    engine.setInitialProperties({
        {QStringLiteral("settingsBackend"), QVariant::fromValue(&settings)},
        {QStringLiteral("connectivityBackend"), QVariant::fromValue(&connectivity)},
        {QStringLiteral("timeBackend"), QVariant::fromValue(&time)}
        ,{QStringLiteral("promptClient"), QVariant::fromValue(&prompts)}
        ,{QStringLiteral("portalClient"), QVariant::fromValue(&portals)}
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
    installPortalSettingsHarness(&app, &engine, &portals, &wallpaper);
    installWifiSettingsHarness(&app, &connectivity);
    installKeyboardHarness(&app, &engine, false);
    installSettingsPowerHarness(&app, &engine);
    installSettingsUninstallHarness(&app, &engine, &applications);

    return app.exec();
}
