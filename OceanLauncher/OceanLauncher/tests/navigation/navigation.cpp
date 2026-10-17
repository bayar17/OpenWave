#include <QGuiApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <QtWaylandCompositor/QWaylandCompositor>
#include <QtWaylandCompositor/QWaylandOutput>
#include <QtWaylandCompositor/QWaylandOutputMode>
#include <QtWaylandCompositor/QWaylandSeat>
#include <QtWaylandCompositor/QWaylandSurface>
#include <QtWaylandCompositor/QWaylandXdgShell>

class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList photos READ empty CONSTANT)
    Q_PROPERTY(QVariantList albums READ empty CONSTANT)
    Q_PROPERTY(QVariantList supportedFormats READ empty CONSTANT)
    Q_PROPERTY(QVariantList supportedMimeTypes READ empty CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
public:
    QVariantList empty() const { return {}; }
    QString error() const { return {}; }
signals:
    void changed();
};

class NavigationTests : public QObject
{
    Q_OBJECT
private slots:
    void rootBackRequestsMinimizeAndKeepsProcessAlive()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        QWaylandCompositor compositor;
        compositor.setSocketName("openwave-navigation-test");
        compositor.setUseHardwareIntegrationExtension(false);
        QWaylandXdgShell shell(&compositor);
        compositor.create();
        QWaylandOutput output(&compositor, nullptr);
        QWaylandOutputMode mode(QSize(768, 1366), 60000);
        output.addMode(mode, true);
        output.setCurrentMode(mode);
        compositor.setDefaultOutput(&output);
        QWaylandXdgToplevel *toplevel = nullptr;
        QWaylandSurface *surface = nullptr;
        connect(&shell, &QWaylandXdgShell::toplevelCreated, this,
            [&](QWaylandXdgToplevel *top, QWaylandXdgSurface *xdg) {
                toplevel = top;
                surface = xdg->surface();
                top->sendMaximized(QSize(768, 1366));
            });
        QProcess client;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "wayland");
        env.insert("QT_QUICK_BACKEND", "software");
        env.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
        env.insert("WAYLAND_DISPLAY", compositor.socketName());
        client.setProcessEnvironment(env);
        const QString loader = qEnvironmentVariable("OPENWAVE_TEST_LOADER");
        const QString executable = QFileInfo(QCoreApplication::arguments().first()).absoluteFilePath();
        if (loader.isEmpty()) client.start(executable, {"--gallery"});
        else client.start(loader, {"--library-path", qEnvironmentVariable("OPENWAVE_TEST_LIBRARY_PATH"),
                                  executable, "--gallery"});
        QVERIFY(client.waitForStarted());
        QTRY_VERIFY2_WITH_TIMEOUT(toplevel && surface && surface->hasContent(),
                                 client.readAllStandardError().constData(), 10000);
        QSignalSpy minimized(toplevel, &QWaylandXdgToplevel::setMinimized);
        QVERIFY(compositor.defaultSeat()->setKeyboardFocus(surface));
        compositor.processWaylandEvents();
        QTest::qWait(100);
        compositor.defaultSeat()->sendKeyEvent(Qt::Key_Back, true);
        compositor.defaultSeat()->sendKeyEvent(Qt::Key_Back, false);
        compositor.processWaylandEvents();
        QTRY_COMPARE_WITH_TIMEOUT(minimized.count(), 1, 5000);
        QCOMPARE(client.state(), QProcess::Running);
        const qint64 pid = client.processId();
        QVERIFY(surface->hasContent());
        toplevel->sendMaximized(QSize(768, 1366));
        QVERIFY(compositor.defaultSeat()->setKeyboardFocus(surface));
        compositor.processWaylandEvents();
        QTest::qWait(100);
        compositor.defaultSeat()->sendKeyEvent(Qt::Key_Back, true);
        compositor.defaultSeat()->sendKeyEvent(Qt::Key_Back, false);
        compositor.processWaylandEvents();
        QTRY_COMPARE_WITH_TIMEOUT(minimized.count(), 2, 5000);
        QCOMPARE(client.processId(), pid);
        QCOMPARE(client.state(), QProcess::Running);
        client.terminate();
        QVERIFY(client.waitForFinished(5000));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--gallery")) {
        Library library;
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"library", QVariant::fromValue(&library)}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(GALLERY_SOURCE)));
        if (engine.rootObjects().isEmpty()) return 1;
        return app.exec();
    }
    NavigationTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "navigation.moc"
