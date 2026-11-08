#include <QGuiApplication>
#include <QDebug>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QTimer>
#include <OpenWave/FontManager.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "AppController.h"
#include "LockController.h"
#include "PowerController.h"
#include "TileWallpaperItem.h"
#include "PageEdgeColor.h"
#include <OpenWave/QuickMenuBackend.h>
#include "PortalFileChooserBackend.h"
#include "../../userspace/openrc/qa/PortalShellHarness.h"
#include "../../userspace/openrc/qa/PowerShellHarness.h"
#include "../../userspace/openrc/qa/KeyboardHarness.h"
#include "../../userspace/openrc/qa/BrowserActionsShellHarness.h"
#include "../OceanFiles/FilesBackend.h"
#include <OpenWave/TimeBackend.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/PromptRendererBackend.h>
#ifdef OPENWAVE_PERMISSION_TESTS
#include "../../userspace/permissiond/tests/ShellHarness.h"
#endif

namespace {
class OceanInputProbe final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const auto *window = qobject_cast<QQuickWindow *>(watched);
            const qreal devicePixelRatio = window ? window->devicePixelRatio() : 1.0;
            qInfo().noquote() << "OCEAN_QA pointer"
                              << (event->type() == QEvent::MouseButtonPress ? "press" : "release")
                              << "position=" << mouse->position()
                              << "devicePixelRatio=" << devicePixelRatio
                              << "physicalPosition=" << mouse->position() * devicePixelRatio
                              << "window=" << watched->objectName();
        }
        return false;
    }
};
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const bool qaGraphicsDiagnostics = qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1");
    if (qaGraphicsDiagnostics)
        qInfo().noquote() << "OCEAN_QA qpa-platform=" << QGuiApplication::platformName();
    OpenWave::FontManager::initialize();
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Launcher"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));

    AppController controller;
    LockController lockController;
    PowerController powerController;
    QuickMenuBackend quickMenuBackend;
    FilesBackend portalFilesBackend;
    PortalFileChooserBackend portalBackend;
    PromptRendererBackend promptBackend;
    TimeBackend timeBackend;
    WallpaperClient wallpaper;
    QQmlApplicationEngine engine;
    qmlRegisterType<PageEdgeColor>("OpenWave.Shell", 1, 0, "PageEdgeColor");
    qmlRegisterType<TileWallpaperItem>("OpenWave.Shell", 1, 0, "TileWallpaperItem");
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.rootContext()->setContextProperty(QStringLiteral("qaGraphicsDiagnostics"), qaGraphicsDiagnostics);
    const QString qaWallpaperOverride = qaGraphicsDiagnostics
        ? qEnvironmentVariable("OPENWAVE_QA_WALLPAPER_IMAGE") : QString();
    engine.rootContext()->setContextProperty(QStringLiteral("qaWallpaperImageOverride"),
                                              qaWallpaperOverride);
    engine.setInitialProperties({
        {QStringLiteral("appController"), QVariant::fromValue(&controller)},
        {QStringLiteral("lockController"), QVariant::fromValue(&lockController)},
        {QStringLiteral("powerController"), QVariant::fromValue(&powerController)},
        {QStringLiteral("quickMenuBackend"), QVariant::fromValue(&quickMenuBackend)},
        {QStringLiteral("portalBackend"), QVariant::fromValue(&portalBackend)},
        {QStringLiteral("promptBackend"), QVariant::fromValue(&promptBackend)},
        {QStringLiteral("portalFilesBackend"), QVariant::fromValue(&portalFilesBackend)},
        {QStringLiteral("timeBackend"), QVariant::fromValue(&timeBackend)},
        {QStringLiteral("resumeDrawer"), app.arguments().contains(QStringLiteral("--drawer"))},
        {QStringLiteral("startupError"), app.arguments().contains(QStringLiteral("--app-error"))
            ? QStringLiteral("Settings could not run. Please try again.") : QString()}
    });
    engine.addImageProvider(QStringLiteral("openwavegallery"),
        new PortalGalleryImageProvider(&portalBackend));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &controller,
        [&controller](QObject *object, const QUrl &) {
            auto *window = qobject_cast<QQuickWindow *>(object);
            if (!window) return;
            QObject::connect(window, &QQuickWindow::frameSwapped, &controller, [&controller] {
                controller.setGraphicsReady(true);
            }, Qt::QueuedConnection);
            QObject::connect(window, &QQuickWindow::sceneGraphInvalidated, &controller, [&controller] {
                controller.setGraphicsReady(false);
            }, Qt::QueuedConnection);
            QObject::connect(window, &QQuickWindow::sceneGraphError, &controller,
                [&controller](QQuickWindow::SceneGraphError, const QString &message) {
                    qCritical() << "Ocean graphics initialization failed:" << message;
                    controller.setGraphicsReady(false);
                });
        });
    if (qaGraphicsDiagnostics) {
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
            [&controller](QObject *object, const QUrl &url) {
                qInfo().noquote() << "OCEAN_QA qml-object-created url=" << url
                                  << "success=" << (object != nullptr);
                if (!object)
                    return;
                auto *window = qobject_cast<QQuickWindow *>(object);
                if (!window)
                    return;
                auto *probe = new OceanInputProbe(window);
                window->installEventFilter(probe);
                const auto reportWindow = [window, &controller] {
                    qInfo().noquote() << "OCEAN_QA window visible=" << window->isVisible()
                                      << "exposed=" << window->isExposed()
                                      << "size=" << window->size()
                                      << "apps=" << controller.installedApps().size()
                                      << "favorites=" << controller.favoriteApps().size()
                                      << "unpinned=" << controller.unpinnedApps().size();
                };
                QObject::connect(window, &QWindow::visibleChanged, window, reportWindow);
                QObject::connect(window, &QWindow::widthChanged, window, reportWindow);
                QObject::connect(window, &QWindow::heightChanged, window, reportWindow);
                QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, [reportWindow] {
                    qInfo() << "OCEAN_QA scene-graph-initialized";
                    reportWindow();
                });
                QObject::connect(window, &QQuickWindow::frameSwapped, window, [window, reportWindow] {
                    static bool firstFrame = true;
                    if (!firstFrame)
                        return;
                    firstFrame = false;
                    qInfo() << "OCEAN_QA first-frame-swapped";
                    reportWindow();
                });
                QTimer::singleShot(1500, window, reportWindow);
            });
        QObject::connect(&controller, &AppController::registryChanged, &app, [&controller] {
            qInfo() << "OCEAN_QA application-model apps=" << controller.installedApps().size()
                    << "favorites=" << controller.favoriteApps().size()
                    << "unpinned=" << controller.unpinnedApps().size()
                    << "artwork=" << controller.appArtwork().size();
        });
    }
    engine.loadFromModule("OceanLauncher", "Main");
    installPortalShellHarness(&app, &engine, &portalBackend, &portalFilesBackend);
    installPowerShellHarness(&app, &engine, &promptBackend);
    installKeyboardHarness(&app, &engine, true);
    installBrowserActionsShellHarness(&app, &engine, &promptBackend, &portalBackend);
#ifdef OPENWAVE_PERMISSION_TESTS
    installPermissionHarness(&app, &controller, &promptBackend, &portalBackend);
#endif

    return QGuiApplication::exec();
}
