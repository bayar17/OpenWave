#include "GalleryLibrary.h"
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>
#ifdef OPENWAVE_PHOTOS_SYSTEM_FONTS
#include <OpenWave/FontManager.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/preferences.h>
#include <OpenWave/application-data.h>
#include <QSettings>
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Gallery"); app.setApplicationVersion("0.1.0");
    app.setOrganizationName("OpenWave"); app.setOrganizationDomain("openwave.org");
    app.setDesktopFileName("com.openwave.photos");
    app.setWindowIcon(QIcon(":/qt/qml/OceanPhotos/assets/icon.svg"));
#ifdef OPENWAVE_PHOTOS_SYSTEM_FONTS
    OpenWave::FontManager::initialize();
#else
    int regular = -1;
    for (const auto &weight : {"Regular", "Light", "ExtraLight"}) {
        const int id = QFontDatabase::addApplicationFont(QString(":/Gallery/fonts/SourceSansPro-%1.otf").arg(weight));
        if (QString(weight) == "Regular") regular = id;
    }
    if (regular >= 0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(regular).value(0)));
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription("Gallery · OpenWave Qt Quick concept");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"directory", "Browse a local folder (up to 1,000 images).", "path"});
    parser.addOption({"sample-library", "Load a concept library JSON file.", "path"});
    parser.addOption({"image", "Open an image in the viewer.", "path"});
    parser.addOption({"concept", "Include a system-bar illustration for standalone previews."});
    parser.addOption({"view", "Initial concept view: photos, albums, or viewer.", "view", "photos"});
    parser.addOption({"capture", "Save the rendered concept to a PNG, then exit.", "path"});
    parser.addOption({"list-formats", "Print the formats provided by installed Qt image decoders."});
    parser.process(app);
#ifdef OPENWAVE_PHOTOS_SYSTEM_FONTS
    const QString dataPath = openwaveApplicationStoragePath(openwaveApplicationId());
    if (dataPath.isEmpty()) { qCritical("Gallery application storage is unavailable."); return 1; }
    GalleryLibrary library(dataPath + QStringLiteral("/Gallery"));
#else
    GalleryLibrary library;
#endif
    if (parser.isSet("list-formats")) { qInfo().noquote() << library.supportedFormats().join(", "); return 0; }
    if (parser.isSet("sample-library") && !library.loadSampleLibrary(parser.value("sample-library"))) return 1;
    if (parser.isSet("directory") && !library.loadFolder(QUrl::fromLocalFile(parser.value("directory")))) return 1;
    int selected = 0;
    if (parser.isSet("image")) {
        selected = library.openImage(QUrl::fromLocalFile(parser.value("image")));
        if (selected < 0) { qWarning().noquote() << library.error(); return 1; }
    }
#ifdef OPENWAVE_PHOTOS_SYSTEM_FONTS
    OpenWavePortals portals;
    WallpaperClient wallpaper;
#endif
    QQmlApplicationEngine engine;
    QVariantMap properties{{"library", QVariant::fromValue(&library)}, {"showStatusBar", parser.isSet("concept")},
        {"initialView", parser.isSet("image") ? QString("viewer") : parser.value("view")},
        {"selectedIndex", selected}, {"captureMode", parser.isSet("capture")}};
#ifdef OPENWAVE_PHOTOS_SYSTEM_FONTS
    properties.insert("portalClient", QVariant::fromValue(&portals));
    properties.insert("wallpaperClient", QVariant::fromValue(&wallpaper));
    properties.insert("reducedMotion", QSettings(openwavePreferencesPath(), QSettings::IniFormat)
        .value(QStringLiteral("accessibility/reduceMotion"), false).toBool());
    ApplicationChannel lifecycle(OpenWaveApplications::instanceAddress(QCoreApplication::applicationPid()),
        [&app](const QJsonObject &message) {
            if (message.value("command") != QStringLiteral("quit")) return QStringLiteral("Unsupported Gallery request.");
            QTimer::singleShot(50, &app, &QCoreApplication::quit);
            return QString();
        });
#endif
    engine.setInitialProperties(properties);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("OceanPhotos", "Main");
    if (parser.isSet("capture")) QTimer::singleShot(1800, &app, [&] {
        auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const bool saved = window && window->grabWindow().save(parser.value("capture"));
        if (!saved) qWarning("Could not capture Gallery.");
        app.exit(saved ? 0 : 1);
    });
    return app.exec();
}
