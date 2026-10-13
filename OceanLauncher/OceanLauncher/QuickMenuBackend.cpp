#include "QuickMenuBackend.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QtGlobal>
#undef signals
#include <gio/gio.h>

namespace {
struct RadioState {
    bool wifiEnabled = false;
    bool wifiAvailable = false;
    bool wifiConnected = false;
    int wifiStrength = 0;
    bool cellularAvailable = false;
    int cellularStrength = 0;
    bool bluetoothEnabled = false;
    bool bluetoothAvailable = false;
    int volume = 40;
    bool audioRead = false;
    QString modemLabel = QStringLiteral("NO MODEM");
};

struct ModemInfo {
    QString label = QStringLiteral("NO MODEM");
    bool available = false;
    int strength = 0;
};

QString propertyString(GVariant *properties, const char *key)
{
    GVariant *value = g_variant_lookup_value(properties, key, G_VARIANT_TYPE_STRING);
    if (!value)
        return {};
    const QString result = QString::fromUtf8(g_variant_get_string(value, nullptr)).trimmed();
    g_variant_unref(value);
    return result;
}

ModemInfo modemInfo()
{
    ModemInfo result;
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        if (failure)
            g_error_free(failure);
        return result;
    }
    // ModemManager is optional in the current image. Do not activate it merely
    // to draw the menu, and never infer a cellular carrier from the Wi-Fi link.
    GVariant *reply = g_dbus_connection_call_sync(bus,
        "org.freedesktop.ModemManager1", "/org/freedesktop/ModemManager1",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
        1000, nullptr, &failure);
    g_object_unref(bus);
    if (!reply) {
        if (failure)
            g_error_free(failure);
        return result;
    }

    GVariant *objects = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);
    const gchar *path = nullptr;
    GVariant *interfaces = nullptr;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        GVariant *modem = g_variant_lookup_value(interfaces,
            "org.freedesktop.ModemManager1.Modem", G_VARIANT_TYPE("a{sv}"));
        if (modem) {
            result.available = true;
            GVariant *signal = g_variant_lookup_value(modem, "SignalQuality",
                                                      G_VARIANT_TYPE("(ub)"));
            if (signal) {
                guint32 quality = 0;
                gboolean recent = FALSE;
                g_variant_get(signal, "(ub)", &quality, &recent);
                result.strength = qBound(0, int(quality), 100);
                g_variant_unref(signal);
            }
            GVariant *simValue = g_variant_lookup_value(modem, "Sim", G_VARIANT_TYPE_OBJECT_PATH);
            const QString simPath = simValue
                ? QString::fromUtf8(g_variant_get_string(simValue, nullptr)) : QString();
            if (simValue)
                g_variant_unref(simValue);
            if (simPath.isEmpty() || simPath == QStringLiteral("/")) {
                result.label = QStringLiteral("NO SIM");
            } else {
                QString carrier;
                GVariant *threeGpp = g_variant_lookup_value(interfaces,
                    "org.freedesktop.ModemManager1.Modem.Modem3gpp", G_VARIANT_TYPE("a{sv}"));
                if (threeGpp) {
                    carrier = propertyString(threeGpp, "OperatorName");
                    g_variant_unref(threeGpp);
                }
                if (carrier.isEmpty()) {
                    GVariantIter simIter;
                    g_variant_iter_init(&simIter, objects);
                    const gchar *candidatePath = nullptr;
                    GVariant *simInterfaces = nullptr;
                    while (g_variant_iter_next(&simIter, "{&o@a{sa{sv}}}",
                                               &candidatePath, &simInterfaces)) {
                        if (simPath == QString::fromUtf8(candidatePath)) {
                            GVariant *sim = g_variant_lookup_value(simInterfaces,
                                "org.freedesktop.ModemManager1.Sim", G_VARIANT_TYPE("a{sv}"));
                            if (sim) {
                                carrier = propertyString(sim, "OperatorName");
                                g_variant_unref(sim);
                            }
                        }
                        g_variant_unref(simInterfaces);
                        if (simPath == QString::fromUtf8(candidatePath))
                            break;
                    }
                }
                QString number;
                GVariant *numbers = g_variant_lookup_value(modem, "OwnNumbers", G_VARIANT_TYPE("as"));
                if (numbers) {
                    GVariantIter numberIter;
                    g_variant_iter_init(&numberIter, numbers);
                    const gchar *entry = nullptr;
                    while (g_variant_iter_next(&numberIter, "&s", &entry)) {
                        number = QString::fromUtf8(entry).trimmed();
                        if (!number.isEmpty())
                            break;
                    }
                    g_variant_unref(numbers);
                }
                result.label = (carrier.isEmpty() ? QStringLiteral("Unknown carrier") : carrier)
                    + QStringLiteral(" - ")
                    + (number.isEmpty() ? QStringLiteral("Unknown number") : number);
            }
            g_variant_unref(modem);
            g_variant_unref(interfaces);
            break;
        }
        g_variant_unref(interfaces);
    }
    g_variant_unref(objects);
    g_variant_unref(reply);
    return result;
}

