#include "../GalleryLibrary.h"
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class GalleryTest : public QObject
{
    Q_OBJECT
private slots:
    void decoderDiscovery() {
        GalleryLibrary library;
        QVERIFY(library.supportedFormats().contains("PNG"));
        QStringList formats = library.supportedFormats(); formats.removeDuplicates();
        QCOMPARE(formats, library.supportedFormats());
        const QStringList types = library.supportedMimeTypes();
        QVERIFY(types.contains("image/png"));
        QVERIFY(types.size() <= 32);
        const QRegularExpression portalMime("^[a-zA-Z0-9/.+\\-]+$");
        for (const QString &type : types) QVERIFY(portalMime.match(type).hasMatch());
    }
    void importMetadataAndDeduplicate() {
        QTemporaryDir folder;
        const auto path = folder.filePath("test.png");
        QImage image(120, 80, QImage::Format_RGB32); image.fill(Qt::cyan);
        QVERIFY(image.save(path));
        GalleryLibrary library;
        QCOMPARE(library.openImage(QUrl::fromLocalFile(path)), 0);
        const auto photo = library.photos().first().toMap();
        QCOMPARE(photo["format"].toString(), QString("PNG"));
        QCOMPARE(photo["dimensions"].toString(), QString("120 × 80"));
        QCOMPARE(library.openImage(QUrl::fromLocalFile(path)), 0);
        QCOMPARE(library.photos().size(), 1);
        QCOMPARE(library.albums().first().toMap()["count"].toInt(), 1);
    }
    void badImageDoesNotDiscardLibrary() {
        QTemporaryDir folder;
        const auto valid = folder.filePath("photo.png");
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(valid));
        QFile damaged(folder.filePath("broken.png")); QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("invalid image"); damaged.close();
        GalleryLibrary library;
        QCOMPARE(library.openImage(QUrl::fromLocalFile(valid)), 0);
        QCOMPARE(library.openImage(QUrl::fromLocalFile(damaged.fileName())), -1);
        QVERIFY(!library.error().isEmpty()); QCOMPARE(library.photos().size(), 1);
        QVERIFY(!library.loadFolder(QUrl("https://example.invalid/photos")));
        QCOMPARE(library.photos().size(), 1);
    }
    void importedPhotosSurvivePortalTemporaryFileRemoval() {
        QTemporaryDir grant;
        const QString source = grant.filePath("photo.png");
        QImage image(24, 12, QImage::Format_RGB32); image.fill(Qt::magenta); QVERIFY(image.save(source));
        GalleryLibrary library;
        QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "Holiday.png"), 0);
        const auto first = library.photos().first().toMap();
        QCOMPARE(first["title"].toString(), QString("Holiday.png"));
        QCOMPARE(first["album"].toString(), QString("Selected photos"));
        QVERIFY(QFile::remove(source));
        QVERIFY(!QImageReader(first["url"].toUrl().toLocalFile()).read().isNull());
        image.fill(Qt::cyan); QVERIFY(image.save(source));
        QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "Second.png"), 1);
        QVERIFY(QFile::exists(first["url"].toUrl().toLocalFile()));
    }
    void importsSurviveRestartAndCacheRemoval() {
        QTemporaryDir appData, grant;
        const QString source = grant.filePath("photo.png");
        QImage image(40, 20, QImage::Format_RGB32); image.fill(Qt::yellow); QVERIFY(image.save(source));
        const QString storage = appData.filePath("Data/Gallery");
        QUrl saved;
        {
            GalleryLibrary library(storage);
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "Holiday.png"), 0);
            saved = library.photos().first().toMap()["url"].toUrl();
            QVERIFY(QFile::remove(source));
        }
        QVERIFY(QDir().mkpath(appData.filePath("Cache")));
        QVERIFY(QDir(appData.filePath("Cache")).removeRecursively());
        GalleryLibrary restored(storage);
        QCOMPARE(restored.photos().size(), 1);
        QCOMPARE(restored.photos().first().toMap()["title"].toString(), QString("Holiday.png"));
        QCOMPARE(restored.photos().first().toMap()["url"].toUrl(), saved);
        QVERIFY(!QImageReader(saved.toLocalFile()).read().isNull());
    }
    void identicalImportsDeduplicateAndNameCollisionsRemainDistinct() {
        QTemporaryDir data, grant;
        const QString source = grant.filePath("photo.png");
        QImage image(10, 10, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(source));
        {
            GalleryLibrary library(data.path());
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "photo.png"), 0);
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "renamed.png"), 0);
            image.fill(Qt::blue); QVERIFY(image.save(source));
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "photo.png"), 1);
            QCOMPARE(library.photos().size(), 2);
        }
        GalleryLibrary restored(data.path());
        QCOMPARE(restored.photos().size(), 2);
        QVERIFY(restored.photos()[0].toMap()["url"] != restored.photos()[1].toMap()["url"]);
    }
    void brokenSavedFilesDoNotDiscardHealthyPhotos() {
        QTemporaryDir data, grant;
        const QString source = grant.filePath("photo.png");
        QImage image(10, 10, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(source));
        QString broken;
        {
            GalleryLibrary library(data.path());
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "broken.png"), 0);
            broken = library.photos().first().toMap()["url"].toUrl().toLocalFile();
            image.fill(Qt::green); QVERIFY(image.save(source));
            QCOMPARE(library.importImage(QUrl::fromLocalFile(source), "valid.png"), 1);
        }
        QFile corrupt(broken); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("corrupted"); corrupt.close();
        GalleryLibrary restored(data.path());
        QCOMPARE(restored.photos().size(), 1);
        QCOMPARE(restored.photos().first().toMap()["title"].toString(), QString("valid.png"));
        QFile bad(grant.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("invalid"); bad.close();
        QCOMPARE(restored.importImage(QUrl::fromLocalFile(bad.fileName()), "bad.png"), -1);
        QCOMPARE(restored.photos().size(), 1);
    }
    void detectFileContentAndFilterFolder() {
        QTemporaryDir folder;
        QImage image(5, 7, QImage::Format_RGB32); image.fill(Qt::blue);
        QVERIFY(image.save(folder.filePath("valid.png")));
        QVERIFY(image.save(folder.filePath("unusual.extension"), "PNG"));
        QFile text(folder.filePath("notes.txt")); QVERIFY(text.open(QIODevice::WriteOnly)); text.write("not an image"); text.close();
        GalleryLibrary library;
        QVERIFY(library.loadFolder(QUrl::fromLocalFile(folder.path())));
        QCOMPARE(library.photos().size(), 1);
        QCOMPARE(library.openImage(QUrl::fromLocalFile(folder.filePath("unusual.extension"))), 1);
        QCOMPARE(library.photos().at(1).toMap()["format"].toString(), QString("PNG"));
    }
    void sampleAlbumsAndSorting() {
        QTemporaryDir folder;
        QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::white);
        const auto older = folder.filePath("older.png"), newer = folder.filePath("newer.png");
        QVERIFY(image.save(older)); QVERIFY(image.save(newer));
        QFile sample(folder.filePath("sample.json")); QVERIFY(sample.open(QIODevice::WriteOnly));
        sample.write(QJsonDocument(QJsonArray{
            QJsonObject{{"path", older}, {"album", "Landscapes"}, {"date", "2026-10-13T09:00:00+03:00"}},
            QJsonObject{{"path", newer}, {"album", "Favorites"}, {"date", "2026-10-16T09:00:00+03:00"}}
        }).toJson()); sample.close();
        GalleryLibrary library;
        QVERIFY(library.loadSampleLibrary(sample.fileName()));
        QCOMPARE(library.photos().first().toMap()["title"].toString(), QString("newer.png"));
        QCOMPARE(library.albums().size(), 2);
    }
};
QTEST_MAIN(GalleryTest)
#include "tst_library.moc"
