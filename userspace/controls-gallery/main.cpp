#include <OpenWave/Application.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ControlsGallery"));
    app.setOrganizationName(QStringLiteral("OpenWave"));
    app.setOrganizationDomain(QStringLiteral("openwave.org"));
    app.setDesktopFileName(QStringLiteral("com.openwave.waterrouge-gallery"));
    QQmlApplicationEngine engine;
    OpenWave::Application::initialize(&engine);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OpenWave.ControlsGallery"), QStringLiteral("Main"));
    return app.exec();
}
