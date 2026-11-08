#pragma once
#include "PowerShellHarness.h"

inline void installSettingsPowerHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    QFile cmdline("/proc/cmdline");
    if (!cmdline.open(QIODevice::ReadOnly)) return;
    const auto tokens = cmdline.readAll().trimmed().split(' ');
    if (!tokens.contains("openwave.qa=poweroff") && !tokens.contains("openwave.qa=reboot")) return;
    struct State { int clicks = 0, wait = 0; };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *page = root->findChild<QObject *>("settingsPage");
        auto *general = root->findChild<QObject *>("settingsGeneralPage");
        QFile actionFile("/run/openwave/power-qa-action");
        if (!page || !general || !actionFile.open(QIODevice::ReadOnly)) return;
        const auto action = actionFile.readAll().trimmed();
        if (page->property("currentTab").toInt() != 2) {
            QMetaObject::invokeMethod(page, "selectTab", Q_ARG(QVariant, QVariant(2)), Q_ARG(QVariant, QVariant(false))); return;
        }
        if (general->property("category").toString() != "power") {
            QMetaObject::invokeMethod(general, "openCategory", Q_ARG(QVariant, QVariant("power"))); return;
        }
        if (state->clicks >= 2 || !page->property("generalPromptAction").toString().isEmpty()) { state->wait = 0; return; }
        if (++state->wait < 12) return;
        auto *button = root->findChild<QObject *>(action == "reboot" ? "settingsRestart" : "settingsShutdown");
        if (!button || !button->property("enabled").toBool()) return;
        qInfo() << "POWER_QA Settings General Power Options click" << action << state->clicks;
        ++state->clicks; state->wait = 0; powerQaClick(root, button);
    });
    timer->start(150);
}
