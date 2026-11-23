#include "QuickMenuAppletManager.h"
#include "QuickMenuAppletRegistry.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

bool writeApplet(const QString &root, const QString &id, const QString &name, int order,
                 bool withSettings = false)
{
    QString text = QStringLiteral("[Desktop Entry]\nType=X-OpenWave-Applet\nName=%1\nX-OpenWave-Applet-Order=%2\n").arg(name).arg(order);
    if (withSettings) {
        text += QStringLiteral("X-OpenWave-Applet-Settings=Config.qml\n");
        if (!writeFile(root + QLatin1Char('/') + id + QStringLiteral("/Config.qml"),
                       "import QtQuick\nText { objectName: \"fixtureSettings\"; text: \"Fixture settings\" }\n"))
            return false;
    }
    return writeFile(root + QLatin1Char('/') + id + QStringLiteral("/MainApplet.qml"), "import QtQuick\nItem {}\n")
        && writeFile(root + QLatin1Char('/') + id + QStringLiteral("/applet.desktop"), text.toUtf8());
}

}

class QuickMenuModuleTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        const QString needed = m_directory.filePath(QStringLiteral("needed"));
        const QString system = m_directory.filePath(QStringLiteral("system"));
        const QString user = m_directory.filePath(QStringLiteral("user"));
        QVERIFY(writeApplet(needed, QStringLiteral("WiFi"), QStringLiteral("Wi-Fi"), 10));
        QVERIFY(writeApplet(system, QStringLiteral("Torch"), QStringLiteral("Torch"), 10, true));
        QVERIFY(writeApplet(user, QStringLiteral("Mine"), QStringLiteral("Mine"), 10, true));
        m_preferences = m_directory.filePath(QStringLiteral("prefs.ini"));
        m_registry.reset(new QuickMenuAppletRegistry({{QuickMenuAppletRegistry::Needed, needed},
                                                      {QuickMenuAppletRegistry::System, system},
                                                      {QuickMenuAppletRegistry::User, user}},
                                                     m_preferences));
        m_manager.reset(new QuickMenuAppletManager(m_registry.data()));
        m_engine.reset(new QQmlEngine);
        m_engine->addImportPath(QStringLiteral(STUB_IMPORT_DIR));
        m_engine->rootContext()->setContextProperty(QStringLiteral("QuickMenuApplets"), m_manager.data());

        QQmlComponent component(m_engine.data(), QUrl::fromLocalFile(QStringLiteral(SETTINGS_SOURCE_DIR "/QuickMenuPage.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{QStringLiteral("width"), 450}})));
        QVERIFY(m_host);
        m_window.reset(new QQuickWindow);
        m_window->resize(700, 1260);
        m_host->setParentItem(m_window->contentItem());
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.data()));
        m_messages.reset(new QSignalSpy(m_host.data(), SIGNAL(message(QString))));
    }

    void cleanup()
    {
        m_messages.reset();
        m_host.reset();
        m_window.reset();
        m_manager.reset();
        m_registry.reset();
        m_engine.reset();
        QDir(m_directory.path()).removeRecursively();
    }

    void listsEveryAppletWithItsKind()
    {
        QCOMPARE(text(row(QStringLiteral("WiFi")), "quickMenuAppletName"), QStringLiteral("Wi-Fi"));
        QCOMPARE(text(row(QStringLiteral("WiFi")), "quickMenuAppletSummary"), QStringLiteral("Required by the system"));
        QCOMPARE(text(row(QStringLiteral("Torch")), "quickMenuAppletSummary"), QStringLiteral("System applet"));
        QVERIFY(text(row(QStringLiteral("Mine")), "quickMenuAppletSummary").startsWith(QStringLiteral("Added by you")));
        QVERIFY(row(QStringLiteral("WiFi"))->height() > 0);
        QVERIFY(switchOn(QStringLiteral("WiFi")));
        QVERIFY(switchOn(QStringLiteral("Torch")));
        QVERIFY(!switchOn(QStringLiteral("Mine")));
    }

    void togglingASwitchPersistsTheChoice()
    {
        click(child(row(QStringLiteral("Mine")), "quickMenuAppletSwitch"));
        QTRY_VERIFY(switchOn(QStringLiteral("Mine")));
        QCOMPARE(QSettings(m_preferences, QSettings::IniFormat).value(QStringLiteral("quickmenu/applets/state/Mine")).toBool(), true);
        click(child(row(QStringLiteral("Torch")), "quickMenuAppletSwitch"));
        QTRY_VERIFY(!switchOn(QStringLiteral("Torch")));
        QVERIFY(!QSettings(m_preferences, QSettings::IniFormat).value(QStringLiteral("quickmenu/applets/state/Torch")).toBool());
    }

    void requiredAppletsCanBeHiddenButStayResident()
    {
        click(child(row(QStringLiteral("WiFi")), "quickMenuAppletSwitch"));
        QTRY_VERIFY(!switchOn(QStringLiteral("WiFi")));
        QCOMPARE(m_registry->residentApplets().size(), 1);
        QVERIFY(row(QStringLiteral("WiFi")));
    }

    void arrowsReorderApplets()
    {
        QCOMPARE(m_registry->orderedIds(), (QStringList{QStringLiteral("WiFi"), QStringLiteral("Torch"), QStringLiteral("Mine")}));
        click(child(row(QStringLiteral("Mine")), "quickMenuAppletUp"));
        QTRY_COMPARE(m_registry->orderedIds(), (QStringList{QStringLiteral("WiFi"), QStringLiteral("Mine"), QStringLiteral("Torch")}));
        click(child(row(QStringLiteral("WiFi")), "quickMenuAppletDown"));
        QTRY_COMPARE(m_registry->orderedIds(), (QStringList{QStringLiteral("Mine"), QStringLiteral("WiFi"), QStringLiteral("Torch")}));
    }

    void edgeArrowsDoNothing()
    {
        const QStringList before = m_registry->orderedIds();
        click(child(row(before.first()), "quickMenuAppletUp"));
        click(child(row(before.last()), "quickMenuAppletDown"));
        QTest::qWait(50);
        QCOMPARE(m_registry->orderedIds(), before);
        QCOMPARE(child(row(before.first()), "quickMenuAppletUp")->opacity() < 0.5, true);
        QCOMPARE(child(row(before.last()), "quickMenuAppletDown")->opacity() < 0.5, true);
    }

    void systemAppletsOfferTheirOwnSettingsPage()
    {
        QVERIFY(child(row(QStringLiteral("Torch")), "quickMenuAppletConfigure")->isVisible());
        QVERIFY(!child(row(QStringLiteral("WiFi")), "quickMenuAppletConfigure")->isVisible());
        QVERIFY(!child(row(QStringLiteral("Mine")), "quickMenuAppletConfigure")->isVisible());
        click(child(row(QStringLiteral("Torch")), "quickMenuAppletConfigure"));
        QTRY_VERIFY(findItem("fixtureSettings"));
        QVERIFY(findItem("fixtureSettings")->isVisible());
        QVERIFY(!findItem("quickMenuMainPane")->isVisible());
        QVERIFY(goBack());
        QTRY_VERIFY(findItem("quickMenuMainPane")->isVisible());
        QVERIFY(!goBack());
    }

    void hidingAnAppletHidesItsSettingsLink()
    {
        click(child(row(QStringLiteral("Torch")), "quickMenuAppletSwitch"));
        QTRY_VERIFY(!child(row(QStringLiteral("Torch")), "quickMenuAppletConfigure")->isVisible());
    }

    void resetRestoresDefaults()
    {
        click(child(row(QStringLiteral("Mine")), "quickMenuAppletSwitch"));
        click(child(row(QStringLiteral("Mine")), "quickMenuAppletUp"));
        QTRY_VERIFY(switchOn(QStringLiteral("Mine")));
        click(findItem("quickMenuReset"));
        QTRY_VERIFY(!switchOn(QStringLiteral("Mine")));
        QCOMPARE(m_registry->orderedIds(), (QStringList{QStringLiteral("WiFi"), QStringLiteral("Torch"), QStringLiteral("Mine")}));
    }

    void failuresShowAToast()
    {
        QFile::remove(m_preferences);
        QVERIFY(QDir().mkpath(m_preferences));
        click(child(row(QStringLiteral("Mine")), "quickMenuAppletSwitch"));
        QTRY_VERIFY(m_messages->count() > 0);
        QVERIFY(!switchOn(QStringLiteral("Mine")));
    }

    void skippedAppletsAreExplained()
    {
        QVERIFY(!findItem("quickMenuSkipped")->isVisible());
        QVERIFY(writeFile(m_directory.filePath(QStringLiteral("system/Broken/MainApplet.qml")), "import QtQuick\nItem {}\n"));
        QVERIFY(writeFile(m_directory.filePath(QStringLiteral("system/Broken/applet.desktop")), "[Desktop Entry]\nType=X-OpenWave-Applet\nX-OpenWave-Applet-ApiVersion=9\n"));
        m_registry->reload();
        QTRY_VERIFY(findItem("quickMenuSkipped")->isVisible());
        QVERIFY(m_registry->rejections().size() == 1);
    }

    void emptyRegistryShowsAMessage()
    {
        m_registry->setRoots({});
        QTRY_VERIFY(findItem("quickMenuEmpty")->isVisible());
    }

    void appletRowsFollowExternalChanges()
    {
        QVERIFY(!findItem("quickMenuApplet_Late"));
        QVERIFY(writeApplet(m_directory.filePath(QStringLiteral("system")), QStringLiteral("Late"), QStringLiteral("Late"), 50));
        m_registry->reload();
        QTRY_VERIFY(findItem("quickMenuApplet_Late"));
    }

