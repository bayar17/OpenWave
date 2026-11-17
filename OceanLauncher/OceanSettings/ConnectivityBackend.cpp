#include "ConnectivityBackend.h"

#include <QMetaObject>
#include <QSet>
#include <QStringList>
#include <algorithm>
#undef signals
#include <gio/gio.h>

namespace {
constexpr auto busName = "org.openwave.Connectivity";
constexpr auto objectPath = "/org/openwave/Connectivity";
constexpr auto interfaceName = "org.openwave.Connectivity1";

GVariant *call(GDBusConnection *bus, const char *method,
               GVariant *parameters, QString *error, int timeout = 15000,
               bool *scanDeferred = nullptr, QString *remoteError = nullptr)
{
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        bus, busName, objectPath, interfaceName, method, parameters, nullptr,
        G_DBUS_CALL_FLAGS_NONE, timeout, nullptr, &failure);
    if (failure) {
        if (g_dbus_error_is_remote_error(failure)) {
            gchar *remoteName = g_dbus_error_get_remote_error(failure);
            if (scanDeferred)
                *scanDeferred = g_strcmp0(remoteName,
                    "org.openwave.Connectivity.ScanUnavailable") == 0;
            if (remoteError)
                *remoteError = QString::fromUtf8(remoteName);
            g_free(remoteName);
        }
        *error = QString::fromUtf8(failure->message);
        g_error_free(failure);
    }
    return reply;
}

QVariantMap parseSavedWifiInfo(GVariant *reply)
{
    QVariantMap result;
    GVariant *info = g_variant_get_child_value(reply, 0);
    for (const char *key : {"ssid", "security", "password", "passwordStatus", "bssid",
                             "ipv4Address", "gateway", "dns", "configuredAddress",
                             "configuredGateway"}) {
        const gchar *value = nullptr;
        if (g_variant_lookup(info, key, "&s", &value))
            result.insert(QString::fromUtf8(key), QString::fromUtf8(value));
    }
    for (const char *key : {"autoConnect", "connected", "ipManual", "dnsManual"}) {
        gboolean value = FALSE;
        if (g_variant_lookup(info, key, "b", &value))
            result.insert(QString::fromUtf8(key), bool(value));
    }
    for (const char *key : {"strength", "frequency", "configuredPrefix"}) {
        guint32 value = 0;
        if (g_variant_lookup(info, key, "u", &value))
            result.insert(QString::fromUtf8(key), value);
    }
    GVariant *dns = g_variant_lookup_value(info, "configuredDns", G_VARIANT_TYPE("as"));
    if (dns) {
        QStringList servers;
        GVariantIter iter;
        const gchar *server = nullptr;
        g_variant_iter_init(&iter, dns);
        while (g_variant_iter_next(&iter, "&s", &server))
            servers.append(QString::fromUtf8(server));
        result.insert(QStringLiteral("configuredDns"), servers);
        g_variant_unref(dns);
    }
    g_variant_unref(info);
    return result;
}
}

ConnectivityBackend::ConnectivityBackend(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    refresh();
}

ConnectivityBackend::~ConnectivityBackend()
{
    m_pool.waitForDone();
}

QString ConnectivityBackend::wifiStatus() const
{
    if (!m_serviceAvailable)
        return QStringLiteral("Connectivity service unavailable.");
    if (!m_wifiDeviceAvailable)
        return QStringLiteral("No Wi-Fi adapter found.");
    if (!m_wifiEnabled)
        return QStringLiteral("Wi-Fi is off.");
    if (m_wifiState == QStringLiteral("Connected"))
        return m_activeSsid.isEmpty()
            ? QStringLiteral("Connected")
            : QStringLiteral("Connected to %1").arg(m_activeSsid);
    if (m_busy)
        return QStringLiteral("Working…");
    if (m_wifiState == QStringLiteral("Connecting"))
        return QStringLiteral("Connecting to Wi-Fi…");
    if (!m_wifiScanAvailable)
        return QStringLiteral("Wi-Fi adapter is getting ready.");
    if (m_networks.isEmpty())
        return QStringLiteral("No networks found. Tap refresh to scan.");
    return QStringLiteral("%1 network%2 available")
        .arg(m_networks.size())
        .arg(m_networks.size() == 1 ? QString() : QStringLiteral("s"));
}

