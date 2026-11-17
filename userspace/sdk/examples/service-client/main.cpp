#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <OpenWave/OpenWavePromptClient.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/TimeBackend.h>
#include <OpenWave/ConnectivityBackend.h>
#include <OpenWave/QuickMenuBackend.h>
#include <OpenWave/application-data.h>

// No service implementation or D-Bus code is compiled into the application.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    OpenWavePromptClient prompts;
    OpenWavePortals chooser;
    WallpaperClient wallpaper;
    TimeBackend time;
    ConnectivityBackend connectivity;
    QuickMenuBackend controls;
    ApplicationChannel channel(OpenWaveApplications::instanceAddress(app.applicationPid()),
        [&app](const QJsonObject &message) {
            if (message.value("command") == "quit") app.quit();
            return QString();
        });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Prompts", &prompts);
    engine.rootContext()->setContextProperty("Chooser", &chooser);
    engine.rootContext()->setContextProperty("Time", &time);
    engine.loadData(R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    visible: true; width: 768; height: 1366; title: "SDK Example"
    Column {
        anchors.centerIn: parent; spacing: 24
        Label { text: "Time zone: " + Time.timezone }
        Button { text: "Show prompt"; onClicked: Prompts.message("SDK Example", "Native OpenWave prompt") }
        Button { text: "Select file"; onClicked: Chooser.openFile() }
    }
})");
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
