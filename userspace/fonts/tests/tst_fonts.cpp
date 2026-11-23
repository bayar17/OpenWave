#include <OpenWave/FontManager.h>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <memory>

class FontTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(OpenWave::FontManager::initialize(qEnvironmentVariable("OPENWAVE_FONT_TEST_DIRECTORY")));
        QCOMPARE(QGuiApplication::font().family(), QStringLiteral("Source Sans Pro"));
    }
    void completeFamily()
    {
        OpenWave::FontManager fonts;
        QVERIFY(fonts.sourceSansProAvailable());
        QVERIFY(fonts.isAvailable(QStringLiteral("source sans pro")));
        QVERIFY(fonts.families().contains(fonts.family()));
        const auto styles = fonts.styles(fonts.systemFamily());
        QVERIFY(styles.contains(QStringLiteral("Regular")));
        QVERIFY(styles.contains(QStringLiteral("Bold")));
        QVERIFY(styles.contains(QStringLiteral("Italic")));
        QVERIFY(styles.contains(QStringLiteral("ExtraLight")));
    }
    void nativeFontFactory()
    {
        OpenWave::FontManager fonts;
        const QFont font = fonts.font(30, QFont::Light, true);
        QCOMPARE(font.family(), fonts.family());
        QCOMPARE(font.pixelSize(), 30);
        QCOMPARE(font.weight(), QFont::Light);
        QVERIFY(font.italic());
        QVERIFY(QFontInfo(font).family().startsWith(QStringLiteral("Source Sans Pro")));
        QCOMPARE(QFontInfo(font).weight(), QFont::Light);
        QVERIFY(QFontInfo(font).italic());
        QCOMPARE(fonts.font(-1, -1).pixelSize(), 1);
        QCOMPARE(fonts.font(9000, 9000).weight(), QFont::Weight(1000));
        QCOMPARE(fonts.font(20, QFont::Normal, false, QStringLiteral("missing-font")).family(), fonts.family());
        QVERIFY(fonts.styles(QStringLiteral("missing-font")).isEmpty());
    }
    void initializePreservesApplicationOverrides()
    {
        QFont custom = QGuiApplication::font();
        custom.setWeight(QFont::ExtraLight);
        QGuiApplication::setFont(custom);
        QVERIFY(OpenWave::FontManager::initialize());
        QCOMPARE(QGuiApplication::font().weight(), QFont::ExtraLight);
        custom.setWeight(QFont::Normal);
        QGuiApplication::setFont(custom);
    }
    void applicationFontChangeIsNotified()
    {
        OpenWave::FontManager fonts;
        QSignalSpy spy(&fonts, &OpenWave::FontManager::fontChanged);
        QFont font = QGuiApplication::font();
        const auto original = font;
        font.setPointSizeF(font.pointSizeF() + 1);
        QGuiApplication::setFont(font);
        QTRY_VERIFY(spy.count() > 0);
        QCOMPARE(fonts.defaultFont(), font);
        QGuiApplication::setFont(original);
    }
    void qmlModuleUsesSharedFont()
    {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(OPENWAVE_FONT_IMPORT_PATH));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import OpenWave.Fonts 1.0
            Text {
                text: "OpenWave"
                font: Fonts.font(31, Font.ExtraLight)
                property string sharedFamily: Fonts.family
                property bool available: Fonts.sourceSansProAvailable
            }
        )", QUrl(QStringLiteral("file:///font-test.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> text(component.create());
        QVERIFY2(text != nullptr, qPrintable(component.errorString()));
        QCOMPARE(text->property("sharedFamily").toString(), QStringLiteral("Source Sans Pro"));
        QVERIFY(text->property("available").toBool());
        const auto font = text->property("font").value<QFont>();
        QCOMPARE(font.family(), QStringLiteral("Source Sans Pro"));
        QCOMPARE(font.pixelSize(), 31);
        QCOMPARE(font.weight(), QFont::ExtraLight);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    FontTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_fonts.moc"
