#include <QGuiApplication>
#include <OpenWave/FontManager.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "AppController.h"
#include <OpenWave/QuickMenuBackend.h>
#include "PortalFileChooserBackend.h"
#include "../OceanFiles/FilesBackend.h"
#include <OpenWave/TimeBackend.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/PromptRendererBackend.h>
#ifdef OPENWAVE_PERMISSION_TESTS
#include "../../userspace/permissiond/tests/ShellHarness.h"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    OpenWave::FontManager::initialize();
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Launcher"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));

    AppController controller;
    QuickMenuBackend quickMenuBackend;
    FilesBackend portalFilesBackend;
    PortalFileChooserBackend portalBackend;
    PromptRendererBackend promptBackend;
    TimeBackend timeBackend;
    WallpaperClient wallpaper;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.setInitialProperties({
        {QStringLiteral("appController"), QVariant::fromValue(&controller)},
        {QStringLiteral("quickMenuBackend"), QVariant::fromValue(&quickMenuBackend)},
        {QStringLiteral("portalBackend"), QVariant::fromValue(&portalBackend)},
        {QStringLiteral("promptBackend"), QVariant::fromValue(&promptBackend)},
        {QStringLiteral("portalFilesBackend"), QVariant::fromValue(&portalFilesBackend)},
        {QStringLiteral("timeBackend"), QVariant::fromValue(&timeBackend)},
        {QStringLiteral("resumeDrawer"), app.arguments().contains(QStringLiteral("--drawer"))},
        {QStringLiteral("startupError"), app.arguments().contains(QStringLiteral("--app-error"))
            ? QStringLiteral("Settings could not run. Please try again.") : QString()}
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OceanLauncher", "Main");
#ifdef OPENWAVE_PERMISSION_TESTS
    installPermissionHarness(&app, &controller, &promptBackend, &portalBackend);
#endif

    return QGuiApplication::exec();
}
