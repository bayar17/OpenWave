#include "../../SettingsBackend.h"
#include "../../PreferenceBroker.h"
#include "../../../../userspace/preferences.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
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
    int homeRequests = 0;
    QObject::connect(&again, &SettingsBackend::homeRequested, [&] { ++homeRequests; });
    again.closeApp(false);
    again.closeApp(true);
    CHECK(homeRequests == 2);
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
    {
        const QString brokeredPath = temporary.filePath(QStringLiteral("brokered.ini"));
        SettingsBackend brokered(brokeredPath);
        QList<QPair<QString, QVariant>> received;
        bool accept = true;
        brokered.setPreferenceWriter([&](const QString &key, const QVariant &value) {
            received.append({key, value});
            return accept;
        });
        int changes = 0, errors = 0;
        QObject::connect(&brokered, &SettingsBackend::preferencesChanged, [&] { ++changes; });
        QObject::connect(&brokered, &SettingsBackend::errorOccurred, [&] { ++errors; });
        CHECK(brokered.setReduceMotion(true));
        CHECK(received.size() == 1);
        CHECK(received.last().first == QStringLiteral("accessibility/reduceMotion"));
        CHECK(received.last().second.metaType().id() == QMetaType::Bool && received.last().second.toBool());
        CHECK(brokered.saveDeviceName(QStringLiteral("  Brokered  ")));
        CHECK(received.last().first == QStringLiteral("device/name"));
        CHECK(received.last().second.toString() == QStringLiteral("Brokered"));
        CHECK(changes == 2 && errors == 0);
        CHECK(!QFile::exists(brokeredPath));
        accept = false;
        CHECK(!brokered.setLargeText(true));
        CHECK(errors == 1 && changes == 2);
        CHECK(!brokered.saveDeviceName(QStringLiteral("   ")));
        CHECK(received.size() == 3);
    }
    {
        QList<QPair<QString, QVariantList>> calls;
        PreferenceBroker::Completion pending;
        enum Mode { Immediate, Deferred, Failing, Silent } mode = Immediate;
        PreferenceBroker broker([&](const QString &method, const QVariantList &arguments, PreferenceBroker::Completion done) {
            calls.append({method, arguments});
            switch (mode) {
            case Immediate: done({}, QString()); break;
            case Deferred: QTimer::singleShot(20, [done] { done({}, QString()); }); break;
            case Failing: done({}, QStringLiteral("Only the verified Settings application may change preferences")); break;
            case Silent: pending = done; break;
            }
        }, 300);
        CHECK(broker.write(QStringLiteral("accessibility/reduceMotion"), true));
        CHECK(calls.last().first == QStringLiteral("SetPreferenceBool"));
        CHECK(calls.last().second == QVariantList({QStringLiteral("accessibility/reduceMotion"), true}));
        CHECK(broker.write(QStringLiteral("device/name"), QStringLiteral("Phone")));
        CHECK(calls.last().first == QStringLiteral("SetPreferenceString"));
        CHECK(broker.write(QStringLiteral("quickmenu/applets/order"), QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        CHECK(calls.last().first == QStringLiteral("SetPreferenceList"));
        CHECK(calls.last().second.at(1).toStringList() == QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        CHECK(broker.write(QStringLiteral("quickmenu/applets"), QVariant()));
        CHECK(calls.last().first == QStringLiteral("ResetPreferenceGroup"));
        CHECK(calls.last().second == QVariantList({QStringLiteral("quickmenu/applets")}));
        const int before = calls.size();
        CHECK(!broker.write(QStringLiteral("x/y"), 3));
        CHECK(calls.size() == before && !broker.lastError().isEmpty());
        mode = Deferred;
        CHECK(broker.write(QStringLiteral("accessibility/largeText"), false));
        mode = Failing;
        CHECK(!broker.write(QStringLiteral("accessibility/largeText"), true));
        CHECK(broker.lastError().contains(QStringLiteral("verified Settings")));
        mode = Silent;
        CHECK(!broker.write(QStringLiteral("accessibility/largeText"), true));
        CHECK(broker.lastError().contains(QStringLiteral("in time")));
        if (pending) pending({}, QString());
        mode = Immediate;
        CHECK(broker.write(QStringLiteral("accessibility/largeText"), true));
        PreferenceBroker idle(nullptr);
        CHECK(!idle.write(QStringLiteral("accessibility/largeText"), true));
        bool reentered = true;
        PreferenceBroker *outer = nullptr;
        PreferenceBroker nested([&](const QString &, const QVariantList &, PreferenceBroker::Completion done) {
            reentered = outer->write(QStringLiteral("accessibility/largeText"), true);
            done({}, QString());
        }, 300);
        outer = &nested;
        CHECK(nested.write(QStringLiteral("accessibility/largeText"), true));
        CHECK(!reentered);
    }
    std::puts("Settings persistence, validation, system information and save failure checks passed.");
}
