#include "WebRuntime.h"
#include "WebInput.h"
#include <QDir>
#include <QFileInfo>
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

namespace {
QString engineRoot()
{
    for (const char *name : {"OPENWAVE_WEB_ENGINE_ROOT", "OPENWAVE_BUNDLE_ROOT"}) {
        const QString root = QDir::cleanPath(qEnvironmentVariable(name));
        if (!root.isEmpty() && QFileInfo::exists(root + QStringLiteral("/lib64/libWPEWebKit-2.0.so.1")))
            return root;
    }
    return {};
}

void configureEngine()
{
    static const QString root = engineRoot();
    if (root.isEmpty()) return;
    qputenv("OPENWAVE_WEB_ENGINE_ROOT", root.toUtf8());
    qputenv("WEBKIT_INJECTED_BUNDLE_PATH", (root + QStringLiteral("/plugins/wpe-webkit-2.0/injected-bundle")).toUtf8());
    qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", (root + QStringLiteral("/plugins/gstreamer-1.0")).toUtf8());
    qunsetenv("GST_PLUGIN_PATH_1_0");
    qunsetenv("GST_PLUGIN_PATH");
    qputenv("GST_PLUGIN_SCANNER", (root + QStringLiteral("/sbin/gst-plugin-scanner")).toUtf8());
    const QString cache = qEnvironmentVariable("XDG_CACHE_HOME");
    if (!cache.isEmpty() && QDir().mkpath(cache + QStringLiteral("/gstreamer-1.0")))
        qputenv("GST_REGISTRY_1_0", (cache + QStringLiteral("/gstreamer-1.0/registry.bin")).toUtf8());
    qputenv("GIO_EXTRA_MODULES", (root + QStringLiteral("/plugins/gio/modules")).toUtf8());
    if (QFileInfo::exists(root + QStringLiteral("/share/data/icu")))
        qputenv("ICU_DATA", (root + QStringLiteral("/share/data/icu")).toUtf8());
}
}

void WebRuntime::configureGraphics()
{
    configureEngine();
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
    configureEngine();
    if (engine) {
        const QString root = engineRoot();
        if (!root.isEmpty()) engine->addImportPath(root + QStringLiteral("/qml"));
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
