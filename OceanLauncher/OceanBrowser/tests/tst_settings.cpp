#include "../BrowserPreferences.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <OpenWave/OpenWavePortals.h>
#include <fcntl.h>

class SettingsTests : public QObject {
    Q_OBJECT
private slots:
    void directoryGrants() {
        QTemporaryDir directory, outside;
        BrowserPreferences settings(directory.path(), directory.path());
        auto *portal = settings.findChild<OpenWavePortals *>();
        QVERIFY(portal);
        {
            const int fd = ::open(QFile::encodeName(directory.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            QVERIFY(fd >= 0);
            PortalGrant grant("untrusted display name", fd, false, true);
            portal->completed({QVariant::fromValue(static_cast<QObject *>(&grant))});
            QCOMPARE(settings.downloadFolder(), directory.path());
        }
        BrowserPreferences restored(directory.path(), directory.path());
        QCOMPARE(restored.downloadFolder(), directory.path());
        {
            const int fd = ::open(QFile::encodeName(outside.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            QVERIFY(fd >= 0);
            PortalGrant grant("Downloads", fd, false, true);
            portal->completed({QVariant::fromValue(static_cast<QObject *>(&grant))});
            QVERIFY(!settings.error().isEmpty());
            QCOMPARE(settings.downloadFolder(), directory.path());
        }
    }
    void persistenceAndSearch() {
        QTemporaryDir directory;
        {
            BrowserPreferences settings(directory.path(), directory.path());
            QVERIFY(settings.error().isEmpty());
            QVERIFY(settings.askOnDownload()); QVERIFY(settings.rememberTabs());
            QVERIFY(!settings.startPrivate());
            settings.setAskOnDownload(false); settings.setStartPrivate(true);
            settings.setRememberTabs(false); settings.setAlwaysShowFullUrl(false);
            settings.setRememberHistory(false); settings.setBlockThirdPartyCookies(false);
            settings.setSearchEngine("duckduckgo");
            QCOMPARE(settings.searchUrl("cats & dogs"), QString("https://duckduckgo.com/?q=cats%20%26%20dogs"));
            settings.setSearchEngine("invalid"); QCOMPARE(settings.searchEngine(), QString("duckduckgo"));
            settings.setSearchEngine("bing");
        }
        BrowserPreferences restored(directory.path(), directory.path());
        QVERIFY(!restored.askOnDownload()); QVERIFY(restored.startPrivate());
        QVERIFY(!restored.rememberTabs()); QVERIFY(!restored.alwaysShowFullUrl());
        QVERIFY(!restored.rememberHistory()); QVERIFY(!restored.blockThirdPartyCookies());
        QCOMPARE(restored.searchEngine(), QString("bing"));
        QCOMPARE(restored.downloadFolder(), directory.path() + "/Downloads");
        const auto permissions = QFileInfo(directory.path() + "/Browser/Settings.json").permissions();
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
    }
    void corruptAndUnsafeStorage() {
        QTemporaryDir directory, outside;
        QVERIFY(QDir().mkpath(directory.path() + "/Browser"));
        QFile file(directory.path() + "/Browser/Settings.json");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not json"); file.close();
        BrowserPreferences damaged(directory.path(), directory.path());
        QVERIFY(!damaged.error().isEmpty()); QVERIFY(damaged.askOnDownload());
        damaged.setAskOnDownload(false); QVERIFY(damaged.error().isEmpty());
        QVERIFY(file.remove());
        QFile victim(outside.path() + "/private.json");
        QVERIFY(victim.open(QIODevice::WriteOnly)); victim.write("untouched"); victim.close();
        QVERIFY(QFile::link(victim.fileName(), file.fileName()));
        BrowserPreferences rejected(directory.path(), directory.path());
        QVERIFY(!rejected.error().isEmpty()); rejected.setStartPrivate(true);
        QVERIFY(!rejected.startPrivate());
        QVERIFY(victim.open(QIODevice::ReadOnly)); QCOMPARE(victim.readAll(), QByteArray("untouched"));
    }
    void nativeSwitchInput() {
        QTemporaryDir directory;
        BrowserPreferences settings(directory.path(), directory.path());
        QQuickView window;
        window.rootContext()->setContextProperty("BrowserPreferences", &settings);
        window.setResizeMode(QQuickView::SizeRootObjectToView);
        window.resize(733, 1276);
        window.setSource(QUrl::fromLocalFile(QStringLiteral(BROWSER_SETTINGS_SOURCE)));
        QCOMPARE(window.status(), QQuickView::Ready);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *toggle = window.rootObject()->findChild<QQuickItem *>("askOnDownload");
        QVERIFY(toggle); QVERIFY(toggle->property("checkable").toBool());
        const auto center = toggle->mapToScene(QPointF(toggle->width()/2, toggle->height()/2)).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(!settings.askOnDownload());
        toggle->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
        QTRY_VERIFY(settings.askOnDownload());
        window.rootObject()->setProperty("currentTab", 2);
        auto *history = window.rootObject()->findChild<QQuickItem *>("rememberHistory");
        QVERIFY(history && history->isVisible());
        history->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Space);
        QTRY_VERIFY(!settings.rememberHistory());
        BrowserPreferences restored(directory.path(), directory.path());
        QVERIFY(!restored.rememberHistory());
    }
};
int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication application(argc, argv);
    SettingsTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_settings.moc"
