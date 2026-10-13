#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QFileInfo>
#include <QTimer>
#include "FilesBackend.h"
#include "MediaPlayerBackend.h"
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/OpenWavePromptClient.h>
#include <QJsonArray>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Files"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenWave"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ocean-files"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OceanFiles/assets/icon.svg")));

    FilesBackend files;
    OpenWavePromptClient prompts;
    MediaPlayerBackend media(files.rootPath());
    WallpaperClient wallpaper;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Wallpaper"), &wallpaper);
    engine.setInitialProperties({
        {QStringLiteral("filesBackend"), QVariant::fromValue(&files)},
        {QStringLiteral("mediaBackend"), QVariant::fromValue(&media)},
        {QStringLiteral("promptClient"), QVariant::fromValue(&prompts)}
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OceanFiles", "Main");
    const auto openPath = [&files](const QString &path) {
        const QFileInfo info(path);
        if (info.isSymbolicLink() || !info.exists()) return QStringLiteral("The file is unavailable.");
        if (!files.goToDirectory(info.isDir() ? info.absoluteFilePath() : info.absolutePath()))
            return QStringLiteral("The path is outside this user's Files directory.");
        files.closePreview();
        files.setSelectionMode(false);
        if (info.isFile()) for (int i = 0; i < files.rowCount(); ++i)
            if (files.entryPath(i) == info.canonicalFilePath()) { files.activate(i); break; }
        return QString();
    };
    ApplicationChannel lifecycle(OpenWaveApplications::instanceAddress(QCoreApplication::applicationPid()),
        [&app, openPath](const QJsonObject &message) {
            if (message.value("command") == QStringLiteral("quit")) {
                // Let the instance acknowledgement drain before destroying its socket.
                QTimer::singleShot(50, &app, &QCoreApplication::quit);
                return QString();
            }
            const QJsonArray arguments = message.value("arguments").toArray();
            if (message.value("command") != QStringLiteral("activate") || arguments.size() != 1
                || !arguments.first().isString()) return QStringLiteral("Invalid Files request.");
            return openPath(arguments.first().toString());
        });
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) QTimer::singleShot(0, &files,
        [openPath, path = arguments.at(1)] { openPath(path); });

    return app.exec();
}
