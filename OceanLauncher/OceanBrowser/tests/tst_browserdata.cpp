#include "../BrowserData.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

class BrowserDataTests : public QObject {
    Q_OBJECT
private slots:
    void sessionExcludesPrivateTabs() {
        QTemporaryDir directory;
        BrowserPreviews previews;
        BrowserData data(&previews, directory.path(), QStringLiteral("test_user"), "test-app");
        data.saveSession({QVariantMap{{"id", "tab-regular"}, {"url", "https://example.com/"}},
                          QVariantMap{{"id", "tab-private"}, {"url", "https://private.example/"}, {"privateTab", true}}},
                         QStringLiteral("tab-private"));
        const auto session = data.session();
        QCOMPARE(session.value("tabs").toList().size(), 1);
        QCOMPARE(session.value("activeId").toString(), QStringLiteral("tab-regular"));
        QFile file(data.dataPath() + "/Data/tabs.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(!bytes.contains("private"));
        QCOMPARE(QFileInfo(file).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions{});
        BrowserData restored(&previews, directory.path(), QStringLiteral("test_user"), "test-app");
        QCOMPARE(restored.session(), session);
    }
    void previewsRespectSessionPrivacyAndCleanup() {
        QTemporaryDir directory;
        BrowserPreviews previews;
        BrowserData data(&previews, directory.path(), "root", "test-app");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; Rectangle { width: 120; height: 160; color: 'red' }", QUrl{});
        QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(item);
        QQuickWindow window;
        item->setParentItem(window.contentItem());
        window.resize(120, 160);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy spy(&data, &BrowserData::previewReady);
        QVERIFY(data.capturePreview(item.data(), "tab-private", true));
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.last().at(1).toUrl().scheme(), QString("image"));
        QVERIFY(!QFile::exists(data.dataPath() + "/Cache/images/tab-private.jpg"));
        QSize size;
        QVERIFY(!previews.requestImage("tab-private", &size, {}).isNull());
        QVERIFY(data.capturePreview(item.data(), "tab-normal", false));
        QTRY_COMPARE(spy.size(), 2);
        const auto imagePath = spy.last().at(1).toUrl().toLocalFile();
        QVERIFY(QFile::exists(imagePath));
        QVERIFY(!QImage(imagePath).isNull());
        QVERIFY(!data.capturePreview(item.data(), "../escape", false));
        data.saveSession({QVariantMap{{"id", "tab-normal"}, {"url", "about:overview"}}}, "tab-normal");
        BrowserData restored(&previews, directory.path(), "root", "test-app");
        QVERIFY(!restored.previewSource("tab-normal").isEmpty());
        data.removePreview("tab-private");
        QVERIFY(previews.requestImage("tab-private", &size, {}).isNull());
        data.removePreview("tab-normal");
        QVERIFY(!QFile::exists(imagePath));
    }
};
int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    BrowserDataTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_browserdata.moc"
