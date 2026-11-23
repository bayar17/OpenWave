#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

class AddressLoadingTests : public QObject {
    Q_OBJECT
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> create() {
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(ADDRESS_BORDER_SOURCE)));
        if (component.isError()) qWarning() << component.errors();
        std::unique_ptr<QObject> item(component.createWithInitialProperties({{"width", 460}, {"height", 70}}));
        if (item) qobject_cast<QQuickItem *>(item.get())->setParentItem(window.contentItem());
        return item;
    }
    static qreal reveal(QObject *item) { return item->property("reveal").toReal(); }
private slots:
    void completionStopsUpdatesAfterFade() {
        auto item = create(); QVERIFY(item);
        item->setProperty("loading", true);
        QTRY_VERIFY(item->property("rotating").toBool());
        QTRY_COMPARE(reveal(item.get()), 1.0);
        const qreal angle = item->property("phase").toReal();
        QTest::qWait(100);
        QVERIFY(item->property("phase").toReal() != angle);
        item->setProperty("loading", false);
        QTest::qWait(150);
        QVERIFY(reveal(item.get()) > 0 && reveal(item.get()) < 1);
        QVERIFY(item->property("rotating").toBool());
        QTRY_COMPARE(reveal(item.get()), 0.0);
        QVERIFY(!item->property("rotating").toBool());
        const qreal stopped = item->property("phase").toReal();
        QTest::qWait(100);
        QCOMPARE(item->property("phase").toReal(), stopped);
    }
    void consecutiveLoadsAndTabChanges() {
        auto item = create(); QVERIFY(item);
        item->setProperty("loading", true); QTRY_COMPARE(reveal(item.get()), 1.0);
        item->setProperty("loading", false); QTest::qWait(200);
        const qreal fading = reveal(item.get());
        item->setProperty("loading", true);
        QCOMPARE(reveal(item.get()), fading);
        QTRY_COMPARE(reveal(item.get()), 1.0);
        item->setProperty("loading", false);
        QObject otherTab;
        item->setProperty("navigationSession", QVariant::fromValue(&otherTab));
        QCOMPARE(reveal(item.get()), 0.0);
        QVERIFY(!item->property("rotating").toBool());
        item->setProperty("loading", true); QTRY_COMPARE(reveal(item.get()), 1.0);
    }
    void hiddenWindowsAndReducedMotion() {
        auto item = create(); QVERIFY(item);
        item->setProperty("loading", true); QTRY_COMPARE(reveal(item.get()), 1.0);
        item->setProperty("windowPresented", false);
        QCOMPARE(reveal(item.get()), 0.0);
        QVERIFY(!item->property("rotating").toBool());
        item->setProperty("windowPresented", true); QTRY_COMPARE(reveal(item.get()), 1.0);
        item->setProperty("visible", false);
        QCOMPARE(reveal(item.get()), 0.0);
        QVERIFY(!item->property("rotating").toBool());
        item->setProperty("visible", true);
        QTRY_COMPARE(reveal(item.get()), 1.0);
        item->setProperty("reducedMotion", true);
        QCOMPARE(reveal(item.get()), 1.0);
        QVERIFY(!item->property("rotating").toBool());
        item->setProperty("loading", false); QCOMPARE(reveal(item.get()), 0.0);
        item->setProperty("loading", true); QCOMPARE(reveal(item.get()), 1.0);
        QVERIFY(!item->property("rotating").toBool());
    }
};
QTEST_MAIN(AddressLoadingTests)
#include "tst_addressloading.moc"
