#include "../OceanBookmarkManager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

class BookmarkTests : public QObject
{
    Q_OBJECT

private slots:
    void normalizesAndRejectsAddresses()
    {
        QCOMPARE(OceanBookmarkManager::normalized(QStringLiteral(" https://Example.org/a b ")), QStringLiteral("https://example.org/a%20b"));
        QVERIFY(OceanBookmarkManager::normalized(QStringLiteral("about:overview")).isEmpty());
        QVERIFY(OceanBookmarkManager::normalized(QStringLiteral("javascript:alert(1)")).isEmpty());
        QVERIFY(OceanBookmarkManager::normalized(QStringLiteral("file:///etc/passwd")).isEmpty());
        QVERIFY(OceanBookmarkManager::normalized(QStringLiteral("https://")).isEmpty());
        QVERIFY(OceanBookmarkManager::normalized(QString()).isEmpty());
        QVERIFY(OceanBookmarkManager::normalized(QStringLiteral("https://e.org/") + QString(3000, QLatin1Char('a'))).isEmpty());
    }

    void togglesAndPersists()
    {
        QTemporaryDir directory;
        QString id;
        {
            OceanBookmarkManager manager(directory.path());
            QSignalSpy changed(&manager, &OceanBookmarkManager::changed);
            QVERIFY(!manager.contains(QStringLiteral("https://example.org/")));
            QVERIFY(manager.toggle(QStringLiteral("https://example.org/"), QStringLiteral("  Example   page ")));
            QCOMPARE(changed.count(), 1);
            QVERIFY(manager.contains(QStringLiteral("https://EXAMPLE.org/")));
            QCOMPARE(manager.count(), 1);
            QCOMPARE(manager.data(manager.index(0), OceanBookmarkManager::PageTitle).toString(), QStringLiteral("Example page"));
            QCOMPARE(manager.data(manager.index(0), OceanBookmarkManager::Host).toString(), QStringLiteral("example.org"));
            id = manager.data(manager.index(0), OceanBookmarkManager::BookmarkId).toString();
            QVERIFY(!id.isEmpty());
            QVERIFY(manager.toggle(QStringLiteral("https://second.example/"), QString()));
            QCOMPARE(manager.data(manager.index(0), OceanBookmarkManager::PageTitle).toString(), QStringLiteral("second.example"));
        }
        OceanBookmarkManager reopened(directory.path());
        QCOMPARE(reopened.count(), 2);
        QVERIFY(reopened.contains(QStringLiteral("https://example.org/")));
        QVERIFY(!reopened.toggle(QStringLiteral("https://example.org/"), QString()));
        QVERIFY(!reopened.contains(QStringLiteral("https://example.org/")));
        QCOMPARE(reopened.count(), 1);
        OceanBookmarkManager again(directory.path());
        QCOMPARE(again.count(), 1);
    }

    void refusesUnsupportedPages()
    {
        QTemporaryDir directory;
        OceanBookmarkManager manager(directory.path());
        QSignalSpy failures(&manager, &OceanBookmarkManager::errorChanged);
        QVERIFY(!manager.toggle(QStringLiteral("about:history"), QStringLiteral("History")));
        QCOMPARE(failures.count(), 1);
        QVERIFY(!manager.error().isEmpty());
        QCOMPARE(manager.count(), 0);
    }

    void removesFiltersAndClears()
    {
        QTemporaryDir directory;
        OceanBookmarkManager manager(directory.path());
        QVERIFY(manager.toggle(QStringLiteral("https://alpha.example/"), QStringLiteral("Alpha")));
        QVERIFY(manager.toggle(QStringLiteral("https://beta.example/"), QStringLiteral("Beta")));
        QVERIFY(manager.toggle(QStringLiteral("https://gamma.example/"), QStringLiteral("Gamma")));
        manager.setFilter(QStringLiteral("beta"));
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.data(manager.index(0), OceanBookmarkManager::PageTitle).toString(), QStringLiteral("Beta"));
        const QString id = manager.data(manager.index(0), OceanBookmarkManager::BookmarkId).toString();
        manager.remove(id);
        QCOMPARE(manager.rowCount(), 0);
        manager.setFilter(QString());
        QCOMPARE(manager.rowCount(), 2);
        QVERIFY(!manager.contains(QStringLiteral("https://beta.example/")));
        manager.clear();
        QCOMPARE(manager.count(), 0);
        OceanBookmarkManager reopened(directory.path());
        QCOMPARE(reopened.count(), 0);
    }

    void survivesDamagedFilesAndFailedWrites()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("bookmarks.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        OceanBookmarkManager damaged(directory.path());
        QCOMPARE(damaged.count(), 0);
        QVERIFY(!damaged.error().isEmpty());

        QTemporaryDir readable;
        QVERIFY(QFile::setPermissions(readable.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        OceanBookmarkManager locked(readable.path());
        if (geteuid() != 0) {
            QVERIFY(!locked.toggle(QStringLiteral("https://example.org/"), QStringLiteral("Example")));
            QCOMPARE(locked.count(), 0);
            QVERIFY(!locked.error().isEmpty());
        }
        QFile::setPermissions(readable.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void dropsBadStoredEntries()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("bookmarks.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"bookmarks\":["
                   "{\"id\":\"a\",\"url\":\"https://ok.example/\",\"title\":\"Ok\",\"added\":1},"
                   "{\"id\":\"b\",\"url\":\"javascript:1\",\"title\":\"Bad\",\"added\":1},"
                   "{\"id\":\"c\",\"url\":\"https://ok.example/\",\"title\":\"Dup\",\"added\":2},"
                   "{\"url\":\"https://noid.example/\"}]}");
        file.close();
        OceanBookmarkManager manager(directory.path());
        QCOMPARE(manager.count(), 2);
        QVERIFY(manager.contains(QStringLiteral("https://noid.example/")));
    }
};

QTEST_MAIN(BookmarkTests)
#include "tst_bookmarks.moc"
