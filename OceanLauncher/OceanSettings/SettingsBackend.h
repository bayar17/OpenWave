#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>
#include <QThreadPool>
#include <QTimer>

class SettingsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY preferencesChanged)
    Q_PROPERTY(QString localHostname READ localHostname NOTIFY preferencesChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY preferencesChanged)
    Q_PROPERTY(bool largeText READ largeText NOTIFY preferencesChanged)
    Q_PROPERTY(QString bluetoothStatus READ bluetoothStatus NOTIFY radiosChanged)
    Q_PROPERTY(QString wifiStatus READ wifiStatus NOTIFY radiosChanged)
    Q_PROPERTY(QVariantMap systemInfo READ systemInfo CONSTANT)
    Q_PROPERTY(bool powerActionsAvailable READ powerActionsAvailable NOTIFY powerStateChanged)
    Q_PROPERTY(bool powerBusy READ powerBusy NOTIFY powerStateChanged)

public:
    explicit SettingsBackend(const QString &settingsPath, QObject *parent = nullptr);
    ~SettingsBackend() override { m_powerTimer.stop(); m_powerPool.waitForDone(); }
    QString deviceName() const;
    QString localHostname() const;
    bool reduceMotion() const;
    bool largeText() const;
    QString bluetoothStatus() const { return m_bluetoothStatus; }
    QString wifiStatus() const { return m_wifiStatus; }
    QVariantMap systemInfo() const { return m_systemInfo; }
    bool powerActionsAvailable() const { return m_powerAvailable && !m_powerBusy; }
    bool powerBusy() const { return m_powerBusy; }

    Q_INVOKABLE bool saveDeviceName(const QString &name);
    Q_INVOKABLE bool setReduceMotion(bool enabled);
    Q_INVOKABLE bool setLargeText(bool enabled);
    Q_INVOKABLE void refreshRadios();
    Q_INVOKABLE void closeApp(bool home);
    Q_INVOKABLE bool requestPowerAction(const QString &action);

signals:
    void homeRequested();
    void preferencesChanged();
    void radiosChanged();
    void errorOccurred(const QString &message);
    void powerStateChanged();

private:
    bool save(const QString &key, const QVariant &value);
    void refreshPowerService();
    QTimer m_powerTimer;
    QThreadPool m_powerPool;
    bool m_powerAvailable = false, m_powerBusy = false, m_powerChecking = false;
    QSettings m_settings;
    QString m_bluetoothStatus;
    QString m_wifiStatus;
    QVariantMap m_systemInfo;
};
