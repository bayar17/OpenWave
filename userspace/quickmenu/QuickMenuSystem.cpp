#include "QuickMenuSystem.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QVariant>

namespace {

QByteArray readTrimmed(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(64).trimmed() : QByteArray();
}

}

QuickMenuSystem::QuickMenuSystem(QObject *parent)
    : QObject(parent)
    , m_powerSupplyRoot(QStringLiteral("/sys/class/power_supply"))
{
}

QuickMenuSystem *QuickMenuSystem::shared()
{
    static QuickMenuSystem *instance = new QuickMenuSystem(QCoreApplication::instance());
    return instance;
}

QuickMenuSystem *QuickMenuSystem::create(QQmlEngine *, QJSEngine *)
{
    QuickMenuSystem *instance = shared();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

void QuickMenuSystem::attach(QObject *backend)
{
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        const QMetaObject *meta = m_backend->metaObject();
        if (meta->indexOfSignal("changed()") >= 0)
            connect(m_backend, SIGNAL(changed()), this, SIGNAL(changed()));
        if (meta->indexOfSignal("operationFailed(QString)") >= 0)
            connect(m_backend, SIGNAL(operationFailed(QString)), this, SIGNAL(operationFailed(QString)));
    }
    refreshBattery();
    emit changed();
}

void QuickMenuSystem::setPowerSupplyRoot(const QString &root)
{
    m_powerSupplyRoot = root;
    refreshBattery();
}

bool QuickMenuSystem::flag(const char *name) const
{
    return m_backend && m_backend->property(name).toBool();
}

int QuickMenuSystem::number(const char *name) const
{
    return m_backend ? m_backend->property(name).toInt() : 0;
}

bool QuickMenuSystem::connected() const { return !m_backend.isNull(); }
bool QuickMenuSystem::wifiAvailable() const { return flag("wifiAvailable"); }
bool QuickMenuSystem::wifiEnabled() const { return flag("wifiEnabled"); }
bool QuickMenuSystem::wifiConnected() const { return flag("wifiConnected"); }
int QuickMenuSystem::wifiStrength() const { return number("wifiStrength"); }
bool QuickMenuSystem::bluetoothAvailable() const { return flag("bluetoothAvailable"); }
bool QuickMenuSystem::bluetoothEnabled() const { return flag("bluetoothEnabled"); }
bool QuickMenuSystem::airplaneMode() const { return flag("airplaneMode"); }
bool QuickMenuSystem::cellularAvailable() const { return flag("cellularAvailable"); }
int QuickMenuSystem::cellularStrength() const { return number("cellularStrength"); }
QString QuickMenuSystem::modemLabel() const
{
    return m_backend ? m_backend->property("modemLabel").toString() : QString();
}
bool QuickMenuSystem::mobileDataSupported() const { return false; }
bool QuickMenuSystem::batteryAvailable() const { return m_batteryAvailable; }
int QuickMenuSystem::batteryPercent() const { return m_batteryPercent; }
bool QuickMenuSystem::batteryCharging() const { return m_batteryCharging; }
int QuickMenuSystem::volume() const { return number("volume"); }
int QuickMenuSystem::brightness() const { return number("brightness"); }

void QuickMenuSystem::fail(const QString &message)
{
    emit operationFailed(message);
}

bool QuickMenuSystem::invoke(const char *method, bool value)
{
    if (!m_backend) {
        fail(tr("The system service is unavailable."));
        return false;
    }
    if (!QMetaObject::invokeMethod(m_backend, method, Qt::DirectConnection, Q_ARG(bool, value))) {
        fail(tr("The system service does not support this action."));
        return false;
    }
    return true;
}

bool QuickMenuSystem::invokeNumber(const char *method, int value)
{
    if (!m_backend) {
        fail(tr("The system service is unavailable."));
        return false;
    }
    if (!QMetaObject::invokeMethod(m_backend, method, Qt::DirectConnection, Q_ARG(int, value))) {
        fail(tr("The system service does not support this action."));
        return false;
    }
    return true;
}

void QuickMenuSystem::setWifiEnabled(bool enabled)
{
    if (m_backend && !wifiAvailable()) {
        fail(tr("No Wi-Fi adapter is available."));
        return;
    }
    invoke("setWifiEnabled", enabled);
}

void QuickMenuSystem::setBluetoothEnabled(bool enabled)
{
    if (m_backend && !bluetoothAvailable()) {
        fail(tr("No Bluetooth adapter is available."));
        return;
    }
    invoke("setBluetoothEnabled", enabled);
}

void QuickMenuSystem::setAirplaneMode(bool enabled)
{
    invoke("setAirplaneMode", enabled);
}

void QuickMenuSystem::setMobileDataEnabled(bool)
{
    fail(tr("Mobile data cannot be controlled on this system."));
}

void QuickMenuSystem::setVolume(int volume)
{
    invokeNumber("setVolume", qBound(0, volume, 100));
}

void QuickMenuSystem::setBrightness(int brightness)
{
    invokeNumber("setBrightness", qBound(1, brightness, 100));
}

void QuickMenuSystem::lock()
{
    emit lockRequested();
}

void QuickMenuSystem::refreshBattery()
{
    bool available = false;
    int percent = 0;
    bool charging = false;
    const QDir root(m_powerSupplyRoot);
    const QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString base = root.filePath(name);
        if (readTrimmed(base + QStringLiteral("/type")) != "Battery")
            continue;
        const QByteArray present = readTrimmed(base + QStringLiteral("/present"));
        if (!present.isEmpty() && present != "1")
            continue;
        bool valid = false;
        const int capacity = readTrimmed(base + QStringLiteral("/capacity")).toInt(&valid);
        if (!valid || capacity < 0 || capacity > 100)
            continue;
        available = true;
        percent = capacity;
        charging = readTrimmed(base + QStringLiteral("/status")) == "Charging";
        break;
    }
    if (available == m_batteryAvailable && percent == m_batteryPercent && charging == m_batteryCharging)
        return;
    m_batteryAvailable = available;
    m_batteryPercent = percent;
    m_batteryCharging = charging;
    emit batteryChanged();
}
