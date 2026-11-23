#include "../../web/WebsiteIconFetcher.h"
#include "../WebAppIcon.h"
#include <QJsonArray>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class IconTests : public QObject {
    Q_OBJECT
    QTcpServer server;
    QHash<QByteArray, QByteArray> files;
    int requests = 0;
    QByteArray png(int size, QColor color = Qt::red) {
        QImage image(size, size, QImage::Format_ARGB32); image.fill(color);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        return bytes;
    }
    QUrl page() const { return QUrl(QString("http://127.0.0.1:%1/app").arg(server.serverPort())); }
    QString metadata(const QStringList &paths, const QString &manifest = {}) const {
        QJsonArray icons;
        for (const auto &path : paths) icons.append(page().resolved(QUrl(path)).toString());
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"url", page().toString()}, {"icons", icons},
            {"manifest", manifest.isEmpty() ? QString() : page().resolved(QUrl(manifest)).toString()}}).toJson());
    }
private slots:
    void initTestCase() {
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto request = socket->property("bytes").toByteArray() + socket->readAll();
                    socket->setProperty("bytes", request);
                    if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                    socket->setProperty("handled", true); ++requests;
                    const auto path = request.split(' ').value(1);
                    const auto body = files.value(path, "not an image");
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                        + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    void init() { requests = 0; files.clear(); }
    void highestResolutionAndManifest() {
        files["/small.png"] = png(16); files["/apple.png"] = png(180);
        files["/assets/large.png"] = png(768, Qt::blue);
        files["/assets/site.webmanifest"] = R"({"icons":[{"src":"large.png","sizes":"768x768"}]})";
        OpenWave::WebsiteIconFetcher fetcher; QSignalSpy done(&fetcher, &OpenWave::WebsiteIconFetcher::finished);
        fetcher.fetch(page(), metadata({"/small.png", "/apple.png", "/apple.png"}, "/assets/site.webmanifest"));
        QVERIFY(done.wait()); QCOMPARE(done.size(), 1);
        const auto result = done.first().first().toByteArray();
        QVERIFY(OpenWaveWebApp::validIcon(result));
        const auto image = QImage::fromData(result);
        QCOMPARE(image.size(), QSize(512, 512)); QCOMPARE(image.pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(requests, 5);
    }
    void invalidAndUnsafeCandidatesFallBack() {
        files["/favicon.ico"] = png(32, Qt::green);
        OpenWave::WebsiteIconFetcher fetcher; QSignalSpy done(&fetcher, &OpenWave::WebsiteIconFetcher::finished);
        fetcher.fetch(page(), metadata({"file:///etc/passwd", "http://user:secret@127.0.0.1/", "/broken.png"}));
        QVERIFY(done.wait()); QCOMPARE(QImage::fromData(done.first().first().toByteArray()).size(), QSize(32, 32));
        QCOMPARE(requests, 2);
    }
    void multiResolutionIcoUsesLargestFrame() {
        const auto small = png(16), large = png(128, Qt::blue);
        QByteArray ico;
        QDataStream stream(&ico, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint16(0) << quint16(1) << quint16(2);
        stream << quint8(16) << quint8(16) << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(small.size()) << quint32(38);
        stream << quint8(128) << quint8(128) << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(large.size()) << quint32(38 + small.size());
        ico += small; ico += large; files["/favicon.ico"] = ico;
        OpenWave::WebsiteIconFetcher fetcher; QSignalSpy done(&fetcher, &OpenWave::WebsiteIconFetcher::finished);
        fetcher.fetch(page(), metadata({}));
        QVERIFY(done.wait());
        const auto image = QImage::fromData(done.first().first().toByteArray());
        QCOMPARE(image.size(), QSize(128, 128)); QCOMPARE(image.pixelColor(0, 0), QColor(Qt::blue));
    }
    void cancelledRequestCannotCompleteNextRequest() {
        files["/red.png"] = png(256); files["/blue.png"] = png(64, Qt::blue);
        OpenWave::WebsiteIconFetcher fetcher; QSignalSpy done(&fetcher, &OpenWave::WebsiteIconFetcher::finished);
        fetcher.fetch(page(), metadata({"/red.png"}));
        fetcher.fetch(page(), metadata({"/blue.png"}));
        QVERIFY(done.wait()); QTest::qWait(50); QCOMPARE(done.size(), 1);
        const auto image = QImage::fromData(done.first().first().toByteArray());
        QCOMPARE(image.size(), QSize(64, 64)); QCOMPARE(image.pixelColor(0, 0), QColor(Qt::blue));
    }
    void missingIconIsEmpty() {
        OpenWave::WebsiteIconFetcher fetcher; QSignalSpy done(&fetcher, &OpenWave::WebsiteIconFetcher::finished);
        fetcher.fetch(page(), metadata({"/broken"}));
        QVERIFY(done.wait()); QVERIFY(done.first().first().toByteArray().isEmpty());
    }
    void brokerRejectsInvalidImages() {
        QVERIFY(OpenWaveWebApp::validIcon(png(512)));
        QVERIFY(!OpenWaveWebApp::validIcon(png(513)));
        QVERIFY(!OpenWaveWebApp::validIcon("<svg/>"));
        QVERIFY(!OpenWaveWebApp::validIcon(png(128).left(40)));
        QVERIFY(!OpenWaveWebApp::validIcon(QByteArray(1024 * 1024 + 1, 'x')));
    }
};
QTEST_GUILESS_MAIN(IconTests)
#include "tst_icons.moc"
