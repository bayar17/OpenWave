#include <QGuiApplication>
#include <OpenWave/FontManager.h>
#include <OpenWave/WebRuntime.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QDir>
#include "BrowserData.h"
#include "BrowserInput.h"
#include "BrowserPreferences.h"
#include "BrowserWebApps.h"
#include "OceanHistoryManager.h"
#include "OceanCookieManager.h"
#include "OceanDownloadManager.h"
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/ApplicationChannel.h>

int main(int argc, char *argv[])
{
    OpenWave::WebRuntime::configureGraphics();
    QGuiApplication app(argc, argv);
    OpenWave::FontManager::initialize();
    QFont browserFont = QGuiApplication::font();
    browserFont.setWeight(QFont::ExtraLight);
    QGuiApplication::setFont(browserFont);
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Browser"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ocean-browser"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OceanBrowser/assets/Icon.svg")));
    ApplicationChannel lifecycle(OpenWaveApplications::instanceAddress(QCoreApplication::applicationPid()),
        [&app](const QJsonObject &message) {
            if (message.value("command") != QStringLiteral("quit"))
                return QStringLiteral("Unsupported Browser request.");
            // Let the instance acknowledgement drain before destroying its socket.
            QTimer::singleShot(50, &app, &QCoreApplication::quit);
            return QString();
        });

    QString aboutDirectory = QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/about");
#ifdef OCEAN_BROWSER_SOURCE_ABOUT_DIR
    if (!QDir(aboutDirectory).exists())
        aboutDirectory = QStringLiteral(OCEAN_BROWSER_SOURCE_ABOUT_DIR);
#endif
    qputenv("OCEAN_BROWSER_ABOUT_DIRECTORY", aboutDirectory.toUtf8());

    auto *previews = new BrowserPreviews;
    BrowserData browserData(previews);
    BrowserPreferences preferences(browserData.dataPath(), browserData.userPath());
    OceanHistoryManager history(browserData.dataPath());
    // The existing WPE Qt plugin appends cache/HTTP to this cache-only base.
    qputenv("OCEAN_BROWSER_DATA_DIRECTORY", (browserData.dataPath() + QStringLiteral("/Cache")).toUtf8());
    qputenv("WPE_SHELL_MEDIA_DISK_CACHE_PATH", (browserData.dataPath() + QStringLiteral("/Cache/HTTP")).toUtf8());
    OceanCookieManager cookies(browserData.dataPath(), &preferences);
    if (!cookies.ready()) qWarning("Cookie profile initialization: %s", qPrintable(cookies.error()));
    QObject::connect(&cookies, &OceanCookieManager::storageError, &app,
                     [](const QString &error) { qWarning("Cookie profile: %s", qPrintable(error)); });
    OceanDownloadManager downloads(browserData.dataPath(), browserData.userPath(), &preferences);
    qputenv("MESA_SHADER_CACHE_DIR", (browserData.dataPath() + QStringLiteral("/Cache/GPUCache")).toUtf8());
    WallpaperClient wallpaper;
    BrowserInput input;
    BrowserWebApps webApps;
    QQmlApplicationEngine engine;
    if (!OpenWave::WebRuntime::initialize(&engine)) return 1;
    engine.addImageProvider(QStringLiteral("browserpreviews"), previews);
    engine.rootContext()->setContextProperty(QStringLiteral("BrowserData"), &browserData);
    engine.rootContext()->setContextProperty(QStringLiteral("BrowserInput"), &input);
    engine.rootContext()->setContextProperty(QStringLiteral("WebApps"), &webApps);
    engine.rootContext()->setContextProperty(QStringLiteral("BrowserPreferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("History"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("Cookies"), &cookies);
    engine.rootContext()->setContextProperty(QStringLiteral("Downloads"), &downloads);
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
        engine.setInitialProperties(QVariantMap{{QStringLiteral("initialUrl"), arguments.at(1)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("OceanBrowser", "Main");
    return app.exec();
}