QString ConnectivityBackend::bluetoothStatus() const
{
    if (!m_serviceAvailable)
        return QStringLiteral("Connectivity service unavailable.");
    if (!m_bluetoothAdapterAvailable)
        return QStringLiteral("No Bluetooth adapter found. Attach the dongle to QEMU.");
    if (!m_bluetoothEnabled)
        return QStringLiteral("Bluetooth is off. Tap Bluetooth to turn it on.");
    if (m_bluetoothDiscovering)
        return QStringLiteral("Searching for nearby Bluetooth devices…");
    if (m_bluetoothDevices.isEmpty())
        return QStringLiteral("No devices found. Tap refresh to search.");
    return QStringLiteral("Tap a device to pair or connect.");
}

void ConnectivityBackend::refresh()
{
    start(Operation::Refresh);
}

void ConnectivityBackend::requestScan()
{
    start(Operation::Scan);
}

void ConnectivityBackend::setWifiEnabled(bool enabled)
{
    start(Operation::Enable, {}, {}, enabled);
}

void ConnectivityBackend::connectWifi(const QString &ssid, const QString &password)
{
    if (ssid.isEmpty()) {
        emit errorOccurred(QStringLiteral("Select a Wi-Fi network first."));
        return;
    }
    start(Operation::Connect, ssid, password);
}

void ConnectivityBackend::addWifiNetwork(const QString &ssid, bool secured,
                                          const QString &password)
{
    start(Operation::Add, ssid.trimmed(), password, secured);
}

void ConnectivityBackend::disconnectWifi()
{
    start(Operation::Disconnect);
}

void ConnectivityBackend::forgetWifi(const QString &ssid)
{
    if (!ssid.isEmpty()) start(Operation::Forget, ssid);
}

void ConnectivityBackend::loadSavedWifiInfo(const QString &ssid)
{
    if (ssid.isEmpty())
        return;
    m_requestedInfoSsid = ssid;
    m_savedWifiInfo = {{QStringLiteral("ssid"), ssid}};
    m_savedWifiInfoLoading = true;
    emit changed();
    start(Operation::SavedInfo, ssid);
}

void ConnectivityBackend::clearSavedWifiInfo()
{
    m_requestedInfoSsid.clear();
    m_savedWifiInfo.clear();
    m_savedWifiInfoLoading = false;
    emit changed();
}

void ConnectivityBackend::saveSavedWifiIpConfig(const QString &ssid, bool manualIp,
                                                 const QString &address, int prefix,
                                                 const QString &gateway, bool manualDns,
                                                 const QStringList &dns)
{
    if (ssid.isEmpty())
        return;
    start(Operation::SaveIp, ssid, {}, false,
          {{QStringLiteral("manualIp"), manualIp},
           {QStringLiteral("address"), address},
           {QStringLiteral("prefix"), prefix},
           {QStringLiteral("gateway"), gateway},
           {QStringLiteral("manualDns"), manualDns},
           {QStringLiteral("dns"), dns}});
}

void ConnectivityBackend::setDeviceName(const QString &name)
{
    start(Operation::SetName, name);
}

void ConnectivityBackend::setBluetoothEnabled(bool enabled)
{
    start(Operation::BluetoothEnable, {}, {}, enabled);
}

void ConnectivityBackend::startBluetoothDiscovery()
{
    start(Operation::BluetoothDiscover);
}

void ConnectivityBackend::stopBluetoothDiscovery()
{
    start(Operation::BluetoothStopDiscovery);
}

static bool validBluetoothDevicePath(const QString &path)
{
    const QByteArray encoded = path.toUtf8();
    return path.startsWith(QStringLiteral("/org/bluez/hci"))
        && path.contains(QStringLiteral("/dev_"))
        && g_variant_is_object_path(encoded.constData());
}

void ConnectivityBackend::pairBluetoothDevice(const QString &path)
{
    if (validBluetoothDevicePath(path))
        start(Operation::BluetoothPair, path);
    else
        emit errorOccurred(QStringLiteral("Invalid Bluetooth device."));
}

void ConnectivityBackend::connectBluetoothDevice(const QString &path)
{
    if (validBluetoothDevicePath(path))
        start(Operation::BluetoothConnect, path);
    else
        emit errorOccurred(QStringLiteral("Invalid Bluetooth device."));
}

void ConnectivityBackend::disconnectBluetoothDevice(const QString &path)
{
    if (validBluetoothDevicePath(path))
        start(Operation::BluetoothDisconnect, path);
    else
        emit errorOccurred(QStringLiteral("Invalid Bluetooth device."));
}

