#include "QuickMenuAppearance.h"
#include "QuickMenuAppletRegistry.h"
#include "QuickMenuSystem.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wifiAvailable MEMBER wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled MEMBER wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiConnected MEMBER wifiConnected NOTIFY changed)
    Q_PROPERTY(int wifiStrength MEMBER wifiStrength NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable MEMBER bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled MEMBER bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool airplaneMode MEMBER airplaneMode NOTIFY changed)
    Q_PROPERTY(bool cellularAvailable MEMBER cellularAvailable NOTIFY changed)
    Q_PROPERTY(int cellularStrength MEMBER cellularStrength NOTIFY changed)
    Q_PROPERTY(QString modemLabel MEMBER modemLabel NOTIFY changed)
    Q_PROPERTY(int volume MEMBER volume NOTIFY changed)
    Q_PROPERTY(int brightness MEMBER brightness NOTIFY changed)
    Q_PROPERTY(QStringList calls MEMBER calls NOTIFY changed)

public:
    bool wifiAvailable = true;
    bool wifiEnabled = false;
    bool wifiConnected = false;
    int wifiStrength = 0;
    bool bluetoothAvailable = true;
    bool bluetoothEnabled = false;
    bool airplaneMode = false;
    bool cellularAvailable = false;
    int cellularStrength = 0;
    QString modemLabel = QStringLiteral("NO MODEM");
    int volume = 40;
    int brightness = 60;
    QStringList calls;

    Q_INVOKABLE void setWifiEnabled(bool enabled) { calls << QStringLiteral("wifi:%1").arg(enabled); wifiEnabled = enabled; emit changed(); }
    Q_INVOKABLE void setBluetoothEnabled(bool enabled) { calls << QStringLiteral("bluetooth:%1").arg(enabled); bluetoothEnabled = enabled; emit changed(); }
    Q_INVOKABLE void setAirplaneMode(bool enabled) { calls << QStringLiteral("airplane:%1").arg(enabled); airplaneMode = enabled; emit changed(); }
    Q_INVOKABLE void setVolume(int value) { calls << QStringLiteral("volume:%1").arg(value); volume = value; emit changed(); }
    Q_INVOKABLE void setBrightness(int value) { calls << QStringLiteral("brightness:%1").arg(value); brightness = value; emit changed(); }
    Q_INVOKABLE void touch() { emit changed(); }

signals:
    void changed();
    void operationFailed(const QString &message);
};

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup()
    {
        QuickMenuAppletRegistry *registry = QuickMenuAppletRegistry::shared();
        registry->setPreferencesPath(m_directory.filePath(QStringLiteral("prefs.ini")));
        registry->setRoots({{QuickMenuAppletRegistry::Needed, QStringLiteral(QUICKMENU_SOURCE_DIR "/applets/Needed")},
                            {QuickMenuAppletRegistry::System, QStringLiteral(QUICKMENU_SOURCE_DIR "/applets")},
                            {QuickMenuAppletRegistry::User, QStringLiteral(FIXTURE_DIR "/applets")}});
        QuickMenuAppearance *appearance = QuickMenuAppearance::shared();
        appearance->setPreferencesPath(m_directory.filePath(QStringLiteral("prefs.ini")));
        appearance->setPresetDirectories({QStringLiteral(QUICKMENU_SOURCE_DIR "/tests/fixtures/appearances")});
        QuickMenuSystem::shared()->setPowerSupplyRoot(m_directory.filePath(QStringLiteral("power")));
        QuickMenuSystem::shared()->attach(&m_backend);
    }

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("TestBackend"), &m_backend);
        engine->rootContext()->setContextProperty(QStringLiteral("TestHooks"), this);
    }

public:
    Q_INVOKABLE void setAppletEnabled(const QString &id, bool enabled)
    {
        QuickMenuAppletRegistry::shared()->setEnabled(id, enabled);
    }

    Q_INVOKABLE void writeBattery(int percent, const QString &status)
    {
        const QString base = m_directory.filePath(QStringLiteral("power/BAT0"));
        QDir().mkpath(base);
        const auto write = [&base](const QString &name, const QString &value) {
            QFile file(base + QLatin1Char('/') + name);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.write(value.toUtf8());
        };
        write(QStringLiteral("type"), QStringLiteral("Battery\n"));
        write(QStringLiteral("capacity"), QString::number(percent));
        write(QStringLiteral("status"), status);
        QuickMenuSystem::shared()->refreshBattery();
    }

    Q_INVOKABLE void useFixtureAppearances(bool enabled)
    {
        QuickMenuAppearance::shared()->setPresetDirectories(
            enabled ? QStringList{QStringLiteral(QUICKMENU_SOURCE_DIR "/tests/fixtures/appearances")} : QStringList{});
    }

    Q_INVOKABLE void reloadApplets()
    {
        QuickMenuAppletRegistry::shared()->reload();
    }

    Q_INVOKABLE void resetBackend()
    {
        m_backend.calls.clear();
        m_backend.wifiAvailable = true;
        m_backend.wifiEnabled = false;
        m_backend.bluetoothAvailable = true;
        m_backend.bluetoothEnabled = false;
        m_backend.airplaneMode = false;
        m_backend.cellularAvailable = false;
        m_backend.volume = 40;
        m_backend.brightness = 60;
        m_backend.touch();
    }

private:
    QTemporaryDir m_directory;
    Backend m_backend;
};

QUICK_TEST_MAIN_WITH_SETUP(QuickMenuQml, Setup)
#include "main.moc"
