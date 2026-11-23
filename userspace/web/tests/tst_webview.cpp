#include <OpenWave/WebRuntime.h>
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>

class WebViewTests : public QObject
{
    Q_OBJECT
    QQmlApplicationEngine engine;
    QQuickItem *view = nullptr;
    QQuickWindow *window = nullptr;
    QTcpServer server;
    QString address(const QString &path) const {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path);
    }
    void navigate(const QString &path) { view->setProperty("url", QUrl(address(path))); }
private slots:
    void initTestCase() {
        QVERIFY(OpenWave::WebRuntime::initialize(&engine));
        QVERIFY(OpenWave::WebRuntime::initialize(&engine));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                    socket->setProperty("handled", true);
                    const QByteArray path = request.split(' ').value(1);
                    if (path == "/wait") return;
                    if (path == "/redirect") {
                        socket->write("HTTP/1.1 302 Found\r\nLocation: /second\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else {
                        const auto title = path == "/second" ? "Second" : "First";
                        const QByteArray html = QByteArray("<!doctype html><html><head><title>") + title
                            + "</title></head><body><button id='tap' onclick='this.textContent=42'>Tap</button></body></html>";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                            + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
        engine.loadData(R"QML(
import QtQuick
import QtQuick.Window
import OpenWave.Web 1.0
Window {
    id: window
    width: 544; height: 900; visible: true
    property int completions: 0
    property int starts: 0
    property bool success: false
    property string scriptResult: ""
    function evaluate(script) {
        page.runJavaScript(script, function(value) { window.scriptResult = String(value) })
    }
    WebView {
        id: page; objectName: "page"; anchors.fill: parent
        onWebViewCreated: window.scriptResult = "created"
        onLoadingChanged: function(request) { if (request.status === 0) window.starts++ }
        onLoadFinished: function(ok, address, error) { window.success = ok; window.completions++ }
    }
}
)QML");
        QVERIFY(!engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        view = window->findChild<QQuickItem *>(QStringLiteral("page"));
        QVERIFY(view);
        QTRY_VERIFY_WITH_TIMEOUT(view->property("ready").toBool(), 15000);
        QVERIFY(view->property("privateBrowsing").toBool());
        QCOMPARE(window->property("scriptResult").toString(), QStringLiteral("created"));
    }
    void navigationAndJavaScript() {
        navigate("first");
        QTRY_VERIFY_WITH_TIMEOUT(window->property("completions").toInt() > 0, 10000);
        QVERIFY(window->property("success").toBool());
        QCOMPARE(view->property("title").toString(), QStringLiteral("First"));
        QVERIFY(!view->property("navigationLoading").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "evaluate", Q_ARG(QVariant, "document.title + ':' + (6*7)")));
        QTRY_COMPARE(window->property("scriptResult").toString(), QStringLiteral("First:42"));
        const int completed = window->property("completions").toInt();
        navigate("second");
        QTRY_VERIFY(window->property("completions").toInt() > completed);
        QTRY_COMPARE(view->property("title").toString(), QStringLiteral("Second"));
        QVERIFY(view->property("canGoBack").toBool());
        QVERIFY(QMetaObject::invokeMethod(view, "goBack"));
        QTRY_COMPARE(view->property("title").toString(), QStringLiteral("First"));
        QVERIFY(QMetaObject::invokeMethod(view, "goForward"));
        QTRY_COMPARE(view->property("title").toString(), QStringLiteral("Second"));
    }
    void redirectIsOneNavigation() {
        const int completed = window->property("completions").toInt();
        const int starts = window->property("starts").toInt();
        navigate("redirect");
        QTRY_VERIFY(window->property("completions").toInt() > completed);
        QVERIFY(window->property("success").toBool());
        QCOMPARE(window->property("starts").toInt(), starts + 1);
        QCOMPARE(window->property("completions").toInt(), completed + 1);
        QVERIFY(view->property("url").toUrl().path().endsWith("second"));
    }
    void cancelAndFailureClearLoading() {
        int completed = window->property("completions").toInt();
        navigate("wait");
        QTRY_VERIFY(view->property("navigationLoading").toBool());
        QVERIFY(QMetaObject::invokeMethod(view, "stop"));
        QTRY_VERIFY(!view->property("navigationLoading").toBool());
        QTRY_COMPARE(window->property("completions").toInt(), completed + 1);
        QVERIFY(!window->property("success").toBool());
        QVERIFY(view->property("errorText").toString().isEmpty());
        completed = window->property("completions").toInt();
        view->setProperty("url", QUrl("http://127.0.0.1:1/unreachable"));
        QTRY_VERIFY(window->property("completions").toInt() > completed);
        QVERIFY(!view->property("navigationLoading").toBool());
        QVERIFY(!window->property("success").toBool());
        QVERIFY(!view->property("errorText").toString().isEmpty());
        completed = window->property("completions").toInt();
        navigate("first");
        QTRY_VERIFY(window->property("completions").toInt() > completed);
        QVERIFY(window->property("success").toBool());
        QVERIFY(view->property("errorText").toString().isEmpty());
    }
};

int main(int argc, char **argv)
{
    OpenWave::WebRuntime::configureGraphics();
    QGuiApplication app(argc, argv);
    WebViewTests tests;
    const int result = QTest::qExec(&tests, argc, argv);
    QFile report(qEnvironmentVariable("HOME") + QStringLiteral("/Data/web-result"));
    if (report.open(QIODevice::WriteOnly)) report.write(QByteArray::number(result));
    return result;
}
#include "tst_webview.moc"