void ConnectivityBackend::renameBluetoothDevice(const QString &path, const QString &nickname)
{
    const QString alias = nickname.trimmed();
    if (!validBluetoothDevicePath(path) || alias.isEmpty() || alias.size() > 248 ||
        alias.contains(QChar::Null) || alias.contains(QChar::LineFeed) || alias.contains(QChar::CarriageReturn)) {
        emit errorOccurred(QStringLiteral("Enter a valid device nickname."));
        return;
    }
    start(Operation::BluetoothRename, path, alias);
}

void ConnectivityBackend::forgetBluetoothDevice(const QString &path)
{
    if (validBluetoothDevicePath(path))
        start(Operation::BluetoothForget, path);
    else
        emit errorOccurred(QStringLiteral("Invalid Bluetooth device."));
}

void ConnectivityBackend::start(Operation operation, const QString &ssid,
                                const QString &password, bool enabled,
                                const QVariantMap &config)
{
    if (m_inFlight) {
        if (operation != Operation::Refresh) {
            m_hasQueuedAction = true;
            m_queuedAction = operation;
            m_queuedSsid = ssid;
            m_queuedPassword = password;
            m_queuedEnabled = enabled;
            m_queuedConfig = config;
            if (operation == Operation::BluetoothPair || operation == Operation::BluetoothConnect) {
                m_busy = true;
                m_bluetoothActionPath = ssid;
                m_bluetoothActionState = operation == Operation::BluetoothPair
                    ? QStringLiteral("pairing") : QStringLiteral("connecting");
                emit changed();
            }
        }
        return;
    }
    m_inFlight = true;
    m_busy = operation != Operation::Refresh;
    if (operation == Operation::BluetoothPair || operation == Operation::BluetoothConnect) {
        m_bluetoothActionPath = ssid;
        m_bluetoothActionState = operation == Operation::BluetoothPair
            ? QStringLiteral("pairing") : QStringLiteral("connecting");
    }
    emit changed();
    m_pool.start([this, operation, ssid, password, enabled, config] {
        const Result result = execute(operation, ssid, password, enabled, config);
        QMetaObject::invokeMethod(this, [this, result, operation, ssid] {
            apply(result, operation, ssid);
        }, Qt::QueuedConnection);
    });
}

void ConnectivityBackend::apply(const Result &result, Operation operation, const QString &actionPath)
{
    m_inFlight = false;
    m_busy = false;
    m_serviceAvailable = result.serviceAvailable;
    m_wifiDeviceAvailable = result.wifiDeviceAvailable;
    m_wifiEnabled = result.wifiEnabled;
    m_wifiScanAvailable = result.wifiScanAvailable && !result.scanDeferred;
    m_wifiState = result.wifiState;
    m_activeSsid = result.activeSsid;
    m_bluetoothAdapterAvailable = result.bluetoothAdapterAvailable;
    m_bluetoothEnabled = result.bluetoothEnabled;
    m_bluetoothDiscovering = result.bluetoothDiscovering;
    m_bluetoothDevices = result.bluetoothDevices;
    if (operation == Operation::BluetoothPair || operation == Operation::BluetoothConnect) {
        m_bluetoothActionPath.clear();
        m_bluetoothActionState.clear();
    }
    if ((operation == Operation::Forget && result.actionSucceeded) || !result.networks.isEmpty() || !result.serviceAvailable ||
        !result.wifiDeviceAvailable || !result.wifiEnabled)
        m_networks = result.networks;
    else {
        for (QVariant &network : m_networks) {
            QVariantMap details = network.toMap();
            details.insert(QStringLiteral("connected"),
                result.wifiState == QStringLiteral("Connected") &&
                details.value(QStringLiteral("ssid")).toString() == result.activeSsid);
            details.insert(QStringLiteral("connecting"),
                result.wifiState == QStringLiteral("Connecting") &&
                details.value(QStringLiteral("ssid")).toString() == result.activeSsid);
            network = details;
        }
    }
    if ((operation == Operation::SavedInfo || operation == Operation::SaveIp) &&
        result.infoSsid == m_requestedInfoSsid) {
        m_savedWifiInfoLoading = false;
        if (!result.savedWifiInfo.isEmpty())
            m_savedWifiInfo = result.savedWifiInfo;
    }
    if (operation == Operation::BluetoothPair && result.actionSucceeded && !m_hasQueuedAction) {
        start(Operation::BluetoothConnect, actionPath);
        return;
    }
    emit changed();
    if (result.scanDeferred)
        emit scanDeferred();
    if (!result.error.isEmpty() && operation != Operation::Refresh &&
        (operation != Operation::SavedInfo || result.infoSsid == m_requestedInfoSsid))
        emit errorOccurred(result.error);
    if (operation == Operation::Add && result.error.isEmpty())
        emit networkAdded();
    if (operation == Operation::SaveIp && result.error.isEmpty())
        emit configurationSaved(result.applied);
    if (m_hasQueuedAction) {
        m_hasQueuedAction = false;
        start(m_queuedAction, m_queuedSsid, m_queuedPassword, m_queuedEnabled,
              m_queuedConfig);
        m_queuedPassword.clear();
        m_queuedConfig.clear();
    }
}

