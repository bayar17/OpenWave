#pragma once
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <OpenWave/ConnectivityBackend.h>
#include <memory>

inline void installWifiSettingsHarness(QObject *owner, ConnectivityBackend *backend)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=wifi")) return;
    auto stage = std::make_shared<int>(0);
    auto *timer = new QTimer(owner);
    QObject::connect(backend, &ConnectivityBackend::errorOccurred, owner,
        [stage, timer](const QString &error) {
            if (*stage == 4 && error.endsWith(QStringLiteral("SSID is not in the current scan"))) {
                qInfo() << "WIFI_SETTINGS_QA_PASS managed Settings can enable, scan and dispatch ConnectWifi";
                *stage = 5;
            } else {
                qCritical().noquote() << "WIFI_SETTINGS_QA_FAIL" << error;
                *stage = 99;
            }
            timer->stop();
        });
    QObject::connect(timer, &QTimer::timeout, owner, [backend, stage, timer] {
        if (*stage == 99 || *stage == 5) { timer->stop(); return; }
        if (!backend->serviceAvailable() || !backend->wifiDeviceAvailable() || backend->busy()) return;
        if (*stage == 0) {
            *stage = 1;
            backend->setWifiEnabled(true);
        } else if (*stage == 1 && backend->wifiEnabled() && backend->wifiScanAvailable()) {
            *stage = 2;
            backend->requestScan();
        } else if (*stage == 2 && !backend->networks().isEmpty()) {
            // Exercise the actual Settings backend and authenticated broker.
            // An absent SSID guarantees no saved profile or connection is used.
            const QString absent = QStringLiteral("ow-qa-absent-5c72839d21b64c8e");
            for (const auto &network : backend->networks()) {
                if (network.toMap().value(QStringLiteral("ssid")).toString() == absent) {
                    qCritical() << "WIFI_SETTINGS_QA_FAIL fixture SSID unexpectedly exists";
                    *stage = 99; timer->stop(); return;
                }
            }
            *stage = 4;
            backend->connectWifi(absent, QString());
        }
    });
    timer->start(150);
    QTimer::singleShot(30000, owner, [stage, timer] {
        if (*stage != 5 && *stage != 99) qCritical() << "WIFI_SETTINGS_QA_FAIL timeout stage=" << *stage;
        timer->stop();
    });
}
