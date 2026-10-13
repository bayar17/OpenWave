#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>

class QuickMenuBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiConnected READ wifiConnected NOTIFY changed)
    Q_PROPERTY(int wifiStrength READ wifiStrength NOTIFY changed)
    Q_PROPERTY(bool cellularAvailable READ cellularAvailable NOTIFY changed)
    Q_PROPERTY(int cellularStrength READ cellularStrength NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool airplaneMode READ airplaneMode NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness NOTIFY changed)
    Q_PROPERTY(bool hardwareBrightness READ hardwareBrightness CONSTANT)
    Q_PROPERTY(bool softwareBrightnessActive READ softwareBrightnessActive NOTIFY changed)
    Q_PROPERTY(QString modemLabel READ modemLabel NOTIFY changed)

public:
    explicit QuickMenuBackend(QObject *parent = nullptr);
    ~QuickMenuBackend() override;

    bool wifiEnabled() const { return m_wifiEnabled; }
    bool wifiAvailable() const { return m_wifiAvailable; }
    bool wifiConnected() const { return m_wifiConnected; }
    int wifiStrength() const { return m_wifiStrength; }
    bool cellularAvailable() const { return m_cellularAvailable; }
    int cellularStrength() const { return m_cellularStrength; }
    bool bluetoothEnabled() const { return m_bluetoothEnabled; }
    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool airplaneMode() const { return m_airplaneMode; }
    int volume() const { return m_volume; }
    int brightness() const { return m_brightness; }
    bool hardwareBrightness() const { return !m_backlightPath.isEmpty(); }
    bool softwareBrightnessActive() const { return m_softwareBrightnessActive; }
    QString modemLabel() const { return m_modemLabel; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPolling(bool enabled);
    Q_INVOKABLE void setWifiEnabled(bool enabled);
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void setAirplaneMode(bool enabled);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void setBrightness(int brightness);

signals:
    void changed();
    void operationFailed(const QString &message);

private:
    void setRadio(const char *method, bool enabled);
    QThreadPool m_pool;
    QTimer m_pollTimer;
    QTimer m_volumeTimer;
    bool m_refreshPending = false;
    bool m_refreshAgain = false;
    bool m_wifiEnabled = false;
    bool m_wifiAvailable = false;
    bool m_wifiConnected = false;
    int m_wifiStrength = 0;
    bool m_cellularAvailable = false;
    int m_cellularStrength = 0;
    bool m_bluetoothEnabled = false;
    bool m_bluetoothAvailable = false;
    bool m_airplaneMode = false;
    bool m_restoreWifi = false;
    bool m_restoreBluetooth = false;
    int m_volume = 40;
    int m_brightness = 60;
    QString m_backlightPath;
    int m_maxBrightness = 0;
    bool m_softwareBrightnessActive = false;
    QString m_modemLabel = QStringLiteral("NO MODEM");
};
