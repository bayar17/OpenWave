#include "SettingsModuleLoader.h"
#include "SettingsModuleRegistry.h"

#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

const QString BluetoothId = QStringLiteral("com.openwave.settings.bluetooth");
const QString PathA = QStringLiteral("/org/bluez/hci0/dev_01_02_03_04_05_06");
const QString PathB = QStringLiteral("/org/bluez/hci0/dev_02_02_03_04_05_06");
const QString PathC = QStringLiteral("/org/bluez/hci0/dev_03_02_03_04_05_06");
const QString PathD = QStringLiteral("/org/bluez/hci0/dev_04_02_03_04_05_06");

QVariantMap device(const QString &path, const QString &address, const QString &name,
                   bool paired, bool connected, const QString &alias = QString())
{
    return {
        {QStringLiteral("path"), path}, {QStringLiteral("address"), address},
        {QStringLiteral("name"), name}, {QStringLiteral("alias"), alias},
        {QStringLiteral("paired"), paired}, {QStringLiteral("connected"), connected}
    };
}

QVariantList fixture()
{
    return {
        device(PathA, QStringLiteral("01:02:03:04:05:06"), QStringLiteral("WH-1000XM5"), true, true),
        device(PathB, QStringLiteral("02:02:03:04:05:06"), QStringLiteral("JBL Go 4"), true, false),
        device(PathC, QStringLiteral("03:02:03:04:05:06"), QStringLiteral("My VW 1841"), true, false),
        device(PathD, QStringLiteral("04:02:03:04:05:06"), QString(), false, false)
    };
}

}

class BluetoothModuleTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_engine.reset(new QQmlEngine);
        m_engine->addImportPath(QStringLiteral(STUB_IMPORT_DIR));
        m_mock = createMock(QStringLiteral("MockConnectivity.qml"));
        m_prompts = createMock(QStringLiteral("MockPrompts.qml"));
        m_environment = createMock(QStringLiteral("MockEnvironment.qml"));
        QVERIFY(m_mock && m_prompts && m_environment);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Connectivity"), m_mock);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), m_prompts);
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsEnvironment"), m_environment);
        SettingsModuleRegistry::shared()->setModuleRoot(QStringLiteral(MODULES_DIR));

        QQmlComponent component(m_engine.data());
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            SettingsModuleHost { width: 672; height: 900 }
        )", QUrl(QStringLiteral("qrc:/bluetoothhost.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(m_host);
        m_window.reset(new QQuickWindow);
        m_window->resize(700, 960);
        m_host->setParentItem(m_window->contentItem());
        m_window->show();
    }

    void cleanup()
    {
        m_host.reset();
        m_window.reset();
        SettingsModuleRegistry::shared()->setModuleRoot(QString());
        delete m_mock;
        delete m_prompts;
        delete m_environment;
        m_mock = m_prompts = m_environment = nullptr;
        m_engine.reset();
    }

    void discoversBluetoothModuleMetadataNextToWifi()
    {
        SettingsModuleRegistry *registry = SettingsModuleRegistry::shared();
        const QVariantMap info = registry->module(BluetoothId);
        QVERIFY(!info.isEmpty());
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("Bluetooth"));
        QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral("Connectivity"));
        QCOMPARE(info.value(QStringLiteral("apiVersion")).toInt(), SettingsModuleRegistry::SupportedApiVersion);
        QCOMPARE(info.value(QStringLiteral("description")).toString(), QStringLiteral("Nearby devices"));
        QStringList ids;
        for (const QVariant &entry : registry->modules())
            ids << entry.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ids.size() >= 2);
        QCOMPARE(ids.mid(0, 2), (QStringList{QStringLiteral("com.openwave.settings.wifi"), BluetoothId}));
        QVERIFY(registry->entryUrl(BluetoothId).path().endsWith(QStringLiteral("/Bluetooth/MainModule.qml")));
    }

    void loadsThroughRealLoader()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(loader()->activeModuleId(), BluetoothId);
        QCOMPARE(loader()->item()->objectName(), QStringLiteral("bluetoothModule"));
        QVERIFY(find(QStringLiteral("bluetoothSwitch")));
        QVERIFY(find(QStringLiteral("bluetoothRefresh")));
    }

    void headingShowsDeviceNameAndSwitchTogglesRadio()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(text(find(QStringLiteral("bluetoothHeadingPrefix"))), QStringLiteral("Visible as "));
        QCOMPARE(text(find(QStringLiteral("bluetoothHeadingName"))), QStringLiteral("KB Phone"));
        QVERIFY(find(QStringLiteral("bluetoothHeadingName"))->width() > 20);
        QVERIFY(find(QStringLiteral("bluetoothHeadingName"))->width() <= find(QStringLiteral("bluetoothHeadingName"))->property("implicitWidth").toReal());
        QTRY_VERIFY(find(QStringLiteral("bluetoothSwitch"))->width() >= find(QStringLiteral("bluetoothHeadingPrefix"))->width() + find(QStringLiteral("bluetoothHeadingName"))->width() + 100);
        QTRY_VERIFY(find(QStringLiteral("bluetoothSwitchTrack"))->x() >= find(QStringLiteral("bluetoothHeadingPrefix"))->width() + find(QStringLiteral("bluetoothHeadingName"))->width());
        QCOMPARE(find(QStringLiteral("bluetoothSwitchTrack"))->property("color").value<QColor>(), QColor("#0759fa"));
        m_environment->setProperty("deviceName", QStringLiteral("KB's Phone"));
        QCOMPARE(text(find(QStringLiteral("bluetoothHeadingName"))), QStringLiteral("KB's Phone"));
        QVERIFY(find(QStringLiteral("bluetoothHeadingName"))->width() > 20);
        QQuickItem *toggle = find(QStringLiteral("bluetoothSwitch"));
        QVERIFY(toggle->isEnabled());
        QMetaObject::invokeMethod(toggle, "simulateUserToggle");
        QCOMPARE(m_mock->property("bluetoothEnabled").toBool(), false);
        QCOMPARE(find(QStringLiteral("bluetoothSwitchTrack"))->property("color").value<QColor>(), QColor("#777e83"));
        QVERIFY(m_mock->property("bluetoothAction").toString().contains(QStringLiteral("power:false")));
        QMetaObject::invokeMethod(toggle, "simulateUserToggle");
        QCOMPARE(m_mock->property("bluetoothEnabled").toBool(), true);
    }

    void statusTextAndDisabledStates()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(!find(QStringLiteral("bluetoothStatusText"))->isVisible());
        m_mock->setProperty("bluetoothDevices", QVariantList{});
        QVERIFY(find(QStringLiteral("bluetoothStatusText"))->isVisible());
        QCOMPARE(text(find(QStringLiteral("bluetoothStatusText"))), QStringLiteral("No devices found. Tap refresh to search."));
        m_mock->setProperty("bluetoothEnabled", false);
        m_mock->setProperty("bluetoothStatus", QStringLiteral("Bluetooth is off."));
        QVERIFY(find(QStringLiteral("bluetoothStatusText"))->isVisible());
        QCOMPARE(text(find(QStringLiteral("bluetoothStatusText"))), QStringLiteral("Bluetooth is off."));
        m_mock->setProperty("bluetoothAdapterAvailable", false);
        m_mock->setProperty("bluetoothStatus", QStringLiteral("No Bluetooth adapter found."));
        QVERIFY(!find(QStringLiteral("bluetoothSwitch"))->isEnabled());
        m_mock->setProperty("serviceAvailable", false);
        QVERIFY(!find(QStringLiteral("bluetoothSwitch"))->isEnabled());
    }

    void devicesUseConnectivityService()
    {
        m_mock->setProperty("bluetoothDevices", QVariantList{device(PathA, QStringLiteral("01:02:03:04:05:06"), QStringLiteral("Test headphones"), false, false, QStringLiteral("Test headphones"))});
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QQuickItem *row = find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"));
        QVERIFY(row);
        QVERIFY(!find(QStringLiteral("bluetoothStatusText"))->isVisible());
        QMetaObject::invokeMethod(row, "clicked");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("pair:") + PathA);
        QCOMPARE(row->property("statusText").toString(), QStringLiteral("Pairing…"));
        m_mock->setProperty("bluetoothActionState", QStringLiteral("connecting"));
        m_mock->setProperty("bluetoothDevices", QVariantList{device(PathA, QStringLiteral("01:02:03:04:05:06"), QStringLiteral("Test headphones"), true, false)});
        QTRY_COMPARE(find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"))->property("statusText").toString(), QString::fromUtf8("Connecting…"));
        m_mock->setProperty("bluetoothActionState", QString());
        m_mock->setProperty("busy", false);
        m_mock->setProperty("bluetoothDevices", QVariantList{device(PathA, QStringLiteral("01:02:03:04:05:06"), QStringLiteral("Test headphones"), true, true)});
        QTRY_COMPARE(find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"))->property("statusText").toString(), QStringLiteral("Connected"));
    }

    void pairedDeviceConnectsOnTapAndBusyBlocksTaps()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothDevice_02:02:03:04:05:06")), "clicked");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("connect:") + PathB);
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothDevice_04:02:03:04:05:06")), "clicked");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QString());
    }

    void discoveryStartsWhenPowerBecomesReady()
    {
        m_mock->setProperty("bluetoothEnabled", false);
        m_mock->setProperty("bluetoothStatus", QStringLiteral("Bluetooth is off."));
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("power:true"));
        QVERIFY(loader()->item()->property("scanWanted").toBool());
        QVERIFY(!loader()->item()->property("scanRequested").toBool());
        QMetaObject::invokeMethod(m_mock, "changed");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("discover"));
        QVERIFY(loader()->item()->property("scanRequested").toBool());
        QCOMPARE(find(QStringLiteral("bluetoothRefreshSymbol"))->property("kind").toString(), QStringLiteral("scan"));
    }

    void startsDiscoveryImmediatelyWhenAlreadyPowered()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("discover"));
        QVERIFY(m_mock->property("bluetoothDiscovering").toBool());
    }

    void userDisablingPreventsAutomaticDiscovery()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothSwitch")), "simulateUserToggle");
        QVERIFY(!m_mock->property("bluetoothDiscovering").toBool());
        QVERIFY(loader()->item()->property("userDisabled").toBool());
        m_mock->setProperty("bluetoothAction", QString());
        loader()->item()->setProperty("scanStarted", false);
        QMetaObject::invokeMethod(m_mock, "changed");
        QVERIFY(!m_mock->property("bluetoothAction").toString().contains(QStringLiteral("discover")));
    }

    void refreshButtonRestartsOrRefreshes()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        const int refreshes = m_mock->property("refreshCount").toInt();
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothRefresh")), "clicked");
        QVERIFY(m_mock->property("refreshCount").toInt() > refreshes);
        m_mock->setProperty("bluetoothDiscovering", false);
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothRefresh")), "clicked");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("discover"));
        m_mock->setProperty("bluetoothAdapterAvailable", false);
        const int again = m_mock->property("refreshCount").toInt();
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothRefresh")), "clicked");
        QVERIFY(m_mock->property("refreshCount").toInt() > again);
    }

    void groupsRememberedAboveDiscoveredAndLabelsUnnamedByAddress()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QQuickItem *connected = find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"));
        QQuickItem *saved = find(QStringLiteral("bluetoothDevice_02:02:03:04:05:06"));
        QQuickItem *discovered = find(QStringLiteral("bluetoothDevice_04:02:03:04:05:06"));
        QQuickItem *divider = find(QStringLiteral("bluetoothSeparator"));
        QVERIFY(connected && saved && discovered && divider);
        QTRY_VERIFY(connected->y() < divider->y());
        QVERIFY(saved->y() < divider->y());
        QVERIFY(discovered->y() > divider->y());
        QCOMPARE(saved->property("statusText").toString(), QString());
        QCOMPARE(connected->property("statusText").toString(), QStringLiteral("Connected"));
        QCOMPARE(discovered->property("deviceLabel").toString(), QStringLiteral("04:02:03:04:05:06"));
        QCOMPARE(text(find(QStringLiteral("bluetoothDiscoveredHeading"))), QStringLiteral("Discovered devices"));
        QCOMPARE(connected->height(), 110.0);
        QCOMPARE(saved->height(), 90.0);
    }

    void holdingRememberedDeviceOpensDetailsWithoutDisconnecting()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QQuickItem *connected = find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"));
        QQuickItem *discovered = find(QStringLiteral("bluetoothDevice_04:02:03:04:05:06"));
        QMetaObject::invokeMethod(discovered, "longPressed");
        QVERIFY(!pageProperty("infoOpen").toBool());
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(connected, "longPressed");
        QVERIFY(pageProperty("infoOpen").toBool());
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QString());
        QVERIFY(find(QStringLiteral("bluetoothInfoPane"))->isVisible());
        QVERIFY(!find(QStringLiteral("bluetoothMainPane"))->isVisible());
        QCOMPARE(text(find(QStringLiteral("bluetoothInfoName"))), QStringLiteral("WH-1000XM5"));
        QVERIFY(find(QStringLiteral("bluetoothDisconnect"))->isVisible());
        QVERIFY(!find(QStringLiteral("bluetoothConnect"))->isVisible());
        QCOMPARE(find(QStringLiteral("bluetoothInfoStatus"))->property("detail").toString(), QStringLiteral("Connected"));
        QVERIFY(call0("back").toBool());
        QVERIFY(!pageProperty("infoOpen").toBool());
        QVERIFY(find(QStringLiteral("bluetoothMainPane"))->isVisible());
        QVERIFY(!call0("back").toBool());
    }

    void tappingConnectedDeviceOpensDetails()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06")), "clicked");
        QVERIFY(pageProperty("infoOpen").toBool());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothInfoBack")), "clicked");
        QVERIFY(!pageProperty("infoOpen").toBool());
    }

    void nicknameForgetAndDisconnectUseSharedPrompts()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(0);
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothNickname")), "clicked");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("input"));
        QCOMPARE(m_prompts->property("options").toMap().value(QStringLiteral("initialText")).toString(), QStringLiteral("WH-1000XM5"));
        respond(QStringLiteral("cancelled"), QStringLiteral("Other"));
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothNickname")), "clicked");
        respond(QStringLiteral("accepted"), QStringLiteral("My headphones"));
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("rename:") + PathA + QStringLiteral(":My headphones"));
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothForget")), "clicked");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("confirmation"));
        QCOMPARE(m_prompts->property("options").toMap().value(QStringLiteral("acceptLabel")).toString(), QStringLiteral("Forget"));
        respond(QStringLiteral("cancelled"), QString());
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothDisconnect")), "clicked");
        QVERIFY(!m_prompts->property("busy").toBool());
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("disconnect:") + PathA);
        m_mock->setProperty("bluetoothAction", QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothForget")), "clicked");
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("forget:") + PathA);
        QVERIFY(!pageProperty("infoOpen").toBool());
    }

    void connectRowAppearsForDisconnectedRememberedDevice()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(1);
        QVERIFY(!find(QStringLiteral("bluetoothDisconnect"))->isVisible());
        QVERIFY(find(QStringLiteral("bluetoothConnect"))->isVisible());
        QCOMPARE(find(QStringLiteral("bluetoothInfoStatus"))->property("detail").toString(), QStringLiteral("Previously connected"));
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothConnect")), "clicked");
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QStringLiteral("connect:") + PathB);
        QVERIFY(!find(QStringLiteral("bluetoothNickname"))->isEnabled());
        QCOMPARE(find(QStringLiteral("bluetoothInfoStatus"))->property("detail").toString(), QString::fromUtf8("Connecting…"));
    }

    void staleResultsAndVanishedDevicesAreIgnored()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(1);
        call1("requestPrompt", QStringLiteral("forget"));
        m_mock->setProperty("bluetoothDevices", QVariantList{});
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_mock->property("bluetoothAction").toString().contains(QStringLiteral("forget")), false);
        QVERIFY(!pageProperty("infoOpen").toBool());
        QVERIFY(find(QStringLiteral("bluetoothMainPane"))->isVisible());

        m_mock->setProperty("bluetoothDevices", fixture());
        openInfo(1);
        call1("requestPrompt", QStringLiteral("nickname"));
        call0("closeInfo");
        QVERIFY(!m_prompts->property("busy").toBool());
        respond(QStringLiteral("accepted"), QStringLiteral("Late"));
        QCOMPARE(m_mock->property("bluetoothAction").toString().contains(QStringLiteral("rename")), false);
    }

    void promptResultForAnotherDeviceIsIgnored()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(1);
        call1("requestPrompt", QStringLiteral("forget"));
        QVERIFY(m_prompts->property("busy").toBool());
        openInfo(0);
        QCOMPARE(pageProperty("selectedPath").toString(), PathA);
        m_mock->setProperty("bluetoothAction", QString());
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_mock->property("bluetoothAction").toString(), QString());
        QVERIFY(pageProperty("infoOpen").toBool());
    }

    void infoClosesWhenAdapterDisappears()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(0);
        QVERIFY(pageProperty("infoOpen").toBool());
        m_mock->setProperty("bluetoothEnabled", false);
        QMetaObject::invokeMethod(m_mock, "changed");
        QVERIFY(!pageProperty("infoOpen").toBool());
    }

    void promptRefusedWhenBackendBusyOrServiceMissing()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(0);
        m_mock->setProperty("busy", true);
        call1("requestPrompt", QStringLiteral("forget"));
        QVERIFY(!m_prompts->property("busy").toBool());
        m_mock->setProperty("busy", false);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), static_cast<QObject *>(nullptr));
        call0("closeInfo");
        m_host->setProperty("moduleId", QString());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        openInfo(0);
        call1("requestPrompt", QStringLiteral("forget"));
        QVERIFY(find(QStringLiteral("bluetoothToastText"))->isVisible() || find(QStringLiteral("bluetoothToastText"))->parentItem()->isVisible());
        QVERIFY(text(find(QStringLiteral("bluetoothToastText"))).contains(QStringLiteral("prompt")));
    }

    void backendErrorsShowAsToast()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(!find(QStringLiteral("bluetoothToastText"))->parentItem()->isVisible());
        QMetaObject::invokeMethod(m_mock, "errorOccurred", Q_ARG(QString, QStringLiteral("Pairing failed.")));
        QVERIFY(find(QStringLiteral("bluetoothToastText"))->parentItem()->isVisible());
        QCOMPARE(text(find(QStringLiteral("bluetoothToastText"))), QStringLiteral("Pairing failed."));
    }

    void largeTextEnlargesRows()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(find(QStringLiteral("bluetoothDevice_02:02:03:04:05:06"))->height(), 90.0);
        m_environment->setProperty("largeText", true);
        QCOMPARE(find(QStringLiteral("bluetoothDevice_02:02:03:04:05:06"))->height(), 104.0);
        QCOMPARE(find(QStringLiteral("bluetoothDevice_01:02:03:04:05:06"))->height(), 124.0);
    }

    void toleratesMissingServices()
    {
        m_engine->rootContext()->setContextProperty(QStringLiteral("Connectivity"), static_cast<QObject *>(nullptr));
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsEnvironment"), static_cast<QObject *>(nullptr));
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(!find(QStringLiteral("bluetoothSwitch"))->isEnabled());
        QCOMPARE(text(find(QStringLiteral("bluetoothStatusText"))), QStringLiteral("Bluetooth is unavailable."));
        QCOMPARE(text(find(QStringLiteral("bluetoothHeadingName"))), QString());
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothRefresh")), "clicked");
        QVERIFY(loader()->loaded());
    }

    void pollsAtConfiguredIntervalWhileLoaded()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        loader()->item()->setProperty("refreshIntervalMs", 100);
        const int before = m_mock->property("refreshCount").toInt();
        QTRY_VERIFY_WITH_TIMEOUT(m_mock->property("refreshCount").toInt() > before + 2, 2000);
    }

    void discoveryStopsAfterConfiguredWindow()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(m_mock->property("bluetoothDiscovering").toBool());
        loader()->item()->setProperty("scanDurationMs", 100);
        QMetaObject::invokeMethod(find(QStringLiteral("bluetoothRefresh")), "clicked");
        QTRY_VERIFY_WITH_TIMEOUT(!m_mock->property("bluetoothDiscovering").toBool(), 1500);
    }

    void leavingTheTabStopsDiscoveryCancelsPromptsAndFreesTheModule()
    {
        m_mock->setProperty("bluetoothDevices", fixture());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(m_mock->property("bluetoothDiscovering").toBool());
        QPointer<QQuickItem> page = loader()->item();
        openInfo(0);
        call1("requestPrompt", QStringLiteral("forget"));
        QVERIFY(m_prompts->property("busy").toBool());
        m_host->setProperty("moduleId", QString());
        QVERIFY(!loader()->loaded());
        QTRY_VERIFY(page.isNull());
        QVERIFY(!m_prompts->property("busy").toBool());
        QVERIFY(!m_mock->property("bluetoothDiscovering").toBool());
        QCOMPARE(m_mock->property("actions").toList().last().toString(), QStringLiteral("stop"));
        const int refreshes = m_mock->property("refreshCount").toInt();
        QTest::qWait(2300);
        QCOMPARE(m_mock->property("refreshCount").toInt(), refreshes);
        respond(QStringLiteral("accepted"), QString());
        QVERIFY(!m_mock->property("bluetoothAction").toString().contains(QStringLiteral("forget")));

        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(!pageProperty("infoOpen").toBool());
        QVERIFY(m_mock->property("bluetoothDiscovering").toBool());
    }

