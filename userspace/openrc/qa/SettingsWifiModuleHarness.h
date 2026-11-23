#pragma once
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace wifimoduleqa {

inline QQuickItem *search(QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (QQuickItem *child : parent->childItems()) {
        if (QQuickItem *found = search(child, name))
            return found;
    }
    return nullptr;
}

}

inline void installWifiModuleHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=wifi-module")) return;
    struct State {
        int stage = 0;
        int wifiTab = -1;
        int bluetoothTab = -1;
        int generalTab = -1;
        int ticks = 0;
        int hold = 0;
    };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    auto fail = [state, timer](const QString &reason) {
        qCritical().noquote() << "WIFI_MODULE_QA_FAIL" << reason;
        state->stage = 99;
        timer->stop();
    };
    QObject::connect(timer, &QTimer::timeout, owner, [engine, state, timer, fail] {
        if (state->stage >= 7) { timer->stop(); return; }
        if (++state->ticks > 600) { fail(QStringLiteral("timeout stage=%1").arg(state->stage)); return; }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = qobject_cast<QQuickItem *>(engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsPage")));
        if (!root || !root->isVisible()) return;
        auto *host = wifimoduleqa::search(root, QStringLiteral("settingsModuleHost"));
        if (!host) return;
        const bool loaded = host->property("loaded").toBool();
        auto select = [root](int index) {
            QMetaObject::invokeMethod(root, "selectTab", Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(false)));
        };
        if (state->stage == 0) {
            const QStringList tabs = root->property("tabs").toStringList();
            const QVariantList entries = root->property("tabEntries").toList();
            int count = 0;
            bool fromModule = false;
            for (int index = 0; index < entries.size(); ++index) {
                const QVariantMap entry = entries.at(index).toMap();
                if (entry.value(QStringLiteral("title")).toString().toLower().remove(QLatin1Char('-')) == QLatin1String("wifi")) {
                    state->wifiTab = index;
                    fromModule = !entry.value(QStringLiteral("moduleId")).toString().isEmpty();
                    ++count;
                }
            }
            int bluetoothCount = 0;
            for (int index = 0; index < entries.size(); ++index) {
                const QVariantMap entry = entries.at(index).toMap();
                if (entry.value(QStringLiteral("moduleId")).toString() == QLatin1String("com.openwave.settings.bluetooth")) {
                    state->bluetoothTab = index;
                    ++bluetoothCount;
                }
            }
            qInfo().noquote() << "WIFI_MODULE_QA TABS" << tabs.join(QLatin1Char(',')) << "wifiIndex=" << state->wifiTab << "wifiEntries=" << count
                << "bluetoothIndex=" << state->bluetoothTab << "bluetoothEntries=" << bluetoothCount;
            if (bluetoothCount != 1 || state->bluetoothTab != 1) { fail(QStringLiteral("Bluetooth is not the single second registry tab")); return; }
            int generalCount = 0;
            for (int index = 0; index < entries.size(); ++index) {
                if (entries.at(index).toMap().value(QStringLiteral("moduleId")).toString() == QLatin1String("com.openwave.settings.general")) {
                    state->generalTab = index;
                    ++generalCount;
                }
            }
            if (generalCount != 1 || state->generalTab != 2) { fail(QStringLiteral("General is not the single third registry tab")); return; }
            for (const QString &title : tabs) {
                if (title.toLower() == QLatin1String("general") && !entries.at(state->generalTab).toMap().value(QStringLiteral("moduleId")).toString().startsWith(QLatin1String("com.openwave"))) {
                    fail(QStringLiteral("a built-in General tab still exists")); return;
                }
            }
            if (count != 1) { fail(QStringLiteral("expected exactly one Wi-Fi entry, found %1").arg(count)); return; }
            if (!fromModule) { fail(QStringLiteral("Wi-Fi is not a registry module tab")); return; }
            if (state->wifiTab != 0) { fail(QStringLiteral("Wi-Fi is not the first tab")); return; }
            select(state->wifiTab);
            state->stage = 1;
            state->ticks = 0;
        } else if (state->stage == 1) {
            if (!loaded) {
                const QString error = host->property("errorString").toString();
                if (!error.isEmpty()) fail(QStringLiteral("module failed to load: ") + error);
                return;
            }
            auto *page = wifimoduleqa::search(host, QStringLiteral("wifiModule"));
            auto *toggle = wifimoduleqa::search(host, QStringLiteral("wifiSwitch"));
            auto *status = wifimoduleqa::search(host, QStringLiteral("wifiStatusText"));
            if (!page || !toggle || !status) return;
            auto *backend = root->property("connectivityBackend").value<QObject *>();
            qInfo().noquote() << "WIFI_MODULE_QA STATE loaded id=" << host->property("loader").value<QObject *>()->property("activeModuleId").toString()
                << "status=" << status->property("text").toString()
                << "switchEnabled=" << toggle->isEnabled() << "switchChecked=" << toggle->property("checked").toBool()
                << "backendService=" << (backend ? backend->property("serviceAvailable").toBool() : false)
                << "adapter=" << (backend ? backend->property("wifiDeviceAvailable").toBool() : false);
            state->stage = 2;
            state->hold = 0;
        } else if (state->stage == 2) {
            if (++state->hold < 100) return;
            if (auto *backend = root->property("connectivityBackend").value<QObject *>()) {
                QStringList names;
                int shown = 0;
                const QVariantList networks = backend->property("networks").toList();
                for (const QVariant &entry : networks) {
                    if (shown++ < 5) names << entry.toMap().value(QStringLiteral("ssid")).toString();
                }
                auto *page = wifimoduleqa::search(host, QStringLiteral("wifiModule"));
                qInfo().noquote() << "WIFI_MODULE_QA STATE later adapter=" << backend->property("wifiDeviceAvailable").toBool()
                    << "enabled=" << backend->property("wifiEnabled").toBool()
                    << "status=" << backend->property("wifiStatus").toString()
                    << "networks=" << networks.size() << "first=" << names.join(QLatin1Char(','))
                    << "scanActive=" << (page ? page->property("scanActive").toBool() : false);
            }
            select(root->property("accessibilityTab").toInt());
            state->stage = 3;
            state->ticks = 0;
        } else if (state->stage == 3) {
            if (loaded || wifimoduleqa::search(root, QStringLiteral("wifiModule"))) return;
            qInfo().noquote() << "WIFI_MODULE_QA STATE unloaded";
            select(state->wifiTab);
            state->stage = 4;
            state->ticks = 0;
        } else if (state->stage == 4) {
            if (!loaded || !wifimoduleqa::search(host, QStringLiteral("wifiModule"))) return;
            qInfo().noquote() << "WIFI_MODULE_QA PASS Wi-Fi registry module loaded, unloaded and reloaded";
            select(state->bluetoothTab);
            state->stage = 5;
            state->ticks = 0;
        } else if (state->stage == 5) {
            auto *page = wifimoduleqa::search(host, QStringLiteral("bluetoothModule"));
            auto *toggle = wifimoduleqa::search(host, QStringLiteral("bluetoothSwitch"));
            auto *status = wifimoduleqa::search(host, QStringLiteral("bluetoothStatusText"));
            if (!loaded || !page || !toggle) return;
            auto *backend = root->property("connectivityBackend").value<QObject *>();
            qInfo().noquote() << "WIFI_MODULE_QA STATE bluetooth id=" << host->property("loader").value<QObject *>()->property("activeModuleId").toString()
                << "status=" << (status ? status->property("text").toString() : QString())
                << "switchEnabled=" << toggle->isEnabled()
                << "adapter=" << (backend ? backend->property("bluetoothAdapterAvailable").toBool() : false);
            qInfo().noquote() << "WIFI_MODULE_QA PASS Bluetooth registry module loaded as the second tab";
            select(state->generalTab);
            state->stage = 6;
            state->ticks = 0;
        } else if (state->stage == 6) {
            auto *page = wifimoduleqa::search(host, QStringLiteral("generalModule"));
            auto *about = wifimoduleqa::search(host, QStringLiteral("generalCategory_about"));
            if (!loaded || !page || !about) return;
            const QVariantList categories = page->property("categories").toList();
            QStringList titles;
            for (const QVariant &entry : categories)
                titles << entry.toMap().value(QStringLiteral("title")).toString();
            qInfo().noquote() << "WIFI_MODULE_QA STATE general id=" << host->property("loader").value<QObject *>()->property("activeModuleId").toString()
                << "categories=" << titles.join(QLatin1Char(','));
            if (titles.contains(QStringLiteral("Appearance"))) { fail(QStringLiteral("General still lists Appearance")); return; }
            QMetaObject::invokeMethod(page, "openCategory", Q_ARG(QVariant, QVariant(QStringLiteral("about"))));
            auto *kernel = wifimoduleqa::search(host, QStringLiteral("generalSystem_kernel"));
            auto *name = wifimoduleqa::search(host, QStringLiteral("generalDeviceName"));
            qInfo().noquote() << "WIFI_MODULE_QA STATE general about device=" << (name ? name->property("detail").toString() : QString())
                << "kernel=" << (kernel ? kernel->property("detail").toString() : QString());
            qInfo().noquote() << "WIFI_MODULE_QA PASS General registry module loaded as the third tab";
            state->stage = 7;
            timer->stop();
            QTimer::singleShot(25000, qApp, &QCoreApplication::quit);
        }
    });
    timer->start(150);
}
