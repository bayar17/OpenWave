#include "../../SettingsBackend.h"
#include "../../../../userspace/preferences.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("settings.ini"));
    {
        SettingsBackend settings(path);
        CHECK(settings.deviceName() == QStringLiteral("KB Phone"));
        CHECK(settings.localHostname() == QStringLiteral("kbphone.local"));
        CHECK(!settings.reduceMotion());
        CHECK(!settings.largeText());
        CHECK(settings.saveDeviceName(QStringLiteral("  Ocean Phone  ")));
        CHECK(settings.setReduceMotion(true));
        CHECK(settings.setLargeText(true));
        CHECK(!settings.saveDeviceName(QStringLiteral("   ")));
        CHECK(!settings.saveDeviceName(QString(65, QChar('x'))));
        CHECK(!settings.saveDeviceName(QStringLiteral("Bad\nName")));
        CHECK(settings.deviceName() == QStringLiteral("Ocean Phone"));
        CHECK(settings.localHostname() == QStringLiteral("oceanphone.local"));
        CHECK(settings.saveDeviceName(QStringLiteral("KB's Phone")));
        CHECK(settings.localHostname() == QStringLiteral("kbsphone.local"));
        CHECK(settings.saveDeviceName(QStringLiteral("Ocean Phone")));
        CHECK(!settings.bluetoothStatus().isEmpty());
        CHECK(!settings.wifiStatus().isEmpty());
        CHECK(!settings.systemInfo().value(QStringLiteral("kernel")).toString().isEmpty());
        CHECK(settings.systemInfo().value(QStringLiteral("storageTotal")).toLongLong() > 0);
        CHECK(!settings.requestPowerAction(QStringLiteral("invalid-action")));
        if (!settings.powerActionsAvailable()) {
            CHECK(!settings.requestPowerAction(QStringLiteral("restart")));
            CHECK(!settings.requestPowerAction(QStringLiteral("shutdown")));
        }
    }
    {
        SettingsBackend reloaded(path);
        CHECK(reloaded.deviceName() == QStringLiteral("Ocean Phone"));
        CHECK(reloaded.reduceMotion());
        CHECK(reloaded.largeText());
        CHECK(reloaded.setReduceMotion(false));
        CHECK(reloaded.saveDeviceName(QString::fromUtf8("Çağrı’s Phone")));
    }
    SettingsBackend again(path);
    CHECK(!again.reduceMotion());
    CHECK(again.largeText());
    CHECK(again.deviceName() == QString::fromUtf8("Çağrı’s Phone"));
    CHECK(again.localHostname() == QStringLiteral("cagrsphone.local"));

    QFile blocker(temporary.filePath(QStringLiteral("not-a-directory")));
    CHECK(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    SettingsBackend unavailable(blocker.fileName() + QStringLiteral("/settings.ini"));
    bool notified = false;
    QObject::connect(&unavailable, &SettingsBackend::errorOccurred, [&notified] { notified = true; });
    CHECK(!unavailable.saveDeviceName(QStringLiteral("Unsaved name")));
    CHECK(notified);
    CHECK(unavailable.deviceName() == QStringLiteral("KB Phone"));

    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    const QString legacyDirectory = temporary.path() + QStringLiteral("/OpenWave/Ocean Launcher");
    CHECK(QDir().mkpath(legacyDirectory));
    QSettings legacy(legacyDirectory + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    legacy.setValue(QStringLiteral("device/name"), QStringLiteral("Existing Phone"));
    legacy.sync();
    const QString migratedPath = openwavePreferencesPath();
    CHECK(migratedPath == temporary.path() + QStringLiteral("/OpenWave/settings.ini"));
    SettingsBackend migrated(migratedPath);
    CHECK(migrated.deviceName() == QStringLiteral("Existing Phone"));
    CHECK(migrated.saveDeviceName(QStringLiteral("Updated Phone")));
    CHECK(openwavePreferencesPath() == migratedPath);
    SettingsBackend preserved(migratedPath);
    CHECK(preserved.deviceName() == QStringLiteral("Updated Phone"));
    std::puts("Settings persistence, validation, system information and save failure checks passed.");
}
