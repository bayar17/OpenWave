#pragma once

#include <QObject>
#include <QThreadPool>
#include <QStringList>
#include <QVariantList>

class ConnectivityBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiDeviceAvailable READ wifiDeviceAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiScanAvailable READ wifiScanAvailable NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString wifiState READ wifiState NOTIFY changed)
    Q_PROPERTY(QString activeSsid READ activeSsid NOTIFY changed)
    Q_PROPERTY(QString wifiStatus READ wifiStatus NOTIFY changed)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)
    Q_PROPERTY(QVariantMap savedWifiInfo READ savedWifiInfo NOTIFY changed)
    Q_PROPERTY(bool savedWifiInfoLoading READ savedWifiInfoLoading NOTIFY changed)
    Q_PROPERTY(bool bluetoothAdapterAvailable READ bluetoothAdapterAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool bluetoothDiscovering READ bluetoothDiscovering NOTIFY changed)
    Q_PROPERTY(QVariantList bluetoothDevices READ bluetoothDevices NOTIFY changed)
    Q_PROPERTY(QString bluetoothStatus READ bluetoothStatus NOTIFY changed)
    Q_PROPERTY(QString bluetoothActionPath READ bluetoothActionPath NOTIFY changed)
    Q_PROPERTY(QString bluetoothActionState READ bluetoothActionState NOTIFY changed)

public:
    explicit ConnectivityBackend(QObject *parent = nullptr);
    ~ConnectivityBackend() override;

    bool serviceAvailable() const { return m_serviceAvailable; }
    bool wifiDeviceAvailable() const { return m_wifiDeviceAvailable; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    bool wifiScanAvailable() const { return m_wifiScanAvailable; }
    bool busy() const { return m_busy; }
    QString wifiState() const { return m_wifiState; }
    QString activeSsid() const { return m_activeSsid; }
    QString wifiStatus() const;
    QVariantList networks() const { return m_networks; }
    QVariantMap savedWifiInfo() const { return m_savedWifiInfo; }
    bool savedWifiInfoLoading() const { return m_savedWifiInfoLoading; }
    bool bluetoothAdapterAvailable() const { return m_bluetoothAdapterAvailable; }
    bool bluetoothEnabled() const { return m_bluetoothEnabled; }
    bool bluetoothDiscovering() const { return m_bluetoothDiscovering; }
    QVariantList bluetoothDevices() const { return m_bluetoothDevices; }
    QString bluetoothStatus() const;
    QString bluetoothActionPath() const { return m_bluetoothActionPath; }
    QString bluetoothActionState() const { return m_bluetoothActionState; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void requestScan();
    Q_INVOKABLE void setWifiEnabled(bool enabled);
    Q_INVOKABLE void connectWifi(const QString &ssid, const QString &password);
    Q_INVOKABLE void addWifiNetwork(const QString &ssid, bool secured,
                                   const QString &password);
    Q_INVOKABLE void disconnectWifi();
    Q_INVOKABLE void forgetWifi(const QString &ssid);
    Q_INVOKABLE void loadSavedWifiInfo(const QString &ssid);
    Q_INVOKABLE void clearSavedWifiInfo();
    Q_INVOKABLE void saveSavedWifiIpConfig(const QString &ssid, bool manualIp,
                                          const QString &address, int prefix,
                                          const QString &gateway, bool manualDns,
                                          const QStringList &dns);
    Q_INVOKABLE void setDeviceName(const QString &name);
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void startBluetoothDiscovery();
    Q_INVOKABLE void stopBluetoothDiscovery();
    Q_INVOKABLE void pairBluetoothDevice(const QString &path);
    Q_INVOKABLE void connectBluetoothDevice(const QString &path);
    Q_INVOKABLE void disconnectBluetoothDevice(const QString &path);
    Q_INVOKABLE void renameBluetoothDevice(const QString &path, const QString &nickname);
    Q_INVOKABLE void forgetBluetoothDevice(const QString &path);

signals:
    void changed();
    void errorOccurred(const QString &message);
    void scanDeferred();
    void networkAdded();
    void configurationSaved(bool applied);

private:
    enum class Operation { Refresh, Scan, Enable, Connect, Add, Disconnect, Forget,
                           SavedInfo, SaveIp, SetName, BluetoothEnable,
                           BluetoothDiscover, BluetoothStopDiscovery,
                           BluetoothPair, BluetoothConnect, BluetoothDisconnect,
                           BluetoothRename, BluetoothForget };
    struct Result {
        bool serviceAvailable = false;
        bool wifiDeviceAvailable = false;
        bool wifiEnabled = false;
        bool wifiScanAvailable = false;
        bool scanDeferred = false;
        QString wifiState;
        QString activeSsid;
        QVariantList networks;
        QVariantMap savedWifiInfo;
        QString infoSsid;
        bool applied = false;
        bool actionSucceeded = false;
        QString error;
        bool bluetoothAdapterAvailable = false;
        bool bluetoothEnabled = false;
        bool bluetoothDiscovering = false;
        QVariantList bluetoothDevices;
    };

    void start(Operation operation, const QString &ssid = {},
               const QString &password = {}, bool enabled = false,
               const QVariantMap &config = {});
    void apply(const Result &result, Operation operation, const QString &actionPath);
    static Result execute(Operation operation, const QString &ssid,
                          const QString &password, bool enabled,
                          const QVariantMap &config);

    QThreadPool m_pool;
    bool m_inFlight = false;
    bool m_busy = false;
    bool m_serviceAvailable = false;
    bool m_wifiDeviceAvailable = false;
    bool m_wifiEnabled = false;
    bool m_wifiScanAvailable = false;
    QString m_wifiState;
    QString m_activeSsid;
    QVariantList m_networks;
    QVariantMap m_savedWifiInfo;
    QString m_requestedInfoSsid;
    bool m_savedWifiInfoLoading = false;
    bool m_bluetoothAdapterAvailable = false;
    bool m_bluetoothEnabled = false;
    bool m_bluetoothDiscovering = false;
    QVariantList m_bluetoothDevices;
    QString m_bluetoothActionPath;
    QString m_bluetoothActionState;
    bool m_hasQueuedAction = false;
    Operation m_queuedAction = Operation::Refresh;
    QString m_queuedSsid;
    QString m_queuedPassword;
    bool m_queuedEnabled = false;
    QVariantMap m_queuedConfig;
};
