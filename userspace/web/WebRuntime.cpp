#include "WebRuntime.h"
#include "WebInput.h"
#include <QGuiApplication>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#undef signals
#include <wpe/webkit.h>

namespace OpenWave {

WebRuntime::WebRuntime(QObject *parent) : QObject(parent) { initialize(); }

void WebRuntime::configureGraphics()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
}

bool WebRuntime::initialize(QQmlEngine *engine)
{
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app || QThread::currentThread() != app->thread()
        || (engine && engine->thread() != app->thread())) {
        qWarning("Initialize OpenWave Web on the GUI thread after QGuiApplication.");
        return false;
    }
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
        qWarning("Call OpenWave::WebRuntime::configureGraphics() before creating Qt Quick windows.");
        return false;
    }
    if (engine) {
        engine->addImportPath(QStringLiteral("/usr/qml"));
        engine->addImportPath(QStringLiteral("/usr/lib64/qt6/qml"));
    }
    static QPointer<QTimer> pump;
    if (!pump) {
        pump = new QTimer(app);
        pump->setInterval(8);
        connect(pump, &QTimer::timeout, app, [] {
            for (int i = 0; i < 64 && g_main_context_pending(nullptr); ++i)
                g_main_context_iteration(nullptr, FALSE);
        });
        connect(app, &QCoreApplication::aboutToQuit, pump, &QTimer::stop);
        pump->start();
    }
    return true;
}

bool WebRuntime::attachView(QObject *view)
{
    if (!initialize() || !view || view->thread() != thread()) return false;
    WebKitWebView *native = nullptr;
    if (!QMetaObject::invokeMethod(view, "webView", Qt::DirectConnection,
        QGenericReturnArgument("WebKitWebView*", &native)) || !native) return false;
    WebInput input;
    input.attachView(view);
    return true;
}

}