GVariant *call(const char *service, const char *path, const char *interface,
               const char *method, GVariant *parameters, QString *error)
{
    GError *failure = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &failure);
    if (!bus) {
        *error = QString::fromUtf8(failure->message);
        g_error_free(failure);
        if (parameters)
            g_variant_unref(g_variant_ref_sink(parameters));
        return nullptr;
    }
    GVariant *reply = g_dbus_connection_call_sync(bus, service, path, interface,
        method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &failure);
    g_object_unref(bus);
    if (failure) {
        *error = QString::fromUtf8(failure->message);
        g_error_free(failure);
    }
    return reply;
}

GVariant *connectivityCall(const char *method, GVariant *parameters, QString *error)
{
    return call("org.openwave.Connectivity", "/org/openwave/Connectivity",
                "org.openwave.Connectivity1", method, parameters, error);
}

GVariant *audioCall(const char *method, GVariant *parameters, QString *error)
{
    return call("org.openwave.Audio", "/org/openwave/Audio",
                "org.openwave.Audio1", method, parameters, error);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}
}

QuickMenuBackend::QuickMenuBackend(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pollTimer.setInterval(5000);
    connect(&m_pollTimer, &QTimer::timeout, this, &QuickMenuBackend::refresh);
    m_pollTimer.start();
    QTimer::singleShot(0, this, &QuickMenuBackend::refresh);
    m_volumeTimer.setSingleShot(true);
    m_volumeTimer.setInterval(70);
    connect(&m_volumeTimer, &QTimer::timeout, this, [this] {
        const int volume = m_volume;
        m_pool.start([this, volume] {
            QString error;
            GVariant *reply = audioCall("SetVolume", g_variant_new("(u)", guint32(volume)), &error);
            if (reply)
                g_variant_unref(reply);
            QMetaObject::invokeMethod(this, [this, error] {
                if (!error.isEmpty())
                    emit operationFailed(error);
            }, Qt::QueuedConnection);
        });
    });

    const QDir backlights(QStringLiteral("/sys/class/backlight"));
    for (const QString &name : backlights.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = backlights.filePath(name);
        bool valid = false;
        const int maximum = readFile(path + QStringLiteral("/max_brightness")).toInt(&valid);
        if (valid && maximum > 0) {
            m_backlightPath = path;
            m_maxBrightness = maximum;
            m_brightness = qBound(1, qRound(100.0 *
                readFile(path + QStringLiteral("/brightness")).toInt() / maximum), 100);
            break;
        }
    }
}

QuickMenuBackend::~QuickMenuBackend()
{
    m_pool.waitForDone();
}

void QuickMenuBackend::setPolling(bool enabled)
{
    // The Topbar needs signal updates even when the Quick Menu is closed.
    m_pollTimer.setInterval(enabled ? 2000 : 5000);
    if (enabled)
        refresh();
}

