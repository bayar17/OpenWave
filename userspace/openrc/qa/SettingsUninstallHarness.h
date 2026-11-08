#pragma once
#include "PowerShellHarness.h"

inline void installSettingsUninstallHarness(QObject *owner, QQmlApplicationEngine *engine, ApplicationsBackend *apps)
{
    QFile cmdline("/proc/cmdline");
    if (!cmdline.open(QIODevice::ReadOnly) || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=browser-actions")) return;
    struct State { int stage = 0, ticks = 0, wait = 0; QString id; };
    auto state = std::make_shared<State>();
    QObject::connect(apps, &ApplicationsBackend::uninstalled, owner, [=](const QString &id) {
        if (id != state->id) return;
        state->stage = 4;
        qInfo() << "UNINSTALL_QA_PASS confirmed removal" << id;
    });
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        if (++state->ticks > 230) {
            qCritical() << "UNINSTALL_QA_FAIL timeout stage=" << state->stage << apps->error();
            timer->stop(); QCoreApplication::exit(1); return;
        }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *page = root->findChild<QObject *>("settingsPage");
        auto *list = root->findChild<QObject *>("settingsAppsPage");
        if (!page || !list || apps->busy()) return;
        if (page->property("currentTab").toInt() != 5) {
            QMetaObject::invokeMethod(page, "selectTab", Q_ARG(QVariant, QVariant(5)), Q_ARG(QVariant, QVariant(false))); return;
        }
        QVariantMap selected;
        for (const auto &entry : apps->applications()) {
            const auto row = entry.toMap();
            if (row.value("name").toString() == "OpenWave Browser QA Site") selected = row;
        }
        if (state->stage == 4) {
            if (!selected.isEmpty()) return;
            qInfo() << "UNINSTALL_QA_PASS removed from Settings registry";
            timer->stop(); QCoreApplication::quit(); return;
        }
        if (selected.isEmpty()) { apps->refresh(); return; }
        state->id = selected.value("appId").toString();
        if (list->property("applicationId").toString() != state->id) {
            QMetaObject::invokeMethod(list, "openApplication", Q_ARG(QVariant, QVariant(selected))); return;
        }
        if (apps->uninstallBusy()) { state->wait = 0; return; }
        if (++state->wait < 12 || state->stage >= 2) return;
        auto *button = root->findChild<QObject *>("settingsAppUninstall");
        if (!button || !button->property("enabled").toBool()) return;
        if (state->stage == 1) qInfo() << "UNINSTALL_QA_PASS cancellation preserved installed app";
        ++state->stage; state->wait = 0;
        powerQaClick(root, button);
    });
    timer->start(150);
}
