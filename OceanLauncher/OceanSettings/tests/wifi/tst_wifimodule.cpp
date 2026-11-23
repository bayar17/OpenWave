#include "SettingsModuleLoader.h"
#include "SettingsModuleRegistry.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

const QString WifiId = QStringLiteral("com.openwave.settings.wifi");
QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_messages << message;
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

QVariantMap network(const QString &ssid, const QString &security, int strength,
                    bool connected = false, bool saved = false, bool connecting = false)
{
    return {
        {QStringLiteral("ssid"), ssid},
        {QStringLiteral("security"), security},
        {QStringLiteral("strength"), strength},
        {QStringLiteral("connected"), connected},
        {QStringLiteral("connecting"), connecting},
        {QStringLiteral("saved"), saved}
    };
}

}

class WifiModuleTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(captureMessages);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        g_messages.clear();
        m_engine.reset(new QQmlEngine);
        m_engine->addImportPath(QStringLiteral(STUB_IMPORT_DIR));
        m_mock = createMock(QStringLiteral("MockConnectivity.qml"));
        m_prompts = createMock(QStringLiteral("MockPrompts.qml"));
        QVERIFY(m_mock && m_prompts);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Connectivity"), m_mock);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), m_prompts);
        SettingsModuleRegistry::shared()->setModuleRoot(QStringLiteral(WIFI_MODULES_DIR));

        QQmlComponent component(m_engine.data());
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            SettingsModuleHost { width: 672; height: 900 }
        )", QUrl(QStringLiteral("qrc:/wifihost.qml")));
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
        m_mock = nullptr;
        m_prompts = nullptr;
        m_engine.reset();
    }

    void discoversWifiModuleMetadata()
    {
        SettingsModuleRegistry *registry = SettingsModuleRegistry::shared();
        QVERIFY(registry->count() >= 1);
        const QVariantMap info = registry->module(WifiId);
        QVERIFY(!info.isEmpty());
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("Wi-Fi"));
        QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral("Connectivity"));
        QCOMPARE(info.value(QStringLiteral("apiVersion")).toInt(), SettingsModuleRegistry::SupportedApiVersion);
        QCOMPARE(info.value(QStringLiteral("description")).toString(), QStringLiteral("Wireless network settings"));
        const QUrl entry = registry->entryUrl(WifiId);
        QVERIFY(entry.path().endsWith(QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(QFileInfo::exists(entry.toLocalFile()));
        bool listed = false;
        for (const QVariant &module : registry->modules())
            listed = listed || module.toMap().value(QStringLiteral("id")).toString() == WifiId;
        QVERIFY(listed);
    }

    void loadsThroughRealLoader()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QCOMPARE(loader()->activeModuleId(), WifiId);
        QCOMPARE(loader()->status(), SettingsModuleLoader::Ready);
        QCOMPARE(loader()->item()->objectName(), QStringLiteral("wifiModule"));
        QVERIFY(find(QStringLiteral("wifiSwitch")));
        QVERIFY(find(QStringLiteral("wifiRefresh")));
        QVERIFY(find(QStringLiteral("wifiStatusText")));
    }

    void presentsEnabledAndDisabledState()
    {
        m_mock->setProperty("wifiStatus", QStringLiteral("2 networks available"));
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 70),
                     network(QStringLiteral("Cafe"), QStringLiteral("Open"), 50)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *toggle = find(QStringLiteral("wifiSwitch"));
        QVERIFY(toggle);
        QVERIFY(toggle->property("checked").toBool());
        QVERIFY(toggle->isEnabled());
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());
        QVERIFY(find(QStringLiteral("wifiNetwork_Home")));
        QCOMPARE(find(QStringLiteral("wifiSwitchTrack"))->property("color").value<QColor>(), QColor("#00d9e7"));

        m_mock->setProperty("wifiEnabled", false);
        m_mock->setProperty("wifiStatus", QStringLiteral("Wi-Fi is off."));
        QVERIFY(!toggle->property("checked").toBool());
        QCOMPARE(find(QStringLiteral("wifiSwitchTrack"))->property("color").value<QColor>(), QColor("#777e83"));
        QVERIFY(find(QStringLiteral("wifiStatusText"))->isVisible());
        QCOMPARE(find(QStringLiteral("wifiStatusText"))->property("text").toString(),
                 QStringLiteral("Wi-Fi is off. Tap Wi-Fi or the scan icon to turn it on."));
        QTRY_VERIFY(!find(QStringLiteral("wifiNetwork_Home")));
        QVERIFY(!find(QStringLiteral("wifiAddNetwork"))->isEnabled());

        m_mock->setProperty("wifiDeviceAvailable", false);
        m_mock->setProperty("wifiStatus", QStringLiteral("No Wi-Fi adapter found."));
        QVERIFY(!toggle->isEnabled());
        QCOMPARE(find(QStringLiteral("wifiStatusText"))->property("text").toString(), QStringLiteral("No Wi-Fi adapter found."));

        m_mock->setProperty("serviceAvailable", false);
        m_mock->setProperty("wifiStatus", QStringLiteral("Connectivity service unavailable."));
        QVERIFY(!toggle->isEnabled());
        QCOMPARE(find(QStringLiteral("wifiStatusText"))->property("text").toString(), QStringLiteral("Connectivity service unavailable."));
    }

    void switchSendsEnableRequestAndFollowsBackend()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *toggle = find(QStringLiteral("wifiSwitch"));
        QVERIFY(toggle->property("checked").toBool());
        QMetaObject::invokeMethod(toggle, "simulateUserToggle");
        QCOMPARE(m_mock->property("enableCalls").toList().size(), 1);
        QCOMPARE(m_mock->property("enableCalls").toList().first().toBool(), false);
        QVERIFY(toggle->property("checked").toBool());
        m_mock->setProperty("wifiEnabled", false);
        QVERIFY(!toggle->property("checked").toBool());
        QMetaObject::invokeMethod(toggle, "simulateUserToggle");
        QCOMPARE(m_mock->property("enableCalls").toList().last().toBool(), true);
        QVERIFY(!toggle->property("checked").toBool());
    }

    void rendersSortedNetworkList()
    {
        setNetworks({network(QStringLiteral("Weak"), QStringLiteral("WPA2"), 20),
                     network(QStringLiteral("Strong"), QStringLiteral("Open"), 90),
                     network(QStringLiteral("Middle"), QStringLiteral("WPA3"), 60, false, true)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *strong = find(QStringLiteral("wifiNetwork_Strong"));
        QQuickItem *middle = find(QStringLiteral("wifiNetwork_Middle"));
        QQuickItem *weak = find(QStringLiteral("wifiNetwork_Weak"));
        QVERIFY(strong && middle && weak);
        QTRY_VERIFY(strong->parentItem()->y() < middle->parentItem()->y());
        QVERIFY(middle->parentItem()->y() < weak->parentItem()->y());
        QCOMPARE(strong->height(), 66.0);
        QCOMPARE(text(strong, QStringLiteral("wifiRowName")), QStringLiteral("Strong"));
        QVERIFY(!search(strong, QStringLiteral("wifiRowDetail"))->isVisible());
        QCOMPARE(search(strong, QStringLiteral("wifiRowLock"))->property("kind").toString(), QStringLiteral("unlock"));
        QCOMPARE(search(weak, QStringLiteral("wifiRowLock"))->property("kind").toString(), QStringLiteral("lock"));
        QCOMPARE(search(strong, QStringLiteral("wifiRowSignal"))->property("kind").toString(), QStringLiteral("wifi"));
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());
    }

    void presentsConnectedNetworkFirstWithDivider()
    {
        m_mock->setProperty("wifiState", QStringLiteral("Connected"));
        m_mock->setProperty("activeSsid", QStringLiteral("Home"));
        setNetworks({network(QStringLiteral("Other"), QStringLiteral("WPA2"), 95),
                     network(QStringLiteral("Home"), QStringLiteral("WPA2"), 55, true, true),
                     network(QStringLiteral("Cafe"), QStringLiteral("Open"), 40)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *home = find(QStringLiteral("wifiNetwork_Home"));
        QQuickItem *other = find(QStringLiteral("wifiNetwork_Other"));
        QVERIFY(home && other);
        QTRY_VERIFY(home->parentItem()->y() < other->parentItem()->y());
        QCOMPARE(home->height(), 94.0);
        QCOMPARE(other->height(), 66.0);
        QCOMPARE(home->parentItem()->height(), 132.0);
        QQuickItem *divider = find(QStringLiteral("wifiDivider_Home"));
        QVERIFY(divider && divider->isVisible());
        QCOMPARE(divider->y(), home->height());
        QVERIFY(!find(QStringLiteral("wifiDivider_Other"))->isVisible());
        QCOMPARE(text(home, QStringLiteral("wifiRowDetail")), QStringLiteral("Connected"));
        QVERIFY(search(home, QStringLiteral("wifiRowDetail"))->isVisible());
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());

        m_mock->setProperty("wifiState", QStringLiteral("Disconnected"));
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 55, false, true)});
        QTRY_VERIFY(!find(QStringLiteral("wifiNetwork_Other")));
        QCOMPARE(find(QStringLiteral("wifiNetwork_Home"))->height(), 66.0);
        QVERIFY(!find(QStringLiteral("wifiDivider_Home"))->isVisible());
    }

    void showsConnectingNetworkWithStatusLine()
    {
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 55, false, true, true)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *row = find(QStringLiteral("wifiNetwork_Home"));
        QVERIFY(row);
        QCOMPARE(row->height(), 94.0);
        QVERIFY(!row->isEnabled());
        QCOMPARE(text(row, QStringLiteral("wifiRowDetail")), QString::fromUtf8("Connecting…"));
    }

    void handlesHiddenAndEmptySsids()
    {
        setNetworks({network(QString(), QStringLiteral("WPA2"), 80),
                     network(QStringLiteral("   "), QStringLiteral("Open"), 70)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *first = find(QStringLiteral("wifiNetwork_hidden0"));
        QQuickItem *second = find(QStringLiteral("wifiNetwork_hidden1"));
        QVERIFY(first && second);
        QCOMPARE(text(first, QStringLiteral("wifiRowName")), QStringLiteral("Hidden network"));
        QCOMPARE(text(second, QStringLiteral("wifiRowName")), QStringLiteral("Hidden network"));
        QMetaObject::invokeMethod(first, "clicked");
        QVERIFY(find(QStringLiteral("wifiErrorBanner"))->isVisible());
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
        QCOMPARE(m_prompts->property("passwordRequests").toList().size(), 0);
        QMetaObject::invokeMethod(second, "clicked");
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
    }

    void showsEmptyListState()
    {
        m_mock->setProperty("wifiStatus", QStringLiteral("No networks found. Tap refresh to scan."));
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QVERIFY(find(QStringLiteral("wifiStatusText"))->isVisible());
        QCOMPARE(find(QStringLiteral("wifiStatusText"))->property("text").toString(), QStringLiteral("No networks found. Tap refresh to scan."));
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 70)});
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());
    }

    void presentsAndDrivesScanning()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QVERIFY(m_mock->property("scanCount").toInt() >= 1);
        QVERIFY(m_mock->property("refreshCount").toInt() >= 1);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("scan"));

        const int scans = m_mock->property("scanCount").toInt();
        call1("stopScan", false);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("refresh"));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiRefresh")), "clicked");
        QCOMPARE(m_mock->property("scanCount").toInt(), scans + 1);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("scan"));

        call1("stopScan", false);
        m_mock->setProperty("wifiScanAvailable", false);
        const int refreshes = m_mock->property("refreshCount").toInt();
        QMetaObject::invokeMethod(find(QStringLiteral("wifiRefresh")), "clicked");
        QCOMPARE(m_mock->property("scanCount").toInt(), scans + 1);
        QVERIFY(m_mock->property("refreshCount").toInt() > refreshes);
    }

    void doesNotScanWhileOffAndScansWhenEnabled()
    {
        m_mock->setProperty("wifiEnabled", false);
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QCOMPARE(m_mock->property("scanCount").toInt(), 0);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("refresh"));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiRefresh")), "clicked");
        QCOMPARE(m_mock->property("enableCalls").toList().size(), 1);
        QCOMPARE(m_mock->property("enableCalls").toList().first().toBool(), true);
        m_mock->setProperty("wifiEnabled", true);
        QVERIFY(m_mock->property("scanCount").toInt() >= 1);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("scan"));
        m_mock->setProperty("wifiEnabled", false);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("refresh"));
    }

    void connectsToSavedAndOpenNetworks()
    {
        setNetworks({network(QStringLiteral("Saved"), QStringLiteral("WPA2"), 80, false, true),
                     network(QStringLiteral("Open"), QStringLiteral("Open"), 70)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Saved")), "clicked");
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Open")), "clicked");
        const QVariantList calls = m_mock->property("connectCalls").toList();
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.at(0).toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Saved"));
        QCOMPARE(calls.at(0).toMap().value(QStringLiteral("password")).toString(), QString());
        QCOMPARE(calls.at(1).toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Open"));
        QCOMPARE(m_prompts->property("passwordRequests").toList().size(), 0);

        m_mock->setProperty("busy", true);
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Saved")), "clicked");
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 2);
    }

    void disconnectsByTappingTheConnectedNetwork()
    {
        m_mock->setProperty("wifiState", QStringLiteral("Connected"));
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 55, true, true)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *row = find(QStringLiteral("wifiNetwork_Home"));
        QQuickItem *divider = find(QStringLiteral("wifiDivider_Home"));
        QMetaObject::invokeMethod(divider, "clicked");
        QCOMPARE(m_mock->property("disconnectCount").toInt(), 0);
        QMetaObject::invokeMethod(row, "clicked");
        QCOMPARE(m_mock->property("disconnectCount").toInt(), 1);
        row->setProperty("longPressTriggered", true);
        QMetaObject::invokeMethod(row, "clicked");
        QCOMPARE(m_mock->property("disconnectCount").toInt(), 1);
        m_mock->setProperty("busy", true);
        QVERIFY(!row->isEnabled());
    }

    void submitsPasswordThroughPromptService()
    {
        const QString secret = QStringLiteral("correct-horse-9");
        setNetworks({network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Locked")), "clicked");
        const QVariantList requests = m_prompts->property("passwordRequests").toList();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Locked"));
        QCOMPARE(requests.first().toMap().value(QStringLiteral("options")).toMap().value(QStringLiteral("minLength")).toInt(), 8);
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
        QCOMPARE(loader()->item()->property("pendingSsid").toString(), QStringLiteral("Locked"));

        QMetaObject::invokeMethod(m_prompts, "complete", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, secret));
        const QVariantList calls = m_mock->property("connectCalls").toList();
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Locked"));
        QCOMPARE(calls.first().toMap().value(QStringLiteral("password")).toString(), secret);
        QVERIFY(loader()->item()->property("pendingSsid").toString().isEmpty());

        const QMetaObject *meta = loader()->item()->metaObject();
        for (int index = 0; index < meta->propertyCount(); ++index) {
            const QVariant value = meta->property(index).read(loader()->item());
            if (value.typeId() == QMetaType::QString)
                QVERIFY2(!value.toString().contains(secret), meta->property(index).name());
        }
        for (const QString &message : std::as_const(g_messages))
            QVERIFY(!message.contains(secret));
    }

    void ignoresCancelledAndUnrelatedPrompts()
    {
        setNetworks({network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(m_prompts, "complete", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, QStringLiteral("unrelated-123")));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);

        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Locked")), "clicked");
        QMetaObject::invokeMethod(m_prompts, "complete", Q_ARG(QVariant, QStringLiteral("cancelled")), Q_ARG(QVariant, QString()));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
        QVERIFY(loader()->item()->property("pendingSsid").toString().isEmpty());

        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Locked")), "clicked");
        m_mock->setProperty("busy", true);
        QMetaObject::invokeMethod(m_prompts, "complete", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, QStringLiteral("whatever-123")));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
        QVERIFY(find(QStringLiteral("wifiErrorBanner"))->isVisible());
    }

    void rejectsWepAndMissingPromptService()
    {
        setNetworks({network(QStringLiteral("Legacy"), QStringLiteral("WEP"), 80),
                     network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 70)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Legacy")), "clicked");
        QVERIFY(find(QStringLiteral("wifiErrorBanner"))->isVisible());
        QVERIFY(text(find(QStringLiteral("wifiErrorBanner")), QStringLiteral("wifiErrorText")).contains(QStringLiteral("WEP")));
        QCOMPARE(m_prompts->property("passwordRequests").toList().size(), 0);

        m_host->setProperty("moduleId", QString());
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), static_cast<QObject *>(nullptr));
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Locked")), "clicked");
        QVERIFY(text(find(QStringLiteral("wifiErrorBanner")), QStringLiteral("wifiErrorText")).contains(QStringLiteral("prompt")));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
    }

    void surfacesBackendErrors()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *banner = find(QStringLiteral("wifiErrorBanner"));
        QVERIFY(!banner->isVisible());
        QMetaObject::invokeMethod(m_mock, "errorOccurred", Q_ARG(QString, QStringLiteral("Wrong password for Home.")));
        QVERIFY(banner->isVisible());
        QCOMPARE(text(banner, QStringLiteral("wifiErrorText")), QStringLiteral("Wrong password for Home."));
        setNetworks({network(QStringLiteral("Saved"), QStringLiteral("WPA2"), 80, false, true)});
        QTRY_VERIFY(find(QStringLiteral("wifiNetwork_Saved")));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Saved")), "clicked");
        QVERIFY(!banner->isVisible());
        QVERIFY(loader()->loaded());
    }

    void toleratesMissingBackendWithoutCrashing()
    {
        m_engine->rootContext()->setContextProperty(QStringLiteral("Connectivity"), static_cast<QObject *>(nullptr));
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QVERIFY(!find(QStringLiteral("wifiSwitch"))->isEnabled());
        QCOMPARE(find(QStringLiteral("wifiStatusText"))->property("text").toString(), QStringLiteral("Connectivity service unavailable."));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiRefresh")), "clicked");
        QVERIFY(loader()->loaded());
    }

    void unloadsCleanlyWhenNavigatingAway()
    {
        setNetworks({network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QPointer<QQuickItem> page = loader()->item();
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Locked")), "clicked");
        QVERIFY(m_prompts->property("busy").toBool());

        m_host->setProperty("moduleId", QString());
        QVERIFY(!loader()->loaded());
        QVERIFY(!loader()->item());
        QTRY_VERIFY(page.isNull());
        QCOMPARE(m_prompts->property("cancelCount").toInt(), 1);
        QVERIFY(!m_prompts->property("busy").toBool());

        const int refreshes = m_mock->property("refreshCount").toInt();
        const int scans = m_mock->property("scanCount").toInt();
        QTest::qWait(1900);
        QCOMPARE(m_mock->property("refreshCount").toInt(), refreshes);
        QCOMPARE(m_mock->property("scanCount").toInt(), scans);

        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QVERIFY(loader()->item()->property("pendingSsid").toString().isEmpty());
        QVERIFY(find(QStringLiteral("wifiNetwork_Locked")));
    }

    void pollsBackendWhileLoaded()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        const int before = m_mock->property("refreshCount").toInt();
        QTRY_VERIFY_WITH_TIMEOUT(m_mock->property("refreshCount").toInt() > before, 3000);
    }

    void connectedRowIgnoresClicksWhileBusy()
    {
        m_mock->setProperty("wifiState", QStringLiteral("Connected"));
        setNetworks({network(QStringLiteral("Home"), QStringLiteral("WPA2"), 55, true, true)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        m_mock->setProperty("busy", true);
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Home")), "clicked");
        QCOMPARE(m_mock->property("disconnectCount").toInt(), 0);
    }

    void cancelsPasswordPromptOnBackAndIgnoresStaleResults()
    {
        setNetworks({network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        call1("requestPassword", network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80));
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("password"));
        QCOMPARE(promptOptions().value(QStringLiteral("acceptLabel")).toString(), QStringLiteral("Connect"));
        QVERIFY(call0("back").toBool());
        QVERIFY(!m_prompts->property("busy").toBool());
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
        QVERIFY(!call0("back").toBool());

        call1("requestPassword", network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80));
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, QStringLiteral("renderer_lost")), Q_ARG(QVariant, QStringLiteral("ignored-password")));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);

        call1("requestPassword", network(QStringLiteral("Locked"), QStringLiteral("WPA2"), 80));
        m_host->setProperty("moduleId", QString());
        QVERIFY(!m_prompts->property("busy").toBool());
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, QStringLiteral("ignored-password")));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);
    }

    void opensSavedNetworkDetailsOnHold()
    {
        setNetworks({network(QStringLiteral("Saved Network"), QStringLiteral("WPA"), 65, false, true),
                     network(QStringLiteral("Fresh"), QStringLiteral("WPA"), 60)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiNetwork_Fresh")), "longPressed");
        QVERIFY(!pageProperty("infoOpen").toBool());

        QQuickItem *saved = find(QStringLiteral("wifiNetwork_Saved Network"));
        QMetaObject::invokeMethod(saved, "longPressed");
        QVERIFY(pageProperty("infoOpen").toBool());
        QCOMPARE(m_mock->property("requestedInfoSsid").toString(), QStringLiteral("Saved Network"));
        QVERIFY(find(QStringLiteral("wifiInfoPane"))->isVisible());
        QVERIFY(!find(QStringLiteral("wifiSwitch"))->isVisible());
        QVERIFY(!find(QStringLiteral("wifiAddNetwork"))->isVisible());
        QCOMPARE(text(find(QStringLiteral("wifiInfoSsid")), QString()).isEmpty(), true);
        QCOMPARE(find(QStringLiteral("wifiInfoSsid"))->property("text").toString(), QStringLiteral("Saved Network"));
        QQuickItem *password = find(QStringLiteral("wifiInfoPassword"));
        QCOMPARE(password->property("detail").toString(), QString::fromUtf8("••••••••"));
        QMetaObject::invokeMethod(password, "clicked");
        QCOMPARE(password->property("detail").toString(), QStringLiteral("saved-secret"));
        QMetaObject::invokeMethod(password, "clicked");
        QCOMPARE(password->property("detail").toString(), QString::fromUtf8("••••••••"));
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);

        saved->setProperty("longPressTriggered", true);
        call0("closeSavedInfo");
        QMetaObject::invokeMethod(saved, "clicked");
        QCOMPARE(m_mock->property("connectCalls").toList().size(), 0);

        QMetaObject::invokeMethod(saved, "longPressed");
        QMetaObject::invokeMethod(find(QStringLiteral("wifiInfoBack")), "clicked");
        QVERIFY(!pageProperty("infoOpen").toBool());
        QVERIFY(find(QStringLiteral("wifiSwitch"))->isVisible());
        QCOMPARE(m_mock->property("requestedInfoSsid").toString(), QString());
        QMetaObject::invokeMethod(saved, "longPressed");
        QVERIFY(call0("back").toBool());
        QVERIFY(!pageProperty("infoOpen").toBool());
        QVERIFY(!call0("back").toBool());
    }

    void showsLoadedSavedNetworkDetails()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        openSaved(QStringLiteral("Saved Network"));
        m_mock->setProperty("savedWifiInfo", QVariantMap{
            {QStringLiteral("ssid"), QStringLiteral("Saved Network")}, {QStringLiteral("security"), QStringLiteral("WPA Personal")},
            {QStringLiteral("connected"), true}, {QStringLiteral("strength"), 70}, {QStringLiteral("frequency"), 5180},
            {QStringLiteral("ipv4Address"), QStringLiteral("192.168.1.9")}, {QStringLiteral("dns"), QStringLiteral("1.1.1.1")},
            {QStringLiteral("ipManual"), true}});
        QCOMPARE(find(QStringLiteral("wifiInfoStatus"))->property("detail").toString(), QStringLiteral("Connected"));
        QCOMPARE(find(QStringLiteral("wifiInfoSecurity"))->property("detail").toString(), QStringLiteral("WPA Personal"));
        QVERIFY(find(QStringLiteral("wifiIpConfig"))->property("detail").toString().contains(QStringLiteral("Manual IPv4")));
        QCOMPARE(find(QStringLiteral("wifiInfoPassword"))->property("detail").toString(), QStringLiteral("Unavailable"));
        QVERIFY(!find(QStringLiteral("wifiInfoPasswordAction"))->isVisible());
        m_mock->setProperty("savedWifiInfoLoading", true);
        QVERIFY(find(QStringLiteral("wifiInfoLoading"))->isVisible());
        QVERIFY(!find(QStringLiteral("wifiForget"))->isEnabled());
        QVERIFY(!find(QStringLiteral("wifiIpConfig"))->isEnabled());
    }

    void forgetRequiresConfirmation()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        openSaved(QStringLiteral("Remembered"));
        QQuickItem *forget = find(QStringLiteral("wifiForget"));
        QVERIFY(forget->isVisible() && forget->isEnabled());
        call0("requestForget");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("confirmation"));
        QCOMPARE(promptOptions().value(QStringLiteral("acceptLabel")).toString(), QStringLiteral("Forget"));
        QCOMPARE(promptOptions().value(QStringLiteral("cancelLabel")).toString(), QStringLiteral("Cancel"));
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, QStringLiteral("cancelled")), Q_ARG(QVariant, QString()));
        QCOMPARE(m_mock->property("forgottenSsid").toString(), QString());
        QVERIFY(pageProperty("infoOpen").toBool());

        call0("requestForget");
        call0("closeSavedInfo");
        QVERIFY(!m_prompts->property("busy").toBool());
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, QString()));
        QCOMPARE(m_mock->property("forgottenSsid").toString(), QString());

        openSaved(QStringLiteral("Remembered"));
        QMetaObject::invokeMethod(find(QStringLiteral("wifiForget")), "clicked");
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, QString()));
        QCOMPARE(m_mock->property("forgottenSsid").toString(), QStringLiteral("Remembered"));
        QVERIFY(!pageProperty("infoOpen").toBool());
    }

    void addsHiddenNetworkThroughForm()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *add = find(QStringLiteral("wifiAddNetwork"));
        QVERIFY(add->isVisible() && add->isEnabled());
        QMetaObject::invokeMethod(add, "clicked");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("form"));
        QCOMPARE(m_prompts->property("title").toString(), QStringLiteral("Add Network"));
        QVERIFY(fieldOptions(QStringLiteral("password")).value(QStringLiteral("secret")).toBool());
        QCOMPARE(fieldOptions(QStringLiteral("security")).value(QStringLiteral("choices")).toMap().value(QStringLiteral("open")).toString(), QStringLiteral("Open"));
        QCOMPARE(fieldOptions(QStringLiteral("security")).value(QStringLiteral("choices")).toMap().value(QStringLiteral("wpa")).toString(), QStringLiteral("WPA Personal"));
        QCOMPARE(fieldOptions(QStringLiteral("ssid")).value(QStringLiteral("maxBytes")).toInt(), 32);
        QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, QStringLiteral("cancelled")), Q_ARG(QVariant, QVariantMap{}));
        QVERIFY(m_mock->property("addedNetwork").toMap().isEmpty());
        QVERIFY(pageProperty("pendingAction").toString().isEmpty());

        QMetaObject::invokeMethod(add, "clicked");
        answerForm(QStringLiteral("accepted"), QVariantMap{{QStringLiteral("ssid"), QStringLiteral("New Network")}, {QStringLiteral("security"), QStringLiteral("wpa")}, {QStringLiteral("password"), QStringLiteral("test-password")}});
        QCOMPARE(m_mock->property("addedNetwork").toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("New Network"));
        QVERIFY(m_mock->property("addedNetwork").toMap().value(QStringLiteral("secured")).toBool());
        QCOMPARE(m_mock->property("addedNetwork").toMap().value(QStringLiteral("password")).toString(), QStringLiteral("test-password"));
        QVERIFY(find(QStringLiteral("wifiNoticeBanner"))->isVisible());

        QMetaObject::invokeMethod(add, "clicked");
        answerForm(QStringLiteral("accepted"), QVariantMap{{QStringLiteral("ssid"), QStringLiteral("Open Hidden")}, {QStringLiteral("security"), QStringLiteral("open")}, {QStringLiteral("password"), QString()}});
        QCOMPARE(m_mock->property("addedNetwork").toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Open Hidden"));
        QVERIFY(!m_mock->property("addedNetwork").toMap().value(QStringLiteral("secured")).toBool());
        QCOMPARE(m_mock->property("addedNetwork").toMap().value(QStringLiteral("password")).toString(), QString());

        QMetaObject::invokeMethod(add, "clicked");
        answerForm(QStringLiteral("accepted"), QVariantMap{{QStringLiteral("ssid"), QStringLiteral("   ")}, {QStringLiteral("security"), QStringLiteral("open")}});
        QVERIFY(find(QStringLiteral("wifiErrorBanner"))->isVisible());
        QCOMPARE(m_mock->property("addedNetwork").toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Open Hidden"));

        m_mock->setProperty("busy", true);
        QVERIFY(!add->isEnabled());
        m_mock->setProperty("busy", false);
        m_mock->setProperty("wifiEnabled", false);
        QVERIFY(!add->isEnabled());
    }

    void validatesAndSavesManualIpAndDns()
    {
        openIpSettings();
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("form"));
        QCOMPARE(m_prompts->property("title").toString(), QStringLiteral("IP and DNS settings"));
        QCOMPARE(fieldOptions(QStringLiteral("ipMode")).value(QStringLiteral("initialText")).toString(), QStringLiteral("automatic"));
        QCOMPARE(fieldOptions(QStringLiteral("dnsMode")).value(QStringLiteral("initialText")).toString(), QStringLiteral("automatic"));
        QCOMPARE(fieldOptions(QStringLiteral("prefix")).value(QStringLiteral("initialText")).toString(), QStringLiteral("24"));
        QCOMPARE(fieldOptions(QStringLiteral("address")).value(QStringLiteral("activeWhenField")).toString(), QStringLiteral("ipMode"));
        QCOMPARE(fieldOptions(QStringLiteral("dnsServers")).value(QStringLiteral("activeWhenField")).toString(), QStringLiteral("dnsMode"));

        QVariantMap values{{QStringLiteral("ipMode"), QStringLiteral("manual")}, {QStringLiteral("address"), QStringLiteral("invalid")},
                           {QStringLiteral("prefix"), QStringLiteral("24")}, {QStringLiteral("gateway"), QStringLiteral("192.168.1.1")},
                           {QStringLiteral("dnsMode"), QStringLiteral("manual")},
                           {QStringLiteral("dnsServers"), QStringLiteral("1.1.1.1, 2001:4860:4860::8888")}};
        auto respond = [&](const QVariantMap &v) {
            QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, v));
        };
        respond(values);
        QVERIFY(savedConfig().isEmpty());
        QVERIFY(m_prompts->property("busy").toBool());
        QCOMPARE(fieldOptions(QStringLiteral("address")).value(QStringLiteral("initialText")).toString(), QStringLiteral("invalid"));

        values[QStringLiteral("address")] = QStringLiteral("192.168.1.50");
        values[QStringLiteral("dnsServers")] = QStringLiteral("invalid-dns");
        respond(values);
        QVERIFY(savedConfig().isEmpty());
        QVERIFY(m_prompts->property("busy").toBool());
        QCOMPARE(fieldOptions(QStringLiteral("address")).value(QStringLiteral("initialText")).toString(), QStringLiteral("192.168.1.50"));

        values[QStringLiteral("dnsServers")] = QStringLiteral("1.1.1.1, 2001:4860:4860::8888");
        respond(values);
        QVERIFY(!m_prompts->property("busy").toBool());
        QCOMPARE(savedConfig().value(QStringLiteral("ssid")).toString(), QStringLiteral("Saved Network"));
        QCOMPARE(savedConfig().value(QStringLiteral("address")).toString(), QStringLiteral("192.168.1.50"));
        QCOMPARE(savedConfig().value(QStringLiteral("prefix")).toInt(), 24);
        QCOMPARE(savedConfig().value(QStringLiteral("gateway")).toString(), QStringLiteral("192.168.1.1"));
        QVERIFY(savedConfig().value(QStringLiteral("manualIp")).toBool());
        QVERIFY(savedConfig().value(QStringLiteral("manualDns")).toBool());
        QCOMPARE(savedConfig().value(QStringLiteral("dns")).toList().size(), 2);
        QCOMPARE(savedConfig().value(QStringLiteral("dns")).toList().at(1).toString(), QStringLiteral("2001:4860:4860::8888"));
        QVERIFY(find(QStringLiteral("wifiNoticeBanner"))->isVisible());
        QCOMPARE(find(QStringLiteral("wifiNotice"))->property("text").toString(), QStringLiteral("Network settings applied."));

        call2("openIpConfig", QVariant(), QVariant());
        QCOMPARE(fieldOptions(QStringLiteral("address")).value(QStringLiteral("initialText")).toString(), QStringLiteral("192.168.1.50"));
        QCOMPARE(fieldOptions(QStringLiteral("dnsServers")).value(QStringLiteral("initialText")).toString(), values.value(QStringLiteral("dnsServers")).toString());
        values[QStringLiteral("ipMode")] = QStringLiteral("automatic");
        respond(values);
        QVERIFY(!savedConfig().value(QStringLiteral("manualIp")).toBool());
        QCOMPARE(savedConfig().value(QStringLiteral("address")).toString(), QString());
        QCOMPARE(savedConfig().value(QStringLiteral("prefix")).toInt(), 0);
        QCOMPARE(savedConfig().value(QStringLiteral("gateway")).toString(), QString());
        QVERIFY(savedConfig().value(QStringLiteral("manualDns")).toBool());

        call2("openIpConfig", QVariant(), QVariant());
        values[QStringLiteral("dnsMode")] = QStringLiteral("automatic");
        respond(values);
        QVERIFY(!savedConfig().value(QStringLiteral("manualDns")).toBool());
        QCOMPARE(savedConfig().value(QStringLiteral("dns")).toList().size(), 0);
    }

    void validatesIpAddressesAndDnsEntries()
    {
        openIpSettings();
        auto attempt = [&](const QString &prefix, const QString &gateway, const QString &dns) {
            QVariantMap values{{QStringLiteral("ipMode"), QStringLiteral("manual")}, {QStringLiteral("address"), QStringLiteral("192.168.1.50")},
                               {QStringLiteral("prefix"), prefix}, {QStringLiteral("gateway"), gateway},
                               {QStringLiteral("dnsMode"), QStringLiteral("manual")}, {QStringLiteral("dnsServers"), dns}};
            QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, QStringLiteral("accepted")), Q_ARG(QVariant, values));
            const bool saved = !savedConfig().isEmpty();
            m_mock->setProperty("savedIpConfig", QVariantMap{});
            if (m_prompts->property("busy").toBool())
                QMetaObject::invokeMethod(m_prompts, "cancel");
            call2("openIpConfig", QVariant(), QVariant());
            return saved;
        };
        for (const QString &prefix : {QStringLiteral("33"), QStringLiteral("-1"), QStringLiteral(""), QStringLiteral("1e1")})
            QVERIFY2(!attempt(prefix, QStringLiteral("192.168.1.1"), QStringLiteral("1.1.1.1")), qPrintable(prefix));
        QVERIFY(!attempt(QStringLiteral("24"), QStringLiteral("999.1.1.1"), QStringLiteral("1.1.1.1")));
        QVERIFY(!attempt(QStringLiteral("24"), QString(), QStringLiteral(" , ")));
        for (const QString &dns : {QStringLiteral("2001:::1"), QStringLiteral("999.1.1.1"), QStringLiteral("001.1.1.1"), QStringLiteral("1.1.1.1, nonsense")})
            QVERIFY2(!attempt(QStringLiteral("24"), QString(), dns), qPrintable(dns));
        QVERIFY(attempt(QStringLiteral("24"), QString(), QStringLiteral("::1")));
        QVERIFY(attempt(QStringLiteral("0"), QStringLiteral("192.168.1.1"), QStringLiteral("2001:4860:4860::8888, 1.1.1.1")));
    }

    void ipPromptHandlesCancellationAndStaleResults()
    {
        QVariantMap values{{QStringLiteral("ipMode"), QStringLiteral("automatic")}, {QStringLiteral("address"), QString()},
                           {QStringLiteral("prefix"), QString()}, {QStringLiteral("gateway"), QString()},
                           {QStringLiteral("dnsMode"), QStringLiteral("manual")}, {QStringLiteral("dnsServers"), QStringLiteral("1.1.1.1")}};
        auto respond = [&](const QString &result) {
            QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, result), Q_ARG(QVariant, values));
        };
        openIpSettings();
        respond(QStringLiteral("cancelled"));
        QVERIFY(pageProperty("pendingAction").toString().isEmpty());
        QVERIFY(savedConfig().isEmpty());

        call2("openIpConfig", QVariant(), QVariant());
        QVERIFY(call0("back").toBool());
        QVERIFY(!m_prompts->property("busy").toBool());
        QVERIFY(pageProperty("infoOpen").toBool());
        respond(QStringLiteral("accepted"));
        QVERIFY(savedConfig().isEmpty());

        call2("openIpConfig", QVariant(), QVariant());
        openSaved(QStringLiteral("Other Network"));
        respond(QStringLiteral("accepted"));
        QVERIFY(savedConfig().isEmpty());

        call2("openIpConfig", QVariant(), QVariant());
        call0("closeSavedInfo");
        QVERIFY(!m_prompts->property("busy").toBool());
        respond(QStringLiteral("accepted"));
        QVERIFY(savedConfig().isEmpty());

        openSaved(QStringLiteral("Saved Network"));
        call2("openIpConfig", QVariant(), QVariant());
        m_host->setProperty("moduleId", QString());
        QVERIFY(!m_prompts->property("busy").toBool());
        respond(QStringLiteral("accepted"));
        QVERIFY(savedConfig().isEmpty());
    }

    void ipSettingsRefuseWhenUnavailable()
    {
        openIpSettings();
        const QVariantMap options = promptOptions();
        call2("openIpConfig", QVariant(), QVariant());
        QCOMPARE(promptOptions(), options);
        QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, QStringLiteral("renderer_lost")), Q_ARG(QVariant, QVariantMap{}));
        m_mock->setProperty("savedWifiInfoLoading", true);
        call2("openIpConfig", QVariant(), QVariant());
        QVERIFY(!m_prompts->property("busy").toBool());
        m_mock->setProperty("savedWifiInfoLoading", false);
        m_mock->setProperty("busy", true);
        call2("openIpConfig", QVariant(), QVariant());
        QVERIFY(!m_prompts->property("busy").toBool());
        m_mock->setProperty("busy", false);
        QMetaObject::invokeMethod(m_mock, "configurationSaved", Q_ARG(bool, false));
        QCOMPARE(find(QStringLiteral("wifiNotice"))->property("text").toString(), QStringLiteral("Saved. Reconnect to apply."));
    }

    void scanWindowRepeatsAndStops()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        loader()->item()->setProperty("scanDurationMs", 500);
        loader()->item()->setProperty("scanIntervalMs", 100);
        call1("stopScan", false);
        m_mock->setProperty("scanCount", 0);
        call0("startScan");
        QVERIFY(pageProperty("scanActive").toBool());
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("scan"));
        QCOMPARE(m_mock->property("scanCount").toInt(), 1);
        QTest::qWait(220);
        QVERIFY(m_mock->property("scanCount").toInt() >= 2);
        QTRY_VERIFY_WITH_TIMEOUT(!pageProperty("scanActive").toBool(), 1500);
        QCOMPARE(find(QStringLiteral("wifiRefreshSymbol"))->property("kind").toString(), QStringLiteral("refresh"));
        const int completed = m_mock->property("scanCount").toInt();
        QTest::qWait(170);
        QCOMPARE(m_mock->property("scanCount").toInt(), completed);
        QMetaObject::invokeMethod(find(QStringLiteral("wifiRefresh")), "clicked");
        QVERIFY(pageProperty("scanActive").toBool());
        QVERIFY(m_mock->property("scanCount").toInt() > completed);
    }

    void scanWaitsForAdapterReadiness()
    {
        m_mock->setProperty("wifiScanAvailable", false);
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QVERIFY(pageProperty("scanActive").toBool());
        QCOMPARE(m_mock->property("scanCount").toInt(), 0);
        QVERIFY(m_mock->property("refreshCount").toInt() > 0);
        m_mock->setProperty("wifiScanAvailable", true);
        QMetaObject::invokeMethod(m_mock, "changed");
        QCOMPARE(m_mock->property("scanCount").toInt(), 1);
        m_mock->setProperty("wifiScanAvailable", false);
        QMetaObject::invokeMethod(m_mock, "changed");
        QMetaObject::invokeMethod(m_mock, "scanDeferred");
        QCOMPARE(m_mock->property("scanCount").toInt(), 1);
        m_mock->setProperty("wifiScanAvailable", true);
        QMetaObject::invokeMethod(m_mock, "changed");
        QCOMPARE(m_mock->property("scanCount").toInt(), 2);
    }

    void keepsSavedNetworksVisibleWithoutScanResults()
    {
        setNetworks({network(QStringLiteral("Remembered"), QStringLiteral("WPA Personal"), 0, false, true)});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        QQuickItem *saved = find(QStringLiteral("wifiNetwork_Remembered"));
        QVERIFY(saved);
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());
        QMetaObject::invokeMethod(saved, "clicked");
        QCOMPARE(m_mock->property("connectCalls").toList().last().toMap().value(QStringLiteral("ssid")).toString(), QStringLiteral("Remembered"));
        setNetworks({network(QStringLiteral("Remembered"), QStringLiteral("WPA Personal"), 0, false, true, true)});
        QQuickItem *connecting = find(QStringLiteral("wifiNetwork_Remembered"));
        QVERIFY(connecting->property("connecting").toBool());
        QCOMPARE(connecting->height(), 94.0);
        QVERIFY(!connecting->isEnabled());
        setNetworks({network(QStringLiteral("Remembered"), QStringLiteral("WPA Personal"), 0, true, true)});
        QVERIFY(!find(QStringLiteral("wifiStatusText"))->isVisible());
        QVERIFY(find(QStringLiteral("wifiNetwork_Remembered"))->property("connected").toBool());
    }

    void pollingContinuesAtConfiguredInterval()
    {
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        loader()->item()->setProperty("refreshIntervalMs", 100);
        const int before = m_mock->property("refreshCount").toInt();
        QTRY_VERIFY_WITH_TIMEOUT(m_mock->property("refreshCount").toInt() > before + 2, 2000);
        setNetworks({network(QStringLiteral("Nearby"), QStringLiteral("Open"), 60)});
        QTRY_VERIFY(find(QStringLiteral("wifiNetwork_Nearby")));
    }

