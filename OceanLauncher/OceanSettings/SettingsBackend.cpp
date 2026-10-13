#include "SettingsBackend.h"
#include <QCoreApplication>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
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
    QCoreApplication::quit();
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
        // QSettings atomically replaces the file. Commit the directory entry
        // too, so the most recent preference survives an immediate restart.
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
