#include <QtTest>
#include <QFile>
#include <QJSEngine>

class SearchCompatibilityTests : public QObject {
    Q_OBJECT
private slots:
    void documentScope_data() {
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("applied");
        QTest::newRow("all-results") << "www.google.com" << "/search" << true;
        QTest::newRow("regional") << "www.google.com.tr" << "/search" << true;
        QTest::newRow("uk") << "www.google.co.uk" << "/search" << true;
        QTest::newRow("germany") << "google.de" << "/search" << true;
        QTest::newRow("images") << "images.google.com" << "/search" << true;
        QTest::newRow("homepage-to-search") << "www.google.com" << "/" << true;
        QTest::newRow("alternate-homepage") << "www.google.com" << "/webhp" << true;
        QTest::newRow("image-preview") << "www.google.com" << "/imgres" << true;
        QTest::newRow("other-site") << "example.org" << "/search" << false;
        QTest::newRow("suffix-lookalike") << "www.google.com.example.org" << "/search" << false;
        QTest::newRow("prefix-lookalike") << "notgoogle.com" << "/search" << false;
        QTest::newRow("other-google-page") << "www.google.com" << "/maps" << false;
        QTest::newRow("path-lookalike") << "www.google.com" << "/searching" << false;
    }
    void documentScope() {
        QFETCH(QString, host); QFETCH(QString, path); QFETCH(bool, applied);
        QFile file(QStringLiteral(SEARCH_COMPATIBILITY_SOURCE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString source = QString::fromUtf8(file.readAll());
        source.remove(0, source.indexOf('\n') + 1); // QML library pragma
        QJSEngine engine;
        QVERIFY(!engine.evaluate(source).isError());
        const QString script = engine.globalObject().property("imagePaintScript").toString();
        QVERIFY(!script.isEmpty());
        engine.globalObject().setProperty("host", host);
        engine.globalObject().setProperty("path", path);
        QVERIFY(!engine.evaluate(QStringLiteral(
            "var location={hostname:host,pathname:path}; var styles=[];"
            "var document={head:{appendChild:function(s){styles.push(s);}},"
            "getElementById:function(id){return styles.find(function(s){return s.id===id;});},"
            "createElement:function(){return {};}};")).isError());
        const QJSValue result = engine.evaluate(script);
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QCOMPARE(engine.evaluate("styles.length").toInt(), applied ? 1 : 0);
        // In-page filter changes reuse the stylesheet rather than adding copies.
        QVERIFY(!engine.evaluate(script).isError());
        QCOMPARE(engine.evaluate("styles.length").toInt(), applied ? 1 : 0);
    }
};
QTEST_GUILESS_MAIN(SearchCompatibilityTests)
#include "tst_searchcompatibility.moc"
