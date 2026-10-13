#include "../OceanHistoryManager.h"
#include "../../../userspace/application-data.h"
#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

class HistoryTests : public QObject {
    Q_OBJECT
    static QVariant value(OceanHistoryManager &history, int row, int role) {
        return history.data(history.index(row), role);
    }
    static void legacy(const QString &path, const QVariantList &rows) {
        QFile file(path + "/history.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(QJsonDocument::fromVariant(rows).toJson());
    }
private slots:
    void resolverUsesProvidedIdentity() {
        QCOMPARE(openwaveApplicationDataPath("a-different-app", "/profiles", "test_user"),
                 QString("/profiles/test_user/.DataLocation/Applications/a-different-app"));
        QVERIFY(openwaveApplicationDataPath("../escape", "/profiles", "test_user").isEmpty());
        QVERIFY(openwaveApplicationDataPath("valid", "/profiles", "..").isEmpty());
    }
    void repeatVisitsPrivacySearchAndRestart() {
        QTemporaryDir directory;
        const auto connections = QSqlDatabase::connectionNames();
        {
            OceanHistoryManager history(directory.path());
            QTRY_VERIFY(history.ready());
            QVERIFY(history.recordVisit(QUrl("about:history"), "History").isEmpty());
            QVERIFY(history.recordVisit(QUrl("https://private.test/"), "Private", true).isEmpty());
            history.recordVisit(QUrl("https://example.test/"), "Page title");
            history.recordVisit(QUrl("https://example.test/"), "Page title");
            history.recordVisit(QUrl("https://second.test/find-me"), "Second page");
            QTRY_COMPARE(history.count(), 3);
            QCOMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("Second page"));
            QVERIFY(value(history, 0, OceanHistoryManager::VisitedAt).toLongLong() > 1700000000000LL);
            QVERIFY(!value(history, 0, OceanHistoryManager::VisitTime).toString().isEmpty());
            history.setFilter("find-me"); QTRY_COMPARE(history.count(), 1);
            history.setFilter("Page title"); QTRY_COMPARE(history.count(), 2);
            history.setFilter("' OR 1=1 --"); QTRY_COMPARE(history.count(), 0);
            history.setFilter(""); QTRY_COMPARE(history.count(), 3);
            const qint64 id = value(history, 0, OceanHistoryManager::HistoryId).toLongLong();
            history.removeEntries({id}); QTRY_COMPARE(history.count(), 2);
        }
        QCOMPARE(QSqlDatabase::connectionNames(), connections);
        OceanHistoryManager restored(directory.path());
        QTRY_VERIFY(restored.ready()); QTRY_COMPARE(restored.count(), 2);
        const auto permissions = QFileInfo(directory.path() + "/Browser/History.sqlite").permissions();
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
        restored.clearHistory(); QTRY_COMPARE(restored.count(), 0);
    }
    void migrationDatesAndSorting() {
        QTemporaryDir directory;
        const QDate today = QDate::currentDate();
        const auto entry = [](QString url, QString title, QDate day) {
            return QVariantMap{{"address", url}, {"title", title}, {"visited", day.startOfDay().toUTC().toString(Qt::ISODate)}};
        };
        legacy(directory.path(), {entry("https://c.test/", "C title", today),
            entry("https://b.test/", "B title", today.addDays(-1)), entry("https://a.test/", "A title", today.addDays(-10))});
        {
            OceanHistoryManager history(directory.path());
            QTRY_VERIFY(history.ready()); QTRY_COMPARE(history.count(), 3);
            QCOMPARE(value(history, 0, OceanHistoryManager::DateGroup).toString(), QString("Today"));
            QCOMPARE(value(history, 1, OceanHistoryManager::DateGroup).toString(), QString("Yesterday"));
            history.setSortOrder("oldest");
            QTRY_COMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("A title"));
            history.setSortOrder("title");
            QTRY_COMPARE(value(history, 1, OceanHistoryManager::PageTitle).toString(), QString("B title"));
            history.clearDateRange("bad date", "bad date");
            QVERIFY(!history.error().isEmpty()); QCOMPARE(history.count(), 3);
            history.clearPeriod("yesterday"); QTRY_COMPARE(history.count(), 2);
            history.clearPeriod("today"); QTRY_COMPARE(history.count(), 1);
            QCOMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("A title"));
            QVERIFY(!QFile::exists(directory.path() + "/history.json"));
        }
        OceanHistoryManager restored(directory.path());
        QTRY_VERIFY(restored.ready()); QTRY_COMPARE(restored.count(), 1);
    }
    void metadataMatchesCommittedDocument() {
        QTemporaryDir directory;
        OceanHistoryManager history(directory.path()); QTRY_VERIFY(history.ready());
        const QUrl page(QString::fromUtf8("https://example.test/café"));
        const auto token = history.recordVisit(page, "Original");
        history.setVisitDetails(token, "Wrong page", {}, QUrl("https://example.test/later"));
        QTRY_COMPARE(history.count(), 1);
        QCOMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("Original"));
        history.setVisitDetails(token, "Translated title", {}, QUrl("https://example.test/caf%C3%A9"));
        QTRY_COMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("Translated title"));
    }
    void faviconCacheIndependentOfHistory() {
        QTemporaryDir directory;
        OceanHistoryManager history(directory.path()); QTRY_VERIFY(history.ready());
        QImage image(16, 16, QImage::Format_ARGB32); image.fill(Qt::red);
        QBuffer buffer; buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
        const QUrl icon("data:image/png;base64," + QString::fromLatin1(buffer.data().toBase64()));
        for (int i = 0; i < 2; ++i) {
            const QString token = history.recordVisit(QUrl("https://icons.test/"), "");
            history.setVisitDetails(token, "With icon", icon);
        }
        QTRY_COMPARE(history.count(), 2);
        QTRY_VERIFY(!value(history, 0, OceanHistoryManager::FaviconSource).toString().isEmpty());
        QTRY_COMPARE(value(history, 0, OceanHistoryManager::PageTitle).toString(), QString("With icon"));
        QTRY_COMPARE(value(history, 0, OceanHistoryManager::FaviconSource), value(history, 1, OceanHistoryManager::FaviconSource));
        const QUrl source(value(history, 0, OceanHistoryManager::FaviconSource).toString());
        QVERIFY(source.toLocalFile().contains("/Cache/Favicons/"));
        QCOMPARE(QDir(directory.path() + "/Cache/Favicons").entryList({"*.png"}, QDir::Files).size(), 1);
        QVERIFY(QDir(directory.path() + "/Cache").removeRecursively());
        history.reload();
        QTRY_VERIFY(value(history, 0, OceanHistoryManager::FaviconSource).toString().isEmpty());
        QCOMPARE(history.count(), 2);
        const auto token = history.recordVisit(QUrl("https://icons.test/new"), "New visit");
        history.setVisitFavicon(token, icon);
        QTRY_COMPARE(history.count(), 3);
        QTRY_VERIFY(!value(history, 0, OceanHistoryManager::FaviconSource).toString().isEmpty());
        QFile corrupt(source.toLocalFile()); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken png"); corrupt.close();
        history.reload(); QTRY_VERIFY(value(history, 0, OceanHistoryManager::FaviconSource).toString().isEmpty());
        QCOMPARE(history.count(), 3);
    }
    void paginationAndSafeFailures() {
        QTemporaryDir directory;
        QVariantList rows;
        for (int i = 0; i < 205; ++i) rows.append(QVariantMap{{"address", QString("https://pages.test/%1").arg(i)},
            {"title", QString("Visit %1").arg(i)}, {"visited", QDateTime::currentDateTimeUtc().addSecs(i).toString(Qt::ISODate)}});
        legacy(directory.path(), rows);
        {
            OceanHistoryManager history(directory.path()); QTRY_VERIFY(history.ready()); QTRY_COMPARE(history.count(), 205);
            QCOMPARE(history.rowCount(), 200); QVERIFY(history.canFetchMore({}));
            history.fetchMore({}); QTRY_COMPARE(history.rowCount(), 205); QVERIFY(!history.canFetchMore({}));
        }
        QTemporaryDir bad;
        QDir().mkpath(bad.path() + "/Browser");
        QFile file(bad.path() + "/Browser/History.sqlite"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("corrupt database"); file.close();
        OceanHistoryManager history(bad.path()); QTRY_VERIFY(!history.error().isEmpty()); QVERIFY(!history.ready());
        QCOMPARE(history.count(), 0);
    }
};
QTEST_GUILESS_MAIN(HistoryTests)
#include "tst_history.moc"
