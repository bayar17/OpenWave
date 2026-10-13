#include "../OceanDownloadManager.h"
#include "../OceanCookieManager.h"
#include "../BrowserPreferences.h"
#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

class DownloadTests : public QObject {
    Q_OBJECT
    static QVariant value(OceanDownloadManager &model, int row, int role) {
        return model.data(model.index(row), role);
    }
    static void seed(const QString &root, const QString &payload) {
        QVERIFY(QDir().mkpath(root + "/Downloads"));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "seed");
            db.setDatabaseName(root + "/Downloads/Downloads.sqlite");
            QVERIFY(db.open());
            QSqlQuery query(db);
            // The original schema must migrate without losing existing records.
            QVERIFY(query.exec("CREATE TABLE downloads(id INTEGER PRIMARY KEY AUTOINCREMENT,source_url TEXT NOT NULL,filename TEXT NOT NULL,destination TEXT NOT NULL,mime_type TEXT,total_bytes INTEGER DEFAULT -1,received_bytes INTEGER DEFAULT 0,status TEXT NOT NULL,started_at INTEGER NOT NULL,completed_at INTEGER)"));
            int i = 0;
            for (const QString &status : {"Completed", "Downloading", "Pending", "Cancelled"}) {
                query.prepare("INSERT INTO downloads(source_url,filename,destination,mime_type,total_bytes,received_bytes,status,started_at,completed_at) VALUES(?,?,?,?,?,?,?,?,?)");
                for (const QVariant &v : QVariantList{"https://example.test/file", "a file.png", payload,
                        "image/png", -1, 42, status, 1800000000000LL + i++, QVariant()}) query.addBindValue(v);
                QVERIFY(query.exec());
            }
        }
        QSqlDatabase::removeDatabase("seed");
    }
private slots:
    void filenameSafety() {
        QCOMPARE(OceanDownloadManager::sanitizeFilename("../folder/file.txt"), QString("file.txt"));
        QCOMPARE(OceanDownloadManager::sanitizeFilename("C:\\path\\report.txt"), QString("report.txt"));
        QCOMPARE(OceanDownloadManager::sanitizeFilename(".."), QString("download"));
        QCOMPARE(OceanDownloadManager::sanitizeFilename("bad:name?.png"), QString("badname.png"));
        QVERIFY(OceanDownloadManager::sanitizeFilename(QString(300, QChar(0x0641))).toUtf8().size() <= 200);
    }
    void migrationInterruptionRemovalAndRestart() {
        QTemporaryDir dir;
        const QString payload = dir.path() + "/a file.png";
        QFile file(payload); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep payload"); file.close();
        seed(dir.path(), payload);
        const auto connections = QSqlDatabase::connectionNames();
        {
            BrowserPreferences downloadsPreferences(dir.path(), dir.path());
            OceanDownloadManager downloads(dir.path(), dir.path(), &downloadsPreferences);
            QTRY_VERIFY(downloads.ready()); QCOMPARE(downloads.rowCount(), 4);
            QCOMPARE(value(downloads, 0, OceanDownloadManager::Status).toString(), QString("Cancelled"));
            QCOMPARE(value(downloads, 1, OceanDownloadManager::Status).toString(), QString("Failed"));
            QCOMPARE(value(downloads, 2, OceanDownloadManager::Status).toString(), QString("Failed"));
            QVERIFY(!value(downloads, 1, OceanDownloadManager::ErrorDetail).toString().isEmpty());
            QCOMPARE(value(downloads, 3, OceanDownloadManager::TotalBytes).toLongLong(), -1LL);
            QVERIFY(value(downloads, 3, OceanDownloadManager::FileExists).toBool());
            QCOMPARE(value(downloads, 3, OceanDownloadManager::ThumbnailSource).toUrl().toLocalFile(), payload);
            QVERIFY(!value(downloads, 3, OceanDownloadManager::CanPause).toBool());
            QVERIFY(!value(downloads, 3, OceanDownloadManager::CanResume).toBool());
            downloads.clearCompleted(); QCOMPARE(downloads.rowCount(), 3); QVERIFY(QFile::exists(payload));
            downloads.removeRecord(value(downloads, 0, OceanDownloadManager::DownloadId).toString());
            QCOMPARE(downloads.rowCount(), 2);
        }
        QCOMPARE(QSqlDatabase::connectionNames(), connections);
        BrowserPreferences restoredPreferences(dir.path(), dir.path());
        OceanDownloadManager restored(dir.path(), dir.path(), &restoredPreferences); QTRY_VERIFY(restored.ready());
        QCOMPARE(restored.rowCount(), 2); QVERIFY(QFile::exists(payload));
        QFile original(payload); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("keep payload"));
        const auto permissions = QFileInfo(dir.path() + "/Downloads/Downloads.sqlite").permissions();
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
    }
    void missingPayloadAndUnsafeStorage() {
        QTemporaryDir dir, outside;
        seed(dir.path(), dir.path() + "/missing.png");
        {
            BrowserPreferences downloadsPreferences(dir.path(), dir.path());
            OceanDownloadManager downloads(dir.path(), dir.path(), &downloadsPreferences); QTRY_VERIFY(downloads.ready());
            QVERIFY(!value(downloads, 3, OceanDownloadManager::FileExists).toBool());
            downloads.openFile(value(downloads, 3, OceanDownloadManager::DownloadId).toString());
            QVERIFY(!downloads.error().isEmpty());
        }
        QTemporaryDir unsafe;
        const auto permissions = QFileInfo(outside.path()).permissions();
        QVERIFY(QFile::link(outside.path(), unsafe.path() + "/Downloads"));
        BrowserPreferences downloadsPreferences(unsafe.path(), unsafe.path());
        OceanDownloadManager downloads(unsafe.path(), unsafe.path(), &downloadsPreferences);
        QTRY_VERIFY(!downloads.error().isEmpty()); QVERIFY(!downloads.ready());
        QCOMPARE(QFileInfo(outside.path()).permissions(), permissions);
        QVERIFY(!QFile::exists(outside.path() + "/Downloads.sqlite"));
    }
    void cookieMigrationAndUnsafeStorage() {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + "/cookies"));
        QFile existing(dir.path() + "/cookies/cookies.sqlite");
        QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("engine-owned-store"); existing.close();
        BrowserPreferences cookiesPreferences(dir.path(), dir.path());
        OceanCookieManager cookies(dir.path(), &cookiesPreferences); QVERIFY(cookies.ready());
        QCOMPARE(cookies.profilePath(), dir.path() + "/WebEngine");
        QFile moved(cookies.profilePath() + "/cookies/cookies.sqlite");
        QVERIFY(moved.open(QIODevice::ReadOnly)); QCOMPARE(moved.readAll(), QByteArray("engine-owned-store"));
        QVERIFY(!QFile::exists(dir.path() + "/cookies"));
        QTemporaryDir unsafe, outside;
        const auto permissions = QFileInfo(outside.path()).permissions();
        QVERIFY(QFile::link(outside.path(), unsafe.path() + "/WebEngine"));
        BrowserPreferences rejectedPreferences(unsafe.path(), unsafe.path());
        OceanCookieManager rejected(unsafe.path(), &rejectedPreferences); QVERIFY(!rejected.ready());
        QCOMPARE(QFileInfo(outside.path()).permissions(), permissions);
        QVERIFY(qgetenv("OCEAN_BROWSER_PROFILE_DIRECTORY").isEmpty());
    }
};
QTEST_GUILESS_MAIN(DownloadTests)
#include "tst_downloads.moc"
