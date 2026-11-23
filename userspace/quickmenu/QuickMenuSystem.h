#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

class QuickMenuSystem : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(System)
    QML_SINGLETON
    Q_PROPERTY(bool connected READ connected NOTIFY changed FINAL)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed FINAL)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed FINAL)
    Q_PROPERTY(bool wifiConnected READ wifiConnected NOTIFY changed FINAL)
    Q_PROPERTY(int wifiStrength READ wifiStrength NOTIFY changed FINAL)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed FINAL)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY changed FINAL)
    Q_PROPERTY(bool airplaneMode READ airplaneMode NOTIFY changed FINAL)
    Q_PROPERTY(bool cellularAvailable READ cellularAvailable NOTIFY changed FINAL)
    Q_PROPERTY(int cellularStrength READ cellularStrength NOTIFY changed FINAL)
    Q_PROPERTY(QString modemLabel READ modemLabel NOTIFY changed FINAL)
    Q_PROPERTY(bool mobileDataSupported READ mobileDataSupported CONSTANT FINAL)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryChanged FINAL)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryChanged FINAL)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged FINAL)
    Q_PROPERTY(int volume READ volume NOTIFY changed FINAL)
    Q_PROPERTY(int brightness READ brightness NOTIFY changed FINAL)

public:
    explicit QuickMenuSystem(QObject *parent);
    QuickMenuSystem() = delete;

    static QuickMenuSystem *shared();
    static QuickMenuSystem *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    void attach(QObject *backend);
    void setPowerSupplyRoot(const QString &root);

    bool connected() const;
    bool wifiAvailable() const;
    bool wifiEnabled() const;
    bool wifiConnected() const;
    int wifiStrength() const;
    bool bluetoothAvailable() const;
    bool bluetoothEnabled() const;
    bool airplaneMode() const;
    bool cellularAvailable() const;
    int cellularStrength() const;
    QString modemLabel() const;
    bool mobileDataSupported() const;
    bool batteryAvailable() const;
    int batteryPercent() const;
    bool batteryCharging() const;
    int volume() const;
    int brightness() const;

    Q_INVOKABLE void setWifiEnabled(bool enabled);
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void setAirplaneMode(bool enabled);
    Q_INVOKABLE void setMobileDataEnabled(bool enabled);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void setBrightness(int brightness);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void refreshBattery();

signals:
    void changed();
    void batteryChanged();
    void operationFailed(const QString &message);
    void lockRequested();

private:
    bool flag(const char *name) const;
    int number(const char *name) const;
    bool invoke(const char *method, bool value);
    bool invokeNumber(const char *method, int value);
    void fail(const QString &message);

    QPointer<QObject> m_backend;
    QString m_powerSupplyRoot;
    bool m_batteryAvailable = false;
    int m_batteryPercent = 0;
    bool m_batteryCharging = false;
};