ConnectivityBackend::Result ConnectivityBackend::execute(
    Operation operation, const QString &ssid, const QString &password, bool enabled,
    const QVariantMap &config)
{
    Result result;
    if (operation == Operation::SavedInfo || operation == Operation::SaveIp)
        result.infoSsid = ssid;
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        result.error = failure
            ? QString::fromUtf8(failure->message)
            : QStringLiteral("System D-Bus is unavailable.");
        g_clear_error(&failure);
        return result;
    }

    const char *method = nullptr;
    GVariant *parameters = nullptr;
    QString configurationErrorName;
    switch (operation) {
    case Operation::Refresh: break;
    case Operation::Scan: method = "RequestWifiScan"; break;
    case Operation::Enable:
        method = "SetWifiEnabled";
        parameters = g_variant_new("(b)", enabled);
        break;
    case Operation::Connect:
        method = "ConnectWifi";
        parameters = g_variant_new("(ss)", ssid.toUtf8().constData(),
                                   password.toUtf8().constData());
        break;
    case Operation::Add:
        method = "AddWifiNetwork";
        parameters = g_variant_new("(sbs)", ssid.toUtf8().constData(), enabled,
                                   password.toUtf8().constData());
        break;
    case Operation::Disconnect: method = "DisconnectWifi"; break;
    case Operation::Forget:
        method = "ForgetWifi";
        parameters = g_variant_new("(s)", ssid.toUtf8().constData());
        break;
    case Operation::SavedInfo:
        method = "GetSavedWifiInfo";
        parameters = g_variant_new("(s)", ssid.toUtf8().constData());
        break;
    case Operation::SaveIp: {
        method = "SetSavedWifiIpConfig";
        GVariantBuilder servers;
        g_variant_builder_init(&servers, G_VARIANT_TYPE("as"));
        for (const QString &server : config.value(QStringLiteral("dns")).toStringList())
            g_variant_builder_add(&servers, "s", server.toUtf8().constData());
        parameters = g_variant_new("(sbsusb@as)", ssid.toUtf8().constData(),
            config.value(QStringLiteral("manualIp")).toBool(),
            config.value(QStringLiteral("address")).toString().toUtf8().constData(),
            config.value(QStringLiteral("prefix")).toUInt(),
            config.value(QStringLiteral("gateway")).toString().toUtf8().constData(),
            config.value(QStringLiteral("manualDns")).toBool(), g_variant_builder_end(&servers));
        break;
    }
    case Operation::SetName:
        method = "SetDeviceName";
        parameters = g_variant_new("(s)", ssid.toUtf8().constData());
        break;
    case Operation::BluetoothEnable:
        method = "SetBluetoothEnabled";
        parameters = g_variant_new("(b)", enabled);
        break;
    case Operation::BluetoothDiscover: method = "StartBluetoothDiscovery"; break;
    case Operation::BluetoothStopDiscovery: method = "StopBluetoothDiscovery"; break;
    case Operation::BluetoothPair:
    case Operation::BluetoothConnect:
    case Operation::BluetoothDisconnect:
    case Operation::BluetoothForget:
        method = operation == Operation::BluetoothPair ? "PairDevice" :
                 operation == Operation::BluetoothConnect ? "ConnectDevice" :
                 operation == Operation::BluetoothForget ? "RemoveDevice" : "DisconnectDevice";
        parameters = g_variant_new("(o)", ssid.toUtf8().constData());
        break;
    case Operation::BluetoothRename:
        method = "SetDeviceAlias";
        parameters = g_variant_new("(os)", ssid.toUtf8().constData(), password.toUtf8().constData());
        break;
    }
    if (method) {
        GVariant *reply = call(bus, method, parameters, &result.error,
                               operation == Operation::BluetoothPair ? 65000 : 30000,
                               operation == Operation::Scan ? &result.scanDeferred : nullptr,
                               operation == Operation::SaveIp ? &configurationErrorName : nullptr);
        result.actionSucceeded = reply != nullptr;
        if (operation == Operation::Add && !result.error.isEmpty())
            result.error = QStringLiteral("Could not add or connect to this network.");
        if (operation == Operation::SaveIp && !result.error.isEmpty()) {
            if (configurationErrorName.endsWith(QStringLiteral(".InvalidArgument")))
                result.error = QStringLiteral("Check the IP address, prefix, gateway, and DNS servers.");
            else if (configurationErrorName.endsWith(QStringLiteral(".PermissionDenied")))
                result.error = QStringLiteral("Permission denied while saving network settings.");
            else if (configurationErrorName.endsWith(QStringLiteral(".Unavailable")))
                result.error = QStringLiteral("NetworkManager is unavailable.");
            else if (configurationErrorName.endsWith(QStringLiteral(".ApplyFailed")))
                result.error = QStringLiteral("Could not apply network settings. Reconnect to use the saved profile.");
            else
                result.error = QStringLiteral("Could not save network settings.");
        }
        if (reply && operation == Operation::SavedInfo)
            result.savedWifiInfo = parseSavedWifiInfo(reply);
        if (reply && operation == Operation::SaveIp) {
            gboolean applied = FALSE;
            g_variant_get(reply, "(b)", &applied);
            result.applied = applied;
            QString infoError;
            GVariant *info = call(bus, "GetSavedWifiInfo",
                                  g_variant_new("(s)", ssid.toUtf8().constData()),
                                  &infoError, 5000);
            if (info) {
                result.savedWifiInfo = parseSavedWifiInfo(info);
                g_variant_unref(info);
            }
        }
        if (reply)
            g_variant_unref(reply);
        if (operation == Operation::Scan &&
            (result.scanDeferred || result.error.contains(
                QStringLiteral("Scanning not allowed"), Qt::CaseInsensitive))) {
            result.scanDeferred = true;
            result.error.clear();
        }
    }

    QString stateError;
    GVariant *reply = call(bus, "GetState", nullptr, &stateError, 5000);
    if (reply) {
        result.serviceAvailable = true;
        GVariant *state = g_variant_get_child_value(reply, 0);
        gboolean value = FALSE;
        if (g_variant_lookup(state, "wifiDeviceAvailable", "b", &value))
            result.wifiDeviceAvailable = value;
        if (g_variant_lookup(state, "wifiEnabled", "b", &value))
            result.wifiEnabled = value;
        const gchar *text = nullptr;
        if (g_variant_lookup(state, "wifiState", "&s", &text))
            result.wifiState = QString::fromUtf8(text);
        if (g_variant_lookup(state, "wifiScanAvailable", "b", &value))
            result.wifiScanAvailable = value;
        else
            result.wifiScanAvailable = result.wifiDeviceAvailable && result.wifiEnabled
                && result.wifiState != QStringLiteral("Connecting");
        if (g_variant_lookup(state, "activeSsid", "&s", &text))
            result.activeSsid = QString::fromUtf8(text);
        if (g_variant_lookup(state, "bluetoothAdapterAvailable", "b", &value))
            result.bluetoothAdapterAvailable = value;
        if (g_variant_lookup(state, "bluetoothEnabled", "b", &value))
            result.bluetoothEnabled = value;
        if (g_variant_lookup(state, "bluetoothDiscovering", "b", &value))
            result.bluetoothDiscovering = value;
        g_variant_unref(state);
        g_variant_unref(reply);
    } else if (result.error.isEmpty()) {
        result.error = stateError;
    }

    if (result.serviceAvailable && result.wifiDeviceAvailable) {
        QString listError;
        reply = call(bus, "ListWifiNetworks", nullptr, &listError, 5000);
        if (reply) {
            GVariant *list = g_variant_get_child_value(reply, 0);
            GVariantIter entries;
            g_variant_iter_init(&entries, list);
            GVariant *entry;
            while ((entry = g_variant_iter_next_value(&entries))) {
                QVariantMap network;
                const gchar *text = nullptr;
                guint32 strength = 0;
                gboolean flag = FALSE;
                if (g_variant_lookup(entry, "ssid", "&s", &text))
                    network.insert(QStringLiteral("ssid"), QString::fromUtf8(text));
                if (g_variant_lookup(entry, "security", "&s", &text))
                    network.insert(QStringLiteral("security"), QString::fromUtf8(text));
                if (g_variant_lookup(entry, "strength", "u", &strength))
                    network.insert(QStringLiteral("strength"), strength);
                if (g_variant_lookup(entry, "connected", "b", &flag))
                    network.insert(QStringLiteral("connected"), bool(flag));
                if (g_variant_lookup(entry, "connecting", "b", &flag))
                    network.insert(QStringLiteral("connecting"), bool(flag));
                if (g_variant_lookup(entry, "saved", "b", &flag))
                    network.insert(QStringLiteral("saved"), bool(flag));
                if (!network.value(QStringLiteral("ssid")).toString().isEmpty())
                    result.networks.append(network);
                g_variant_unref(entry);
            }
            g_variant_unref(list);
            g_variant_unref(reply);
        } else if (result.error.isEmpty()) {
            result.error = listError;
        }
    }

    // Keep the active network visible while association or IP setup is in progress.
    if ((result.wifiState == QStringLiteral("Connected") ||
         result.wifiState == QStringLiteral("Connecting")) && !result.activeSsid.isEmpty()) {
        const bool connected = result.wifiState == QStringLiteral("Connected");
        bool found = false;
        for (QVariant &network : result.networks) {
            QVariantMap details = network.toMap();
            if (details.value(QStringLiteral("ssid")).toString() == result.activeSsid) {
                details.insert(QStringLiteral("connected"), connected);
                details.insert(QStringLiteral("connecting"), !connected);
                network = details;
                found = true;
            }
        }
        if (!found)
            result.networks.prepend(QVariantMap{
                {QStringLiteral("ssid"), result.activeSsid},
                {QStringLiteral("security"), QStringLiteral("Unknown")},
                {QStringLiteral("strength"), 0},
                {QStringLiteral("saved"), false},
                {QStringLiteral("connected"), connected},
                {QStringLiteral("connecting"), !connected}});
    }
    std::stable_sort(result.networks.begin(), result.networks.end(),
                     [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap();
        const auto right = b.toMap();
        if (left.value(QStringLiteral("connected")).toBool() !=
            right.value(QStringLiteral("connected")).toBool())
            return left.value(QStringLiteral("connected")).toBool();
        if (left.value(QStringLiteral("connecting")).toBool() !=
            right.value(QStringLiteral("connecting")).toBool())
            return left.value(QStringLiteral("connecting")).toBool();
        return left.value(QStringLiteral("strength")).toUInt() >
               right.value(QStringLiteral("strength")).toUInt();
    });
    QSet<QString> seen;
    QVariantList unique;
    for (const QVariant &network : result.networks) {
        const QString name = network.toMap().value(QStringLiteral("ssid")).toString();
        if (!seen.contains(name)) {
            seen.insert(name);
            unique.append(network);
        }
    }
    result.networks = unique;

    if (result.serviceAvailable && result.bluetoothAdapterAvailable) {
        QString listError;
        reply = call(bus, "ListBluetoothDevices", nullptr, &listError, 5000);
        if (reply) {
            GVariant *list = g_variant_get_child_value(reply, 0);
            GVariantIter entries;
            g_variant_iter_init(&entries, list);
            GVariant *entry;
            while ((entry = g_variant_iter_next_value(&entries))) {
                QVariantMap device;
                const gchar *text = nullptr;
                gboolean flag = FALSE;
                for (const char *key : {"path", "name", "alias", "address"}) {
                    if (g_variant_lookup(entry, key, key[0] == 'p' ? "&o" : "&s", &text))
                        device.insert(QString::fromUtf8(key), QString::fromUtf8(text));
                }
                for (const char *key : {"paired", "trusted", "connected"}) {
                    if (g_variant_lookup(entry, key, "b", &flag))
                        device.insert(QString::fromUtf8(key), bool(flag));
                }
                if (!device.value(QStringLiteral("path")).toString().isEmpty())
                    result.bluetoothDevices.append(device);
                g_variant_unref(entry);
            }
            g_variant_unref(list);
            g_variant_unref(reply);
            std::stable_sort(result.bluetoothDevices.begin(), result.bluetoothDevices.end(),
                             [](const QVariant &a, const QVariant &b) {
                const QVariantMap left = a.toMap(), right = b.toMap();
                if (left.value(QStringLiteral("connected")).toBool() !=
                    right.value(QStringLiteral("connected")).toBool())
                    return left.value(QStringLiteral("connected")).toBool();
                return left.value(QStringLiteral("name")).toString().localeAwareCompare(
                    right.value(QStringLiteral("name")).toString()) < 0;
            });
        } else if (result.error.isEmpty()) {
            result.error = listError;
        }
    }

    g_object_unref(bus);
    return result;
}