private:
    static QQuickItem *find(QQuickItem *root, const QString &name)
    {
        if (!root)
            return nullptr;
        if (root->objectName() == name)
            return root;
        const QList<QQuickItem *> children = root->childItems();
        for (QQuickItem *item : children) {
            if (QQuickItem *match = find(item, name))
                return match;
        }
        return nullptr;
    }

    QQuickItem *findItem(const char *name) const
    {
        return find(m_host.data(), QString::fromLatin1(name));
    }

    QQuickItem *row(const QString &id) const
    {
        return find(m_host.data(), QStringLiteral("quickMenuApplet_") + id);
    }

    QQuickItem *child(QQuickItem *parent, const char *name) const
    {
        return find(parent, QString::fromLatin1(name));
    }

    QString text(QQuickItem *parent, const char *name) const
    {
        QQuickItem *item = parent && QString::fromLatin1(name) == parent->objectName() ? parent : child(parent, name);
        return item ? item->property("text").toString() : QString();
    }

    bool switchOn(const QString &id) const
    {
        QQuickItem *track = child(row(id), "quickMenuAppletSwitch");
        return track && track->property("color").value<QColor>() == QColor(QStringLiteral("#0759fa"));
    }

    bool goBack()
    {
        QVariant handled;
        const bool invoked = QMetaObject::invokeMethod(m_host.data(), "back", Q_RETURN_ARG(QVariant, handled));
        return invoked && handled.toBool();
    }

    void click(QQuickItem *item)
    {
        QVERIFY(item);
        const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(m_window.data(), Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTest::qWait(30);
    }

    QScopedPointer<QSignalSpy> m_messages;
    QTemporaryDir m_directory;
    QString m_preferences;
    QScopedPointer<QuickMenuAppletRegistry> m_registry;
    QScopedPointer<QuickMenuAppletManager> m_manager;
    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QQuickItem> m_host;
    QScopedPointer<QQuickWindow> m_window;
};

QTEST_MAIN(QuickMenuModuleTest)
#include "tst_quickmenumodule.moc"