private:
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

    QVariant call2(const char *method, const QVariant &first, const QVariant &second)
    {
        QVariant result;
        QMetaObject::invokeMethod(loader()->item(), method, Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, first), Q_ARG(QVariant, second));
        return result;
    }

    void answerForm(const QString &result, const QVariantMap &values)
    {
        QMetaObject::invokeMethod(m_prompts, "respondFields", Q_ARG(QVariant, result), Q_ARG(QVariant, QVariant(values)));
    }

    QVariantMap promptOptions() const { return m_prompts->property("options").toMap(); }
    QVariantMap fieldOptions(const QString &name) const
    {
        return promptOptions().value(QStringLiteral("fields")).toMap().value(name).toMap();
    }
    QVariantMap savedConfig() const { return m_mock->property("savedIpConfig").toMap(); }
    QVariant pageProperty(const char *name) { return loader()->item()->property(name); }

    void openSaved(const QString &ssid)
    {
        call1("openSavedInfo", QVariantMap{{QStringLiteral("ssid"), ssid}, {QStringLiteral("saved"), true}});
    }

    void openIpSettings()
    {
        m_mock->setProperty("networks", QVariantList{});
        QVERIFY2(loadPage(), qPrintable(loaderError()));
        openSaved(QStringLiteral("Saved Network"));
        call2("openIpConfig", QVariant(), QVariant());
    }

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
        m_host->setProperty("moduleId", WifiId);
        return loader()->loaded();
    }

    SettingsModuleLoader *loader() const
    {
        return m_host->property("loader").value<SettingsModuleLoader *>();
    }

    QString loaderError() const { return loader()->errorString(); }

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

    QQuickItem *find(const QString &name) const
    {
        return search(m_host.data(), name);
    }

    QString text(QQuickItem *parent, const QString &name) const
    {
        QQuickItem *child = search(parent, name);
        return child ? child->property("text").toString() : QString();
    }

    void setNetworks(const QVariantList &list)
    {
        m_mock->setProperty("networks", list);
    }

    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QQuickItem> m_host;
    QScopedPointer<QQuickWindow> m_window;
    QObject *m_mock = nullptr;
    QObject *m_prompts = nullptr;
};

QTEST_MAIN(WifiModuleTest)
#include "tst_wifimodule.moc"