private:
    QObject *createMock(const QString &file)
    {
        QQmlComponent component(m_engine.data(), QUrl::fromLocalFile(QStringLiteral(MOCK_DIR "/") + file));
        if (!component.isReady()) {
            qWarning("%s", qPrintable(component.errorString()));
            return nullptr;
        }
        return component.create();
    }

    bool loadPage()
    {
        m_host->setProperty("moduleId", BluetoothId);
        return loader()->loaded();
    }

    SettingsModuleLoader *loader() const
    {
        return m_host->property("loader").value<SettingsModuleLoader *>();
    }

    static QQuickItem *search(QQuickItem *parent, const QString &name)
    {
        if (!parent)
            return nullptr;
        if (parent->objectName() == name)
            return parent;
        for (QQuickItem *child : parent->childItems()) {
            if (QQuickItem *found = search(child, name))
                return found;
        }
        return nullptr;
    }

    QQuickItem *find(const QString &name) const { return search(m_host.data(), name); }
    static QString text(QQuickItem *item) { return item ? item->property("text").toString() : QString(); }
    QVariant pageProperty(const char *name) { return loader()->item()->property(name); }

    QVariant call0(const char *method)
    {
        QVariant result;
        QMetaObject::invokeMethod(loader()->item(), method, Q_RETURN_ARG(QVariant, result));
        return result;
    }

    QVariant call1(const char *method, const QVariant &first)
    {
        QVariant result;
        QMetaObject::invokeMethod(loader()->item(), method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, first));
        return result;
    }

    void openInfo(int index)
    {
        call1("openInfo", m_mock->property("bluetoothDevices").toList().at(index));
    }

    void respond(const QString &result, const QString &value)
    {
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, result), Q_ARG(QVariant, value));
    }

    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QQuickItem> m_host;
    QScopedPointer<QQuickWindow> m_window;
    QObject *m_mock = nullptr;
    QObject *m_prompts = nullptr;
    QObject *m_environment = nullptr;
};

QTEST_MAIN(BluetoothModuleTest)
#include "tst_bluetoothmodule.moc"
