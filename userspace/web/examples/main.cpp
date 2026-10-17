#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <OpenWave/WebRuntime.h>

int main(int argc, char **argv)
{
    OpenWave::WebRuntime::configureGraphics();
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    if (!OpenWave::WebRuntime::initialize(&engine)) return 1;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("WebExample", "Main");
    return app.exec();
}
