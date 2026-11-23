#pragma once
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <memory>
#include "AppController.h"

inline void installUninstallTimingHarness(QObject *owner, AppController *controller)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=waydroid")) return;
    struct State {
        bool started = false;
        QElapsedTimer clock;
    };
    auto state = std::make_shared<State>();
    QObject::connect(controller, &AppController::showExistingApp, owner, [state](const QString &id) {
        if (state->started)
            qInfo().noquote() << "UNINSTALL_TIMING activation id=" << id << "ms=" << state->clock.elapsed();
    });
    QObject::connect(controller, &AppController::uninstallFinished, owner,
        [state](const QString &id, const QString &error) {
            if (state->started)
                qInfo().noquote() << "UNINSTALL_TIMING finished id=" << id << "error=" << error << "ms=" << state->clock.elapsed();
        });
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [controller, state, timer] {
        if (state->started) { timer->stop(); return; }
        QFile request(QStringLiteral("/run/openwave/uninstall-timing-app"));
        if (!request.open(QIODevice::ReadOnly)) return;
        const QString id = QString::fromUtf8(request.readAll()).trimmed();
        if (id.isEmpty()) return;
        state->started = true;
        state->clock.start();
        qInfo().noquote() << "UNINSTALL_TIMING call id=" << id << "canUninstall=" << controller->canUninstall(id);
        QMetaObject::invokeMethod(controller, "uninstall", Q_ARG(QString, id));
    });
    timer->start(200);
}
