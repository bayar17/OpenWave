#include "../BrowserInput.h"
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonDocument>
#include <QWheelEvent>
#include <QFile>
#include <glib.h>

class InputTests : public QObject {
    Q_OBJECT
private:
    BrowserInput input;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *view = nullptr;
    int sequence = 0;
    QVariant evaluate(const QString &script) {
        const int previous = window->property("serial").toInt();
        QMetaObject::invokeMethod(window, "evaluate", Q_ARG(QVariant, script + "\n//" + QString::number(++sequence)));
        QElapsedTimer timer; timer.start();
        while (window->property("serial").toInt() == previous && timer.elapsed() < 5000)
            QTest::qWait(10);
        return window->property("serial").toInt() > previous ? window->property("result") : QVariant();
    }
    void drag(QPoint start, QPoint end, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::mousePress(window, Qt::LeftButton, modifiers, start);
        for (int i = 1; i <= 12; ++i) {
            const QPoint position = start + (end - start) * i / 12;
            QTest::mouseEvent(QTest::MouseMove, window, Qt::NoButton, modifiers, position, 25);
        }
        QTest::mouseRelease(window, Qt::LeftButton, modifiers, end);
        QTest::qWait(250);
    }
private slots:
    void initTestCase() {
        qputenv("OPENWAVE_BROWSER_TOUCH_EMULATION", "0");
        engine.addImportPath("/usr/lib64/qt6/qml");
        engine.rootContext()->setContextProperty("BrowserInput", &input);
        engine.loadData(R"QML(
import QtQuick
import QtQuick.Window
import org.wpewebkit.qtwpe 1.0
Window {
    id: root
    width: 544; height: 900; visible: true; color: "white"
    property int serial: 0
    property var result
    property bool loaded: false
    function evaluate(script) {
        page.runJavaScript("JSON.stringify(eval(" + JSON.stringify(script) + "))",
            function(value) { root.result = JSON.parse(value); root.serial++ })
    }
    WPEView {
        id: page; objectName: "page"; anchors.fill: parent; privateBrowsing: true
        zoomLevel: width / 500
        onWebViewCreated: {
            BrowserInput.attachView(page)
            page.loadHtml(`<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
                <style>body { margin:0;font:24px sans-serif } #tap { position:fixed;top:10px;left:20px;z-index:2 }
                #nested { position:absolute;top:180px;left:20px;width:300px;height:200px;overflow:auto }
                #nested div { height:1200px;background:linear-gradient(#acf,#fea) }
                #select { position:absolute;top:100px;left:20px }
                .spacer { height:2600px;background:linear-gradient(white,#ddd) }</style></head>
                <body><button id="tap" onclick="window.clicks++">Tap target</button>
                <p id="select">Select this readable sentence.</p><div id="nested"><div>Nested scrolling</div></div>
                <div class="spacer"></div><script>window.clicks=0;window.starts=0;window.cancels=0;
                document.addEventListener('touchstart',()=>window.starts++,{passive:true});
                document.addEventListener('wheel',e=>window.lastWheel={delta:e.deltaY,x:e.clientX,y:e.clientY},{passive:true});
                document.addEventListener('touchcancel',()=>window.cancels++,{passive:true});</script></body></html>`)
        }
        onLoadingChanged: function(request) { root.loaded = request.status === WPEView.LoadSucceededStatus }
    }
}
)QML");
        QVERIFY(!engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        view = window->findChild<QQuickItem *>("page");
        QVERIFY(view);
        QTRY_VERIFY_WITH_TIMEOUT(window->property("loaded").toBool(), 20000);
        QCOMPARE(evaluate("document.readyState").toString(), QString("complete"));
    }
    void init() {
        QVERIFY(evaluate("scrollTo(0,0);document.getElementById('nested').scrollTop=0;true").toBool());
        QTest::qWait(150);
    }
    void primaryDragScrolls() {
        drag({430,650}, {430,300});
        QTRY_VERIFY(evaluate("scrollY").toDouble() > 100);
        QCOMPARE(evaluate("window.clicks").toInt(), 0);
        QVERIFY(evaluate("window.starts").toInt() > 0);
    }
    void tapsStillClick() {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, {90,30});
        QTRY_COMPARE(evaluate("window.clicks").toInt(), 1);
        QCOMPARE(evaluate("scrollY").toInt(), 0);
    }
    void nestedElementScrolls() {
        drag({160,365}, {160,240});
        QTRY_VERIFY(evaluate("document.getElementById('nested').scrollTop").toInt() > 30);
        QCOMPARE(evaluate("scrollY").toInt(), 0);
    }
    void wheelStillReachesPage() {
        const int starts = evaluate("window.starts").toInt();
        QVERIFY(evaluate("window.lastWheel=null;true").toBool());
        const QPointF position(430,650);
        QTest::mouseMove(window, position.toPoint());
        QWheelEvent wheel(position, window->mapToGlobal(position.toPoint()), {}, {0,-120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        wheel.setTimestamp(10000);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(evaluate("window.lastWheel ? window.lastWheel.delta : 0").toDouble() > 0);
        QCOMPARE(evaluate("window.starts").toInt(), starts);
    }
    void modifiersDoNotChangeScrolling_data() {
        QTest::addColumn<int>("modifiers");
        QTest::newRow("shift") << int(Qt::ShiftModifier);
        QTest::newRow("control") << int(Qt::ControlModifier);
        QTest::newRow("alt") << int(Qt::AltModifier);
        QTest::newRow("meta") << int(Qt::MetaModifier);
    }
    void modifiersDoNotChangeScrolling() {
        QFETCH(int, modifiers);
        drag({430,650}, {430,300}, Qt::KeyboardModifiers(modifiers));
        QTRY_VERIFY(evaluate("scrollY").toDouble() > 100);
        QVERIFY(evaluate("getSelection().toString()").toString().isEmpty());
    }
    void synthesizedPointersScroll_data() {
        QTest::addColumn<int>("source");
        QTest::newRow("system") << int(Qt::MouseEventSynthesizedBySystem);
        QTest::newRow("qt") << int(Qt::MouseEventSynthesizedByQt);
        QTest::newRow("application") << int(Qt::MouseEventSynthesizedByApplication);
    }
    void synthesizedPointersScroll() {
        QFETCH(int, source);
        auto send = [&](QEvent::Type type, QPointF position) {
            const bool pressed = type != QEvent::MouseButtonRelease;
            QMouseEvent event(type, position, position, window->mapToGlobal(position.toPoint()),
                type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                pressed ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier, Qt::MouseEventSource(source));
            QCoreApplication::sendEvent(view, &event);
        };
        send(QEvent::MouseButtonPress, {430,650});
        for (int y = 620; y >= 320; y -= 30) {
            send(QEvent::MouseMove, {430,qreal(y)});
            QTest::qWait(25);
        }
        send(QEvent::MouseButtonRelease, {430,320});
        QTRY_VERIFY(evaluate("scrollY").toDouble() > 100);
    }
    void cursorMovementDoesNotScroll() {
        const int starts = evaluate("window.starts").toInt();
        for (int y = 650; y >= 320; y -= 30)
            QTest::mouseMove(window, {430,y}, 25);
        QTest::qWait(100);
        QCOMPARE(evaluate("scrollY").toInt(), 0);
        QCOMPARE(evaluate("window.starts").toInt(), starts);
    }
    void hidingCancelsGesture() {
        const int cancels = evaluate("window.cancels").toInt();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, {430,650});
        QTest::qWait(100);
        view->setVisible(false);
        QTest::qWait(100);
        view->setVisible(true);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, {430,650});
        QTRY_VERIFY(evaluate("window.cancels").toInt() > cancels);
        drag({430,650}, {430,300});
        QTRY_VERIFY(evaluate("scrollY").toDouble() > 100);
    }
    void realTouchScrollsAndCancels() {
        auto *device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        QTest::touchEvent(window, device).press(0, {430,650});
        for (int i = 1; i <= 10; ++i) {
            QTest::touchEvent(window, device).move(0, {430,650 - i * 30});
            QTest::qWait(30);
        }
        QTest::touchEvent(window, device).release(0, {430,350});
        QTRY_VERIFY(evaluate("scrollY").toDouble() > 100);
        const int cancels = evaluate("window.cancels").toInt();
        QTest::touchEvent(window, device).press(0, {430,650});
        QTest::qWait(100);
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QCoreApplication::sendEvent(view, &cancel);
        QTRY_VERIFY(evaluate("window.cancels").toInt() > cancels);
        QTest::touchEvent(window, device).release(0, {430,650});
    }
};

int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("browser-input-test"));
    QTimer pump;
    QObject::connect(&pump, &QTimer::timeout, &app, [] {
        for (int i = 0; i < 64 && g_main_context_pending(nullptr); ++i)
            g_main_context_iteration(nullptr, FALSE);
    });
    pump.start(8);
    InputTests tests;
    const int result = QTest::qExec(&tests, argc, argv);
    QFile report(qEnvironmentVariable("HOME") + QStringLiteral("/Data/input-result"));
    if (report.open(QIODevice::WriteOnly)) report.write(QByteArray::number(result));
    return result;
}
#include "tst_input.moc"
