#include "SettingsBackend.h"
#include <QCoreApplication>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#if defined(__OPENWAVE__)
#undef signals
#include <OpenWave/OpenWaveIdentity.h>
#endif
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif

SettingsBackend::SettingsBackend(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settings(settingsPath, QSettings::IniFormat)
{
    const QStorageInfo storage(QStringLiteral("/"));
    m_systemInfo = {
        {QStringLiteral("os"), QStringLiteral("OpenWave")},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("kernel"), QSysInfo::kernelVersion()},
        {QStringLiteral("processors"), QThread::idealThreadCount()},
        {QStringLiteral("storageTotal"), storage.bytesTotal()},
        {QStringLiteral("storageAvailable"), storage.bytesAvailable()}
    };
    refreshRadios();
#if defined(__OPENWAVE__)
    m_powerPool.setMaxThreadCount(1);
    m_powerTimer.setInterval(2000);
    connect(&m_powerTimer, &QTimer::timeout, this, &SettingsBackend::refreshPowerService);
    m_powerTimer.start();
    refreshPowerService();
#endif
}

QString SettingsBackend::deviceName() const
{
    return m_settings.value(QStringLiteral("device/name"), QStringLiteral("KB Phone")).toString();
}

QString SettingsBackend::localHostname() const
{
    const QString normalized = deviceName().normalized(QString::NormalizationForm_KD);
    QString label;
    for (const QChar character : normalized) {
        if (character.unicode() < 128 && character.isLetterOrNumber())
            label += character.toLower();
        if (label.size() == 63)
            break;
    }
    return (label.isEmpty() ? QStringLiteral("openwave") : label) + QStringLiteral(".local");
}

void SettingsBackend::closeApp(bool home)
{
    Q_UNUSED(home);
    emit homeRequested();
}

void SettingsBackend::refreshPowerService()
{
#if defined(__OPENWAVE__)
    if (m_powerChecking || m_powerBusy) return;
    m_powerChecking = true;
    m_powerPool.start([this] {
        auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
        char *owner = bus ? openwave_service_owner(bus, "org.openwave.Power", "/run/openwave/powerd.pid", nullptr) : nullptr;
        const bool available = owner != nullptr;
        g_free(owner); if (bus) g_object_unref(bus);
        QMetaObject::invokeMethod(this, [this, available] {
            m_powerChecking = false;
            if (m_powerAvailable != available) { m_powerAvailable = available; emit powerStateChanged(); }
        }, Qt::QueuedConnection);
    });
#endif
}

bool SettingsBackend::requestPowerAction(const QString &action)
{
    if (action != QStringLiteral("restart") && action != QStringLiteral("shutdown")) {
        emit errorOccurred(tr("Unknown power action."));
        return false;
    }
    if (!powerActionsAvailable()) {
        emit errorOccurred(tr("Power controls are unavailable in this session."));
        return false;
    }
#if defined(__OPENWAVE__)
    m_powerBusy = true; emit powerStateChanged();
    m_powerPool.start([this, action] {
        GError *error = nullptr;
        auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        char *owner = bus ? openwave_service_owner(bus, "org.openwave.Power", "/run/openwave/powerd.pid", &error) : nullptr;
        auto *reply = owner ? g_dbus_connection_call_sync(bus, owner, "/org/openwave/Power", "org.openwave.Power1",
            action == QStringLiteral("restart") ? "Reboot" : "PowerOff", nullptr, G_VARIANT_TYPE("()"),
            G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error) : nullptr;
        const QString failure = reply ? QString() : error ? QString::fromUtf8(error->message) : tr("The power service is unavailable.");
        if (reply) g_variant_unref(reply);
        g_free(owner); g_clear_error(&error); if (bus) g_object_unref(bus);
        QMetaObject::invokeMethod(this, [this, failure] {
            if (!failure.isEmpty()) {
                m_powerBusy = false; m_powerAvailable = false; emit powerStateChanged();
                emit errorOccurred(tr("Could not request the power action: %1").arg(failure));
            }
        }, Qt::QueuedConnection);
    });
    return true;
#endif
    emit errorOccurred(tr("Could not request the power action. Please try again."));
    return false;
}

bool SettingsBackend::reduceMotion() const
{
    return m_settings.value(QStringLiteral("accessibility/reduceMotion"), false).toBool();
}

bool SettingsBackend::largeText() const
{
    return m_settings.value(QStringLiteral("accessibility/largeText"), false).toBool();
}

bool SettingsBackend::save(const QString &key, const QVariant &value)
{
    // Keep the visible preference unchanged if persistence fails.
    QSettings update(m_settings.fileName(), QSettings::IniFormat);
    const QVariant previous = update.value(key);
    update.setValue(key, value);
    update.sync();
    bool saved = update.status() == QSettings::NoError;
#ifdef Q_OS_LINUX
    if (saved) {
        const QByteArray directory = QFile::encodeName(QFileInfo(update.fileName()).absolutePath());
        const int fd = ::open(directory.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        saved = fd >= 0 && ::fsync(fd) == 0;
        if (fd >= 0) ::close(fd);
    }
#endif
    if (!saved) {
        if (previous.isValid())
            update.setValue(key, previous);
        else
            update.remove(key);
        emit errorOccurred(QStringLiteral("Could not save this setting. Please try again."));
        return false;
    }
    m_settings.sync();
    emit preferencesChanged();
    return true;
}

bool SettingsBackend::saveDeviceName(const QString &name)
{
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty() || cleaned.size() > 64) {
        emit errorOccurred(QStringLiteral("Use a device name between 1 and 64 characters."));
        return false;
    }
    for (QChar c : cleaned) {
        if (!c.isPrint()) {
            emit errorOccurred(QStringLiteral("The device name cannot contain control characters."));
            return false;
        }
    }
    return save(QStringLiteral("device/name"), cleaned);
}

bool SettingsBackend::setReduceMotion(bool enabled)
{
    return save(QStringLiteral("accessibility/reduceMotion"), enabled);
}

bool SettingsBackend::setLargeText(bool enabled)
{
    return save(QStringLiteral("accessibility/largeText"), enabled);
}

void SettingsBackend::refreshRadios()
{
    const bool bluetooth = !QDir(QStringLiteral("/sys/class/bluetooth"))
        .entryList({QStringLiteral("hci*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    bool wireless = false;
    const QDir network(QStringLiteral("/sys/class/net"));
    for (const QString &name : network.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        wireless |= QFile::exists(network.filePath(name + QStringLiteral("/wireless")))
            || QFile::exists(network.filePath(name + QStringLiteral("/phy80211")));

    // This fallback is used when the connectivity service client is absent.
    m_bluetoothStatus = bluetooth
        ? QStringLiteral("Bluetooth connections are not supported in this build.")
        : QStringLiteral("No Bluetooth adapter found.");
    m_wifiStatus = wireless
        ? QStringLiteral("Wi-Fi adapter detected. Waiting for connectivity service.")
        : QStringLiteral("No Wi-Fi adapter found.");
    emit radiosChanged();
}
