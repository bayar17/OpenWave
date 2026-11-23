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
        QFile actionFile("/run/openwave/power-qa-action");
        if (!page || !actionFile.open(QIODevice::ReadOnly)) return;
        const auto action = actionFile.readAll().trimmed();
        int generalTab = -1;
        const QVariantList entries = page->property("tabEntries").toList();
        for (int index = 0; index < entries.size(); ++index) {
            if (entries.at(index).toMap().value("moduleId").toString() == QLatin1String("com.openwave.settings.general"))
                generalTab = index;
        }
        if (generalTab < 0) return;
        if (page->property("currentTab").toInt() != generalTab) {
            QMetaObject::invokeMethod(page, "selectTab", Q_ARG(QVariant, QVariant(generalTab)), Q_ARG(QVariant, QVariant(false))); return;
        }
        auto *general = root->findChild<QObject *>("generalModule");
        if (!general) return;
        if (general->property("category").toString() != "power") {
            QMetaObject::invokeMethod(general, "openCategory", Q_ARG(QVariant, QVariant("power"))); return;
        }
        if (state->clicks >= 2 || !general->property("pendingAction").toString().isEmpty()) { state->wait = 0; return; }
        if (++state->wait < 12) return;
        auto *button = root->findChild<QObject *>(action == "reboot" ? "generalRestart" : "generalShutdown");
        if (!button || !button->property("enabled").toBool()) return;
        qInfo() << "POWER_QA Settings General Power Options click" << action << state->clicks;
        ++state->clicks; state->wait = 0; powerQaClick(root, button);
    });
    timer->start(150);
}
