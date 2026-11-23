#include "SettingsBackend.h"
#include "SettingsGeneralService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FakeTime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serviceAvailable MEMBER m_available NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER m_busy NOTIFY changed)
    Q_PROPERTY(QString timezone MEMBER m_timezone NOTIFY changed)
    Q_PROPERTY(QStringList zones MEMBER m_zones NOTIFY changed)

public:
    bool m_available = true;
    bool m_busy = false;
    QString m_timezone = QStringLiteral("Europe/Istanbul");
    QStringList m_zones{QStringLiteral("Europe/Istanbul"), QStringLiteral("UTC")};
    QString requested;
    int refreshes = 0;

    Q_INVOKABLE void setTimezone(const QString &zone) { requested = zone; }
    Q_INVOKABLE void refresh() { ++refreshes; }

signals:
    void changed();
    void errorOccurred(const QString &message);
};

class FakeConnectivity : public QObject
{
    Q_OBJECT

public:
    QString name;
    Q_INVOKABLE void setDeviceName(const QString &value) { name = value; }
};

class SettingsGeneralServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_settings.reset(new SettingsBackend(m_directory.filePath(QStringLiteral("settings-%1.ini")).arg(++m_counter)));
        m_time.reset(new FakeTime);
        m_connectivity.reset(new FakeConnectivity);
        m_service.reset(new SettingsGeneralService(m_settings.data(), m_time.data(), m_connectivity.data()));
    }

    void exposesReadOnlyValues()
    {
        QCOMPARE(m_service->deviceName(), m_settings->deviceName());
        QCOMPARE(m_service->timezone(), QStringLiteral("Europe/Istanbul"));
        QCOMPARE(m_service->zones(), m_time->m_zones);
        QVERIFY(m_service->timeServiceAvailable());
        QVERIFY(!m_service->timeBusy());
        QCOMPARE(m_service->systemInfo(), m_settings->systemInfo());
        const QMetaObject *meta = m_service->metaObject();
        for (const char *name : {"deviceName", "systemInfo", "powerActionsAvailable", "powerBusy", "timeServiceAvailable", "timeBusy", "timezone", "zones"})
            QVERIFY(!meta->property(meta->indexOfProperty(name)).isWritable());
        QCOMPARE(meta->indexOfMethod("closeApp(bool)"), -1);
        QCOMPARE(meta->indexOfMethod("setLargeText(bool)"), -1);
    }

    void savesDeviceNameAndForwardsItToConnectivity()
    {
        QSignalSpy changed(m_service.data(), &SettingsGeneralService::changed);
        QVERIFY(m_service->saveDeviceName(QStringLiteral("My Phone")));
        QCOMPARE(m_service->deviceName(), QStringLiteral("My Phone"));
        QCOMPARE(m_connectivity->name, QStringLiteral("My Phone"));
        QVERIFY(changed.count() >= 1);
        QVERIFY(!m_service->saveDeviceName(QStringLiteral("   ")));
        QVERIFY(!m_service->saveDeviceName(QString(65, QLatin1Char('x'))));
        QCOMPARE(m_service->deviceName(), QStringLiteral("My Phone"));
    }

    void setsOnlyKnownTimeZonesWhenAvailable()
    {
        QVERIFY(m_service->setTimezone(QStringLiteral("UTC")));
        QCOMPARE(m_time->requested, QStringLiteral("UTC"));
        m_time->requested.clear();
        QVERIFY(!m_service->setTimezone(QStringLiteral("Mars/Olympus")));
        QVERIFY(!m_service->setTimezone(QStringLiteral("../../etc/passwd")));
        m_time->m_busy = true;
        QVERIFY(!m_service->setTimezone(QStringLiteral("UTC")));
        m_time->m_busy = false;
        m_time->m_available = false;
        QVERIFY(!m_service->setTimezone(QStringLiteral("UTC")));
        QCOMPARE(m_time->requested, QString());
    }

    void acceptsOnlyRestartAndShutdown()
    {
        for (const QString &action : {QStringLiteral("reboot"), QStringLiteral("poweroff"), QStringLiteral(""), QStringLiteral("restart; rm -rf /"), QStringLiteral("Shutdown")})
            QVERIFY2(!m_service->requestPowerAction(action), qPrintable(action));
    }

    void forwardsChangeAndErrorSignals()
    {
        QSignalSpy changed(m_service.data(), &SettingsGeneralService::changed);
        QSignalSpy errors(m_service.data(), &SettingsGeneralService::errorOccurred);
        emit m_time->changed();
        QCOMPARE(changed.count(), 1);
        emit m_time->errorOccurred(QStringLiteral("zone failure"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), QStringLiteral("zone failure"));
        emit m_settings->errorOccurred(QStringLiteral("power failure"));
        QCOMPARE(errors.count(), 2);
    }

    void refreshesTimeZonesThroughTheTimeService()
    {
        m_service->refreshTimeZones();
        QCOMPARE(m_time->refreshes, 1);
    }

    void toleratesMissingServices()
    {
        SettingsGeneralService service(nullptr, nullptr, nullptr);
        QCOMPARE(service.deviceName(), QString());
        QVERIFY(!service.timeServiceAvailable());
        QVERIFY(!service.powerActionsAvailable());
        QVERIFY(service.zones().isEmpty());
        QVERIFY(!service.saveDeviceName(QStringLiteral("x")));
        QVERIFY(!service.setTimezone(QStringLiteral("UTC")));
        QVERIFY(!service.requestPowerAction(QStringLiteral("restart")));
        service.refreshTimeZones();
    }

private:
    QTemporaryDir m_directory;
    int m_counter = 0;
    QScopedPointer<SettingsBackend> m_settings;
    QScopedPointer<FakeTime> m_time;
    QScopedPointer<FakeConnectivity> m_connectivity;
    QScopedPointer<SettingsGeneralService> m_service;
};

QTEST_MAIN(SettingsGeneralServiceTest)
#include "tst_settingsgeneralservice.moc"
