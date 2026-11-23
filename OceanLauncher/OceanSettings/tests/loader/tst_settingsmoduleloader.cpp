#include "SettingsModuleLoader.h"
#include "SettingsModuleRegistry.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

const QString WifiId = QStringLiteral("com.openwave.settings.wifi");
const QString BluetoothId = QStringLiteral("com.openwave.settings.bluetooth");

QJsonObject metadata(const QString &id, const QString &name, int order)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("order"), order},
        {QStringLiteral("apiVersion"), 1}
    };
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

QByteArray validQml(const QString &marker)
{
    return QStringLiteral("import QtQuick\nItem { property string marker: \"%1\" }\n")
        .arg(marker).toUtf8();
}

bool writeModule(const QString &root, const QString &directory, const QJsonObject &info,
                 const QByteArray &entry)
{
    const QString base = root + QLatin1Char('/') + directory;
    return writeFile(base + QStringLiteral("/module.json"), QJsonDocument(info).toJson())
        && writeFile(base + QStringLiteral("/MainModule.qml"), entry);
}

}

class SettingsModuleLoaderTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        QDir(m_directory.path()).removeRecursively();
        QVERIFY(QDir().mkpath(root()));
        m_registry.reset(new SettingsModuleRegistry(root()));
        m_engine.reset(new QQmlEngine);
        QQmlComponent component(m_engine.data());
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            Item {
                id: host
                width: 300
                height: 200
                property alias loader: moduleLoader
                SettingsModuleLoader { id: moduleLoader; host: host }
            }
        )", QUrl(QStringLiteral("qrc:/host.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(m_host);
        m_loader = m_host->property("loader").value<SettingsModuleLoader *>();
        QVERIFY(m_loader);
        m_loader->setRegistry(m_registry.data());
    }

    void cleanup()
    {
        m_host.reset();
        m_engine.reset();
        m_registry.reset();
    }

    void loadsValidModule()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QSignalSpy loadedSpy(m_loader, &SettingsModuleLoader::moduleLoaded);
        QSignalSpy failedSpy(m_loader, &SettingsModuleLoader::loadFailed);
        QVERIFY(m_loader->load(WifiId));
        QVERIFY(m_loader->loaded());
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Ready);
        QCOMPARE(m_loader->activeModuleId(), WifiId);
        QVERIFY(m_loader->errorString().isEmpty());
        QVERIFY(m_loader->item());
        QCOMPARE(m_loader->item()->parentItem(), m_host.data());
        QCOMPARE(m_loader->item()->property("marker").toString(), QStringLiteral("wifi"));
        QCOMPARE(m_loader->item()->width(), 300.0);
        QCOMPARE(m_loader->item()->height(), 200.0);
        m_host->setWidth(500);
        m_host->setHeight(400);
        QCOMPARE(m_loader->item()->width(), 500.0);
        QCOMPARE(m_loader->item()->height(), 400.0);
        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(loadedSpy.first().first().toString(), WifiId);
        QCOMPARE(failedSpy.count(), 0);
    }

    void loadsModuleWithSiblingComponents()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"),
                  "import QtQuick\nimport \"Components\"\nItem { property string marker: helper.value; Helper { id: helper } }\n");
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/Components/Helper.qml"),
                          "import QtQuick\nQtObject { property string value: \"from-component\" }\n"));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/Components/qmldir"), "Helper 1.0 Helper.qml\n"));
        m_registry->reload();
        QVERIFY2(m_loader->load(WifiId), qPrintable(m_loader->errorString()));
        QCOMPARE(m_loader->item()->property("marker").toString(), QStringLiteral("from-component"));
    }

    void rejectsUnknownModuleId()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QSignalSpy failedSpy(m_loader, &SettingsModuleLoader::loadFailed);
        QVERIFY(!m_loader->load(QStringLiteral("com.openwave.settings.nothing")));
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Error);
        QVERIFY(!m_loader->loaded());
        QVERIFY(!m_loader->item());
        QVERIFY(m_loader->activeModuleId().isEmpty());
        QVERIFY(m_loader->errorString().contains(QStringLiteral("unknown module")));
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!m_loader->load(QString()));
        QVERIFY(m_loader->status() == SettingsModuleLoader::Error);
    }

    void neverAcceptsPathsFromQml()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        const QString entry = root() + QStringLiteral("/Wifi/MainModule.qml");
        const QStringList attempts = {
            entry, QUrl::fromLocalFile(entry).toString(), QStringLiteral("../Wifi/MainModule.qml"),
            QStringLiteral("file:///etc/passwd"), QStringLiteral("qrc:/host.qml"),
            QStringLiteral("Wifi"), QStringLiteral("com.openwave.settings.wifi/../wifi")};
        for (const QString &attempt : attempts) {
            QVERIFY2(!m_loader->load(attempt), qPrintable(attempt));
            QVERIFY(!m_loader->loaded());
        }
    }

    void failsWithoutHost()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        m_loader->setHost(nullptr);
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("host")));
    }

    void rejectsEntryFileRemovedAfterDiscovery()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("missing")));
        QVERIFY(!m_loader->item());
    }

    void rejectsInvalidOrRemovedModulePaths()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        addModule(QStringLiteral("Bluetooth"), BluetoothId, QStringLiteral("Bluetooth"), validQml(QStringLiteral("bt")));
        QVERIFY(QDir(root() + QStringLiteral("/Wifi")).removeRecursively());
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(writeFile(root() + QStringLiteral("/Bluetooth/module.json"), "{ broken"));
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("JSON")));
        QVERIFY(QDir(root()).removeRecursively());
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("root")));
    }

    void rejectsMetadataChangedAfterDiscovery()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QJsonObject changed = metadata(QStringLiteral("com.openwave.settings.other"), QStringLiteral("Wi-Fi"), 1);
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/module.json"), QJsonDocument(changed).toJson()));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("changed")));

        QJsonObject retargeted = metadata(WifiId, QStringLiteral("Wi-Fi"), 1);
        retargeted[QStringLiteral("entry")] = QStringLiteral("Other.qml");
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/module.json"), QJsonDocument(retargeted).toJson()));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/Other.qml"), validQml(QStringLiteral("other"))));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("changed")));
    }

    void rejectsSymlinkEscapes()
    {
        const QString outside = m_directory.filePath(QStringLiteral("outside"));
        QVERIFY(writeFile(outside + QStringLiteral("/Evil.qml"), validQml(QStringLiteral("evil"))));
        QVERIFY(writeModule(outside, QStringLiteral("evil"), metadata(QStringLiteral("com.openwave.settings.evil"), QStringLiteral("Evil"), 1),
                            validQml(QStringLiteral("evil"))));

        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(QFile::link(outside + QStringLiteral("/Evil.qml"), root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(!m_loader->item());

        addModule(QStringLiteral("Bluetooth"), BluetoothId, QStringLiteral("Bluetooth"), validQml(QStringLiteral("bt")));
        QVERIFY(QDir(root() + QStringLiteral("/Bluetooth")).removeRecursively());
        QVERIFY(QFile::link(outside + QStringLiteral("/evil"), root() + QStringLiteral("/Bluetooth")));
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("symbolic")));
        QVERIFY(!m_loader->item());
    }

    void rejectsSymlinkMetadataSwap()
    {
        const QString outside = m_directory.filePath(QStringLiteral("outside"));
        QVERIFY(writeFile(outside + QStringLiteral("/module.json"),
                          QJsonDocument(metadata(WifiId, QStringLiteral("Wi-Fi"), 1)).toJson()));
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/module.json")));
        QVERIFY(QFile::link(outside + QStringLiteral("/module.json"), root() + QStringLiteral("/Wifi/module.json")));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("symbolic")));
    }

    void rejectsSymlinksThatStayInsideModule()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/Components/Real.qml"), validQml(QStringLiteral("real"))));
        QVERIFY(QFile::link(QStringLiteral("Real.qml"), root() + QStringLiteral("/Wifi/Components/Alias.qml")));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("symbolic link")));

        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/Components/Alias.qml")));
        QVERIFY2(m_loader->load(WifiId), qPrintable(m_loader->errorString()));
        m_loader->unload();

        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(QFile::link(QStringLiteral("Components/Real.qml"), root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(!m_loader->item());
    }

    void loadsEntryModifiedAfterDiscovery()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("before")));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/MainModule.qml"), validQml(QStringLiteral("after"))));
        QVERIFY(m_loader->load(WifiId));
        QCOMPARE(m_loader->item()->property("marker").toString(), QStringLiteral("after"));
    }

    void rejectsOversizedEntry()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QByteArray big = validQml(QStringLiteral("big"));
        big.append(QByteArray(3 * 1024 * 1024, ' '));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/MainModule.qml"), big));
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("larger")));
    }

    void rejectsInvalidQmlSyntax()
    {
        addModule(QStringLiteral("Broken"), WifiId, QStringLiteral("Broken"),
                  "import QtQuick\nItem { property int x: \n");
        QSignalSpy failedSpy(m_loader, &SettingsModuleLoader::loadFailed);
        QVERIFY(!m_loader->load(WifiId));
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Error);
        QVERIFY(m_loader->errorString().contains(QStringLiteral("MainModule.qml")));
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!m_loader->item());
    }

    void rejectsMissingImportsAndComponents()
    {
        addModule(QStringLiteral("Import"), WifiId, QStringLiteral("Import"),
                  "import QtQuick\nimport Does.Not.Exist 1.0\nItem {}\n");
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("Does.Not.Exist")));

        addModule(QStringLiteral("Missing"), BluetoothId, QStringLiteral("Missing"),
                  "import QtQuick\nItem { MissingComponent {} }\n");
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("MissingComponent")));
        QVERIFY(!m_loader->item());
    }

    void rejectsComponentCreationFailures()
    {
        addModule(QStringLiteral("NotItem"), WifiId, QStringLiteral("NotItem"),
                  "import QtQml\nQtObject {}\n");
        QVERIFY(!m_loader->load(WifiId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("Item")));

        addModule(QStringLiteral("BadBinding"), BluetoothId, QStringLiteral("BadBinding"),
                  "import QtQuick\nItem { property int broken: undefinedSymbol + 1 }\n");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*undefinedSymbol.*")));
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(m_loader->errorString().contains(QStringLiteral("undefinedSymbol")));
        QVERIFY(!m_loader->item());
        QVERIFY(m_loader->activeModuleId().isEmpty());
    }

    void unloadsActiveModule()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(m_loader->load(WifiId));
        QPointer<QQuickItem> item = m_loader->item();
        QVERIFY(item);
        QSignalSpy unloadedSpy(m_loader, &SettingsModuleLoader::moduleUnloaded);
        QSignalSpy loadedChanged(m_loader, &SettingsModuleLoader::loadedChanged);
        QSignalSpy idChanged(m_loader, &SettingsModuleLoader::activeModuleIdChanged);
        QSignalSpy itemChanged(m_loader, &SettingsModuleLoader::itemChanged);
        m_loader->unload();
        QVERIFY(!m_loader->loaded());
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Idle);
        QVERIFY(m_loader->activeModuleId().isEmpty());
        QVERIFY(!m_loader->item());
        QCOMPARE(unloadedSpy.count(), 1);
        QCOMPARE(unloadedSpy.first().first().toString(), WifiId);
        QCOMPARE(loadedChanged.count(), 1);
        QCOMPARE(idChanged.count(), 1);
        QCOMPARE(itemChanged.count(), 1);
        QTRY_VERIFY(item.isNull());
        QCOMPARE(m_host->childItems().size(), 0);
        QTRY_COMPARE(m_loader->findChildren<QQmlComponent *>().size(), 0);
        QTRY_COMPARE(m_loader->findChildren<QQmlContext *>().size(), 0);
        m_loader->unload();
        QCOMPARE(unloadedSpy.count(), 1);
    }

    void switchesBetweenModules()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        addModule(QStringLiteral("Bluetooth"), BluetoothId, QStringLiteral("Bluetooth"), validQml(QStringLiteral("bt")));
        QVERIFY(m_loader->load(WifiId));
        QPointer<QQuickItem> first = m_loader->item();
        m_events.clear();
        connect(m_loader, &SettingsModuleLoader::moduleUnloaded, this,
                [this](const QString &id) { m_events << QStringLiteral("unloaded:") + id; });
        connect(m_loader, &SettingsModuleLoader::moduleLoaded, this,
                [this](const QString &id) { m_events << QStringLiteral("loaded:") + id; });
        QVERIFY(m_loader->load(BluetoothId));
        QCOMPARE(m_events, (QStringList{QStringLiteral("unloaded:") + WifiId, QStringLiteral("loaded:") + BluetoothId}));
        QCOMPARE(m_loader->activeModuleId(), BluetoothId);
        QCOMPARE(m_loader->item()->property("marker").toString(), QStringLiteral("bt"));
        QTRY_VERIFY(first.isNull());
        QCOMPARE(m_host->childItems().size(), 1);
        for (int index = 0; index < 20; ++index)
            QVERIFY(m_loader->load(index % 2 ? BluetoothId : WifiId));
        QTRY_COMPARE(m_host->childItems().size(), 1);
    }

    void repeatedRequestKeepsActiveModule()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(m_loader->load(WifiId));
        QQuickItem *item = m_loader->item();
        QSignalSpy loadedSpy(m_loader, &SettingsModuleLoader::moduleLoaded);
        QSignalSpy unloadedSpy(m_loader, &SettingsModuleLoader::moduleUnloaded);
        QSignalSpy statusSpy(m_loader, &SettingsModuleLoader::statusChanged);
        QVERIFY(m_loader->load(WifiId));
        QVERIFY(m_loader->load(WifiId));
        QCOMPARE(m_loader->item(), item);
        QCOMPARE(m_host->childItems().size(), 1);
        QCOMPARE(loadedSpy.count(), 0);
        QCOMPARE(unloadedSpy.count(), 0);
        QCOMPARE(statusSpy.count(), 0);
    }

    void notifiesStateChangesInOrder()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QSignalSpy statusSpy(m_loader, &SettingsModuleLoader::statusChanged);
        QSignalSpy loadedChanged(m_loader, &SettingsModuleLoader::loadedChanged);
        QSignalSpy idChanged(m_loader, &SettingsModuleLoader::activeModuleIdChanged);
        QSignalSpy itemChanged(m_loader, &SettingsModuleLoader::itemChanged);
        QSignalSpy errorChanged(m_loader, &SettingsModuleLoader::errorStringChanged);
        QVERIFY(m_loader->load(WifiId));
        QCOMPARE(statusSpy.count(), 2);
        QCOMPARE(loadedChanged.count(), 1);
        QCOMPARE(idChanged.count(), 1);
        QCOMPARE(itemChanged.count(), 1);
        QCOMPARE(errorChanged.count(), 0);
        QVERIFY(!m_loader->load(QStringLiteral("com.openwave.settings.nothing")));
        QCOMPARE(errorChanged.count(), 1);
        QCOMPARE(idChanged.count(), 2);
        QCOMPARE(loadedChanged.count(), 2);
        QVERIFY(m_loader->load(WifiId));
        QCOMPARE(errorChanged.count(), 2);
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Ready);
    }

    void failedLoadLeavesNoStaleState()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        addModule(QStringLiteral("Broken"), BluetoothId, QStringLiteral("Broken"), "import QtQuick\nItem {");
        QVERIFY(m_loader->load(WifiId));
        QPointer<QQuickItem> previous = m_loader->item();
        QVERIFY(!m_loader->load(BluetoothId));
        QVERIFY(!m_loader->loaded());
        QVERIFY(!m_loader->item());
        QVERIFY(m_loader->activeModuleId().isEmpty());
        QCOMPARE(m_loader->status(), SettingsModuleLoader::Error);
        QVERIFY(!m_loader->errorString().isEmpty());
        QTRY_VERIFY(previous.isNull());
        QCOMPARE(m_host->childItems().size(), 0);
        QTRY_COMPARE(m_loader->findChildren<QQmlComponent *>().size(), 0);
        QTRY_COMPARE(m_loader->findChildren<QQmlContext *>().size(), 0);
        QVERIFY(m_loader->load(WifiId));
        QVERIFY(m_loader->loaded());
        QVERIFY(m_loader->errorString().isEmpty());
        QCOMPARE(m_host->childItems().size(), 1);
    }

    void survivesRepeatedFailures()
    {
        addModule(QStringLiteral("Broken"), BluetoothId, QStringLiteral("Broken"), "import QtQuick\nItem {");
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        for (int index = 0; index < 50; ++index) {
            QVERIFY(!m_loader->load(BluetoothId));
            QVERIFY(!m_loader->load(QStringLiteral("com.openwave.settings.nothing%1").arg(index)));
        }
        QVERIFY(m_loader->load(WifiId));
        QVERIFY(m_loader->loaded());
        QCOMPARE(m_host->childItems().size(), 1);
    }

    void hostDestructionClearsState()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(m_loader->load(WifiId));
        QPointer<SettingsModuleLoader> loader = m_loader;
        QSignalSpy unloadedSpy(m_loader, &SettingsModuleLoader::moduleUnloaded);
        m_host.reset();
        QVERIFY(loader.isNull() || !loader->loaded());
        if (!loader.isNull())
            QCOMPARE(unloadedSpy.count(), 1);
    }

    void changingHostUnloadsModule()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        QVERIFY(m_loader->load(WifiId));
        QQuickItem otherHost;
        m_loader->setHost(&otherHost);
        QVERIFY(!m_loader->loaded());
        QCOMPARE(m_host->childItems().size(), 0);
        QVERIFY(m_loader->load(WifiId));
        QCOMPARE(m_loader->item()->parentItem(), &otherHost);
        m_loader->setHost(m_host.data());
        QVERIFY(!m_loader->loaded());
    }

    void hostComponentSwitchesModules()
    {
        addModule(QStringLiteral("Wifi"), WifiId, QStringLiteral("Wi-Fi"), validQml(QStringLiteral("wifi")));
        addModule(QStringLiteral("Broken"), BluetoothId, QStringLiteral("Broken"), "import QtQuick\nItem {");
        SettingsModuleRegistry::shared()->setModuleRoot(root());
        QQmlComponent component(m_engine.data());
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            SettingsModuleHost {
                width: 320
                height: 240
                moduleId: ""
            }
        )", QUrl(QStringLiteral("qrc:/hostcomponent.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QQuickItem> hostItem(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(hostItem);
        QVERIFY(!hostItem->property("loaded").toBool());

        hostItem->setProperty("moduleId", WifiId);
        QVERIFY(hostItem->property("loaded").toBool());
        QCOMPARE(hostItem->childItems().size() >= 1, true);

        hostItem->setProperty("moduleId", BluetoothId);
        QVERIFY(!hostItem->property("loaded").toBool());
        QVERIFY(!hostItem->property("errorString").toString().isEmpty());
        QQuickItem *errorText = hostItem->findChild<QQuickItem *>(QStringLiteral("settingsModuleError"));
        QVERIFY(errorText);
        QVERIFY(errorText->isVisible());

        hostItem->setProperty("moduleId", WifiId);
        QVERIFY(hostItem->property("loaded").toBool());
        QVERIFY(!errorText->isVisible());

        hostItem->setProperty("moduleId", QString());
        QVERIFY(!hostItem->property("loaded").toBool());
        SettingsModuleRegistry::shared()->setModuleRoot(QString());
    }

private:
    void addModule(const QString &directory, const QString &id, const QString &name,
                   const QByteArray &entry)
    {
        QVERIFY(writeModule(root(), directory, metadata(id, name, 10), entry));
        m_registry->reload();
        QVERIFY(m_registry->count() >= 1);
    }

    QString root() const { return m_directory.filePath(QStringLiteral("Modules")); }

    QTemporaryDir m_directory;
    QScopedPointer<SettingsModuleRegistry> m_registry;
    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QQuickItem> m_host;
    SettingsModuleLoader *m_loader = nullptr;
    QStringList m_events;
};

QTEST_MAIN(SettingsModuleLoaderTest)
#include "tst_settingsmoduleloader.moc"
