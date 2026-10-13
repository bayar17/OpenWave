// Run inside QEMU against a fixture URL or a failing website. Never uses a user's profile.
#include <wpe/webkit.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <glib.h>
#include "WPEQtView.h"

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ocean Browser"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ocean-browser"));
    QDir().mkpath(QStringLiteral("/tmp/openwave-engine-media"));
    qputenv("WPE_SHELL_MEDIA_DISK_CACHE_PATH", "/tmp/openwave-engine-media");
    if (argc < 2 || argc > 3) return 2;
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("/usr/lib64/qt6/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("probeUrl"), QUrl(QString::fromUtf8(argv[1])));
    // Optional CSS viewport width reproduces Ocean's mobile viewport selection.
    engine.rootContext()->setContextProperty(QStringLiteral("probeViewportWidth"),
        argc == 3 ? QString::fromUtf8(argv[2]).toDouble() : 0.0);
    engine.loadData(R"QML(
import QtQuick
import QtQuick.Window
import org.wpewebkit.qtwpe 1.0
Window {
    width: 544; height: 960; visible: true; color: "white"
    WPEView {
        id: page; objectName: "probeView"; anchors.fill: parent; privateBrowsing: true
        zoomLevel: probeViewportWidth > 0 ? width / probeViewportWidth : 1
    }
    Timer {
        interval: 5000; running: true; repeat: true
        onTriggered: page.runJavaScript(`JSON.stringify({
            url: location.href, title: document.title,
            viewport: {width: innerWidth, height: innerHeight, dpr: devicePixelRatio},
            images: Array.from(document.images).slice(0,15).map(i => ({
                src:i.currentSrc, complete:i.complete, w:i.naturalWidth, h:i.naturalHeight})),
            media: ['video/mp4; codecs="avc1.42E01E"','video/webm; codecs="vp8"',
                'audio/mp4; codecs="mp4a.40.2"'].map(t=>[t,document.createElement('video').canPlayType?.(t) || '']),
            mse: typeof MediaSource, rtc: typeof RTCPeerConnection,
            fixture: window.engineProbeResult || null
        })`, function(result) { console.log("ENGINE_PROBE " + result) })
    }
}
)QML");
    if (engine.rootObjects().isEmpty()) return 3;
    auto *view = engine.rootObjects().first()->findChild<WPEQtView *>(QStringLiteral("probeView"));
    if (!view) return 4;
    // Log errors from the real WebKit resource requests, rather than guessing from screenshots.
    QTimer ready;
    QObject::connect(&ready, &QTimer::timeout, &app, [&] {
        if (!view->webView()) return;
        ready.stop();
        if (argc == 3) view->setZoomLevel(view->width() / QString::fromUtf8(argv[2]).toDouble());
        g_signal_connect(view->webView(), "resource-load-started", G_CALLBACK(+[](
        WebKitWebView *, WebKitWebResource *resource, WebKitURIRequest *, gpointer) {
        g_signal_connect(resource, "failed", G_CALLBACK(+[](WebKitWebResource *resource,
            GError *error, gpointer) {
            g_printerr("ENGINE_RESOURCE_FAILED %s: %s\n", webkit_web_resource_get_uri(resource), error->message);
        }), nullptr);
        g_signal_connect(resource, "finished", G_CALLBACK(+[](WebKitWebResource *resource, gpointer) {
            auto *response = webkit_web_resource_get_response(resource);
            const char *uri = webkit_web_resource_get_uri(resource);
            if (response && (g_strstr_len(uri, -1, "/wp/") || g_strstr_len(uri, -1, "/mwp/")
                || g_strstr_len(uri, -1, "/dwp") || g_strstr_len(uri, -1, "/img/")
                || webkit_uri_response_get_status_code(response) >= 400))
                g_printerr("ENGINE_RESOURCE_RESPONSE %u %s %s\n",
                    webkit_uri_response_get_status_code(response),
                    webkit_uri_response_get_mime_type(response), uri);
        }), nullptr);
    }), nullptr);
        view->setUrl(QUrl(QString::fromUtf8(argv[1])));
    });
    ready.start(10);
    QTimer pump;
    QObject::connect(&pump, &QTimer::timeout, &app, [] {
        for (int i = 0; i < 64 && g_main_context_pending(nullptr); ++i)
            g_main_context_iteration(nullptr, FALSE);
    });
    pump.start(8);
    QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    return app.exec();
}