void QuickMenuBackend::refresh()
{
    if (m_refreshPending) {
        m_refreshAgain = true;
        return;
    }
    m_refreshPending = true;
    m_pool.start([this] {
        RadioState state;
        QString error;
        GVariant *reply = connectivityCall("GetState", nullptr, &error);
        if (reply) {
            GVariant *dictionary = g_variant_get_child_value(reply, 0);
            gboolean flag = FALSE;
            if (g_variant_lookup(dictionary, "wifiEnabled", "b", &flag))
                state.wifiEnabled = flag;
            if (g_variant_lookup(dictionary, "wifiDeviceAvailable", "b", &flag))
                state.wifiAvailable = flag;
            const gchar *wifiState = nullptr;
            if (g_variant_lookup(dictionary, "wifiState", "&s", &wifiState))
                state.wifiConnected = g_strcmp0(wifiState, "Connected") == 0;
            guint32 wifiStrength = 0;
            if (g_variant_lookup(dictionary, "wifiStrength", "u", &wifiStrength))
                state.wifiStrength = qBound(0, int(wifiStrength), 100);
            if (g_variant_lookup(dictionary, "bluetoothEnabled", "b", &flag))
                state.bluetoothEnabled = flag;
            if (g_variant_lookup(dictionary, "bluezAvailable", "b", &flag))
                state.bluetoothAvailable = flag;
            if (g_variant_lookup(dictionary, "bluetoothAdapterAvailable", "b", &flag))
                state.bluetoothAvailable = flag;
            g_variant_unref(dictionary);
            g_variant_unref(reply);
        }
        const ModemInfo modem = modemInfo();
        state.modemLabel = modem.label;
        state.cellularAvailable = modem.available;
        state.cellularStrength = modem.strength;
        QString audioError;
        reply = audioCall("GetState", nullptr, &audioError);
        if (reply) {
            GVariant *dictionary = g_variant_get_child_value(reply, 0);
            guint32 volume = 0;
            if (g_variant_lookup(dictionary, "Volume", "u", &volume)) {
                state.volume = qBound(0, int(volume), 100);
                state.audioRead = true;
            }
            g_variant_unref(dictionary);
            g_variant_unref(reply);
        }
        QMetaObject::invokeMethod(this, [this, state] {
            m_refreshPending = false;
            m_wifiEnabled = state.wifiEnabled;
            m_wifiAvailable = state.wifiAvailable;
            m_wifiConnected = state.wifiConnected;
            m_wifiStrength = state.wifiStrength;
            m_cellularAvailable = state.cellularAvailable;
            m_cellularStrength = state.cellularStrength;
            m_bluetoothEnabled = state.bluetoothEnabled;
            m_bluetoothAvailable = state.bluetoothAvailable;
            m_modemLabel = state.modemLabel;
            if (state.audioRead && !m_volumeTimer.isActive())
                m_volume = state.volume;
            emit changed();
            if (m_refreshAgain) {
                m_refreshAgain = false;
                refresh();
            }
        }, Qt::QueuedConnection);
    });
}

void QuickMenuBackend::setRadio(const char *method, bool enabled)
{
    const QByteArray name(method);
    m_pool.start([this, name, enabled] {
        QString error;
        GVariant *reply = connectivityCall(name.constData(),
            g_variant_new("(b)", gboolean(enabled)), &error);
        if (reply)
            g_variant_unref(reply);
        QMetaObject::invokeMethod(this, [this, error] {
            if (!error.isEmpty())
                emit operationFailed(error);
            QTimer::singleShot(300, this, &QuickMenuBackend::refresh);
        }, Qt::QueuedConnection);
    });
}

void QuickMenuBackend::setWifiEnabled(bool enabled)
{
    if (!m_wifiAvailable)
        return;
    if (enabled)
        m_airplaneMode = false;
    m_wifiEnabled = enabled;
    emit changed();
    setRadio("SetWifiEnabled", enabled);
}

void QuickMenuBackend::setBluetoothEnabled(bool enabled)
{
    if (!m_bluetoothAvailable)
        return;
    if (enabled)
        m_airplaneMode = false;
    m_bluetoothEnabled = enabled;
    emit changed();
    setRadio("SetBluetoothEnabled", enabled);
}

void QuickMenuBackend::setAirplaneMode(bool enabled)
{
    if (m_airplaneMode == enabled)
        return;
    if (enabled) {
        m_restoreWifi = m_wifiEnabled;
        m_restoreBluetooth = m_bluetoothEnabled;
    }
    m_airplaneMode = enabled;
    if (m_wifiAvailable)
        setWifiEnabled(enabled ? false : m_restoreWifi);
    if (m_bluetoothAvailable)
        setBluetoothEnabled(enabled ? false : m_restoreBluetooth);
    emit changed();
}

void QuickMenuBackend::setVolume(int volume)
{
    volume = qBound(0, volume, 100);
    if (m_volume == volume)
        return;
    m_volume = volume;
    emit changed();
    if (!m_volumeTimer.isActive())
        m_volumeTimer.start();
}

void QuickMenuBackend::setBrightness(int brightness)
{
    brightness = qBound(1, brightness, 100);
    if (m_brightness == brightness)
        return;
    m_brightness = brightness;
    if (hardwareBrightness()) {
        QFile file(m_backlightPath + QStringLiteral("/brightness"));
        if (file.open(QIODevice::WriteOnly))
            file.write(QByteArray::number(qMax(1, m_maxBrightness * brightness / 100)));
        else
            emit operationFailed(QStringLiteral("Could not set display brightness."));
    } else {
        m_softwareBrightnessActive = true;
    }
    emit changed();
}
