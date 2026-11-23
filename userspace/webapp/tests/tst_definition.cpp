#include "../WebAppDefinition.h"
#include <QtTest>
#include <QJsonDocument>

class DefinitionTests : public QObject {
    Q_OBJECT
private slots:
    void rejectUnsafeAddresses_data() {
        QTest::addColumn<QString>("text");
        for (const auto *text : {"", "about:overview", "file:///etc/passwd", "javascript:alert(1)",
             "data:text/html,hello", "https:///", "https://user:password@example.org/", "example.org"})
            QTest::newRow(text) << QString::fromUtf8(text);
        QTest::newRow("too-long") << "https://example.org/" + QString(2048, 'a');
    }
    void rejectUnsafeAddresses() {
        QFETCH(QString, text);
        QVERIFY(OpenWaveWebApp::address(text).isEmpty());
    }
    void preserveAddressAndSafeIdentity() {
        const QString text = "http://homeassistant.local:8123/dashboard?k=a%26b#room";
        const auto url = OpenWaveWebApp::address(text);
        QCOMPARE(url.toString(QUrl::FullyEncoded), text);
        QCOMPARE(OpenWaveWebApp::identifier(url), OpenWaveWebApp::identifier(QUrl(text)));
        QVERIFY(OpenWaveWebApp::identifier(url) != OpenWaveWebApp::identifier(QUrl(text + "2")));
        QVERIFY(OpenWaveWebApp::identifier(url).startsWith("org.openwave.webapp.h"));
        QVERIFY(OpenWaveWebApp::identifier(url).size() <= 128);
    }
    void namesAndSuggestions() {
        const QUrl url("https://example.org/");
        QCOMPARE(OpenWaveWebApp::suggestedName("\n Example\tDashboard ", url), QString("Example Dashboard"));
        QCOMPARE(OpenWaveWebApp::suggestedName("", url), QString("example.org"));
        QVERIFY(!OpenWaveWebApp::validName(""));
        QVERIFY(!OpenWaveWebApp::validName(" padded "));
        QVERIFY(!OpenWaveWebApp::validName("line\nbreak"));
        QVERIFY(!OpenWaveWebApp::validName(QString(257, 'a')));
        QVERIFY(!OpenWaveWebApp::validName(QString(QChar(0x202e)) + "misleading"));
        QVERIFY(OpenWaveWebApp::validName(QString::fromUtf8("خانة Dashboard")));
        const auto shortName = OpenWaveWebApp::suggestedName(QString(400, 'a'), url);
        QCOMPARE(shortName.size(), 256);
        QVERIFY(OpenWaveWebApp::validName(shortName));
    }
    void metadataNeverBecomesExecutableArguments() {
        const QString name = "A \"quoted\" $(example); dashboard";
        const auto url = OpenWaveWebApp::address("https://example.org/?q=%22hello%22&next=/path");
        const auto original = OpenWaveWebApp::manifest(name, url);
        const auto decoded = QJsonDocument::fromJson(QJsonDocument(original).toJson()).object();
        QCOMPARE(decoded.value("name").toString(), name);
        QCOMPARE(decoded.value("webApp").toObject().value("url").toString(), url.toString(QUrl::FullyEncoded));
        QCOMPARE(decoded.value("executable").toString(), QString("bin/openwave-webapp"));
        QVERIFY(!decoded.contains("arguments"));
        QVERIFY(decoded.value("permissions").toArray().isEmpty());
        QVERIFY(decoded.value("sandbox").toObject().value("enabled").toBool());
        QCOMPARE(decoded.value("sandbox").toObject().value("profile").toString(), QString("standard"));
    }
};
QTEST_GUILESS_MAIN(DefinitionTests)
#include "tst_definition.moc"
