#include "WebAppDefinition.h"
#include "WebAppAppearance.h"
#include <QQmlContext>
#include <OpenWave/WebRuntime.h>
#include <OpenWave/FontManager.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/application-data.h>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTimer>

int main(int argc, char **argv)
{
    OpenWave::WebRuntime::configureGraphics();
    QGuiApplication app(argc, argv);
    const QString bundle = openwaveApplicationBundlePath();
    QFile file(bundle + "/manifest.json");
    if (bundle.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 65536) return 1;
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    const QUrl url = OpenWaveWebApp::address(manifest.value("webApp").toObject().value("url").toString());
    const QString id = manifest.value("id").toString();
    const QString name = manifest.value("name").toString();
    if (url.isEmpty() || id != OpenWaveWebApp::identifier(url) || !OpenWaveWebApp::validName(name)
        || manifest.value("schemaVersion").toInt() != 1
        || manifest.value("executable").toString() != "bin/openwave-webapp") return 1;
    app.setApplicationName(name);
    app.setOrganizationName("OpenWave");
    app.setDesktopFileName(id);
    app.setWindowIcon(QIcon(bundle + "/" + manifest.value("icon").toString()));
    OpenWave::FontManager::initialize();
    const QString data = openwaveApplicationDataPath(id);
    if (data.isEmpty() || !QFileInfo(data).isDir() || QFileInfo(data).isSymLink()
        || QFileInfo(data).ownerId() != geteuid()) return 1;
    const QString profile = data + "/Data/WebKit";
    const QString cache = data + "/Cache/WebKit";
    for (const QString &directory : {profile, profile + "/cookies", cache, cache + "/cache", cache + "/cache/HTTP", cache + "/Media"}) {
        if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)
            || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
    }
    qputenv("OCEAN_BROWSER_PROFILE_DIRECTORY", profile.toUtf8());
    qputenv("OCEAN_BROWSER_DATA_DIRECTORY", cache.toUtf8());
    qputenv("WPE_SHELL_MEDIA_DISK_CACHE_PATH", (cache + "/Media").toUtf8());
    ApplicationChannel lifecycle(OpenWaveApplications::instanceAddress(QCoreApplication::applicationPid()),
        [&app](const QJsonObject &message) {
        if (message.value("command") != "quit") return QStringLiteral("Unsupported web app request.");
        QTimer::singleShot(50, &app, &QCoreApplication::quit);
        return QString();
    });
    WebAppAppearance appearance(url, manifest.value("icon").toString() == "resources/icon.svg");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("WebApp", &appearance);
    if (!OpenWave::WebRuntime::initialize(&engine)) return 1;
    engine.setInitialProperties({{"appName", name}, {"initialUrl", url}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("OpenWaveWebApp", "Main");
    return app.exec();
}
