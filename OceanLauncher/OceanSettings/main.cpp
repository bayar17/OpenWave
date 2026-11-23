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
#include "SettingsEnvironment.h"
#include "SettingsGeneralService.h"
#include "QuickMenuAppletManager.h"
#include "QuickMenuAppearance.h"
#include "AppearanceManager.h"
#include "PreferenceBroker.h"
#include <OpenWave/ApplicationsClient.h>
#include "QuickMenuAppletRegistry.h"
#include "../../userspace/openrc/qa/AssociationHarness.h"
#include "../../userspace/openrc/qa/SettingsUninstallHarness.h"
#include "../../userspace/openrc/qa/SettingsWifiModuleHarness.h"
#include "../../userspace/openrc/qa/SettingsQuickMenuHarness.h"
#include <OpenWave/ConnectivityBackend.h>
#include <OpenWave/TimeBackend.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/preferences.h>
#include <OpenWave/OpenWavePromptClient.h>
#include <OpenWave/PermissionClient.h>

int main(int argc, char *argv[])
{
    qputenv("QV4_FORCE_INTERPRETER", "1");
    qputenv("QV4_JIT_CALL_THRESHOLD", "-1");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Settings"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ocean-settings"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OceanSettings/assets/icon.png")));

    SettingsBackend settings(openwavePreferencesPath());
    ApplicationsClient preferenceClient;
    PreferenceBroker preferenceBroker([&preferenceClient](const QString &method, const QVariantList &arguments,
                                                          PreferenceBroker::Completion done) {
        preferenceClient.call(method, arguments, std::move(done));
    });
    if (PreferenceBroker::brokered()) {
        const auto writer = [&preferenceBroker](const QString &key, const QVariant &value) {
            return preferenceBroker.write(key, value);
        };
        settings.setPreferenceWriter(writer);
        QuickMenuAppletRegistry::shared()->setPreferenceWriter(writer);
        QuickMenuAppearance::shared()->setPreferenceWriter(writer);
    }
    ConnectivityBackend connectivity;
    SettingsEnvironment environment(&settings);
    TimeBackend time;
    SettingsGeneralService general(&settings, &time, &connectivity);
    WallpaperClient wallpaper;
    OpenWavePortals portals;
    OpenWavePromptClient prompts;
    OpenWave::PermissionClient permissions;
    ApplicationsBackend applications;
    QuickMenuAppletManager quickMenuApplets(QuickMenuAppletRegistry::shared());
    AppearanceManager appearances(QuickMenuAppearance::shared());
    QQmlApplicationEngine engine;
    OpenWave::Application::initialize(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.rootContext()->setContextProperty(QStringLiteral("OpenWavePortals"), &portals);
    engine.rootContext()->setContextProperty(QStringLiteral("Connectivity"), &connectivity);
    engine.rootContext()->setContextProperty(QStringLiteral("Prompts"), &prompts);
    engine.rootContext()->setContextProperty(QStringLiteral("SettingsEnvironment"), &environment);
    engine.rootContext()->setContextProperty(QStringLiteral("General"), &general);
    engine.rootContext()->setContextProperty(QStringLiteral("QuickMenuApplets"), &quickMenuApplets);
    engine.rootContext()->setContextProperty(QStringLiteral("Appearances"), &appearances);
    engine.setInitialProperties({
        {QStringLiteral("settingsBackend"), QVariant::fromValue(&settings)},
        {QStringLiteral("connectivityBackend"), QVariant::fromValue(&connectivity)}
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
    installWifiModuleHarness(&app, &engine);
    installSettingsQuickMenuHarness(&app, &engine);
    installKeyboardHarness(&app, &engine, false);
    installSettingsPowerHarness(&app, &engine);
    installSettingsUninstallHarness(&app, &engine, &applications);
    installSettingsAssociationHarness(&app, &applications);

    return app.exec();
}
