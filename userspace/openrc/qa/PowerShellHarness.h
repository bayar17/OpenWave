#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/PromptRendererBackend.h>

inline QObject *powerQaVisualChild(QQuickItem *item, const QString &name)
{
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *found = powerQaVisualChild(child, name)) return found;
    return nullptr;
}

inline void powerQaClick(QObject *root, QObject *button, bool innerBottom = false, bool reboot = false)
{
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *item = qobject_cast<QQuickItem *>(button);
    if (!window || !item) return;
    const QPointF local = item->mapToItem(window->contentItem(),
        QPointF(item->width() * (innerBottom ? (reboot ? 0.8 : 0.2) : 0.5),
                innerBottom ? item->height() - 4 : item->height() / 2));
    const QPointF global = window->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
}

inline void installPowerShellHarness(QObject *owner, QQmlApplicationEngine *engine,
    PromptRendererBackend *prompts)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) return;
    const auto tokens = cmdline.readAll().trimmed().split(' ');
    if (!tokens.contains("openwave.qa=poweroff") && !tokens.contains("openwave.qa=reboot")) return;
    struct State { int stage = 0; bool launching = false; };
    auto state = std::make_shared<State>();
    auto *apps = new ApplicationsClient(owner);
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *prompt = root->findChild<QObject *>(QStringLiteral("systemPrompt"));
        QFile actionFile(QStringLiteral("/run/openwave/power-qa-action"));
        if (!prompt || !actionFile.open(QIODevice::ReadOnly)) return;
        const auto action = actionFile.readAll().trimmed();
        if (state->stage == 0 && !state->launching) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            if (window && (powerQaVisualChild(window->contentItem(), "quickMenuRestart")
                || powerQaVisualChild(window->contentItem(), "quickMenuShutdown"))) {
                qCritical() << "POWER_QA FAIL Quick Menu power actions remain"; timer->stop(); return;
            }
            state->launching = true;
            apps->launch("com.openwave.settings", {}, [=](const QVariantList &, const QString &error) {
                state->launching = false;
                if (error.isEmpty()) state->stage = 1;
            });
        }
        const auto request = prompts->request();
        if (state->stage < 1 || state->stage > 2 || request.value("applicationId").toString() != "com.openwave.settings"
            || request.value("type").toString() != "confirmation" || prompt->property("phase").toString() != "open") return;
        auto *button = prompt->findChild<QObject *>(state->stage == 1 ? "systemPromptCancel" : "systemPromptAccept");
        if (!button || !button->property("enabled").toBool()) return;
        if (state->stage == 1) {
            qInfo() << "POWER_QA cancellation";
            state->stage = 2; powerQaClick(root, button);
            // Wait for the first request to disappear before accepting another.
            timer->stop(); QTimer::singleShot(1500, timer, [timer] { timer->start(); });
        } else {
            qInfo().noquote() << "POWER_QA confirmation action=" + QString::fromLatin1(action);
            state->stage = 3; timer->stop();
            QTimer::singleShot(1500, button, [root, button] { powerQaClick(root, button); });
        }
    });
    timer->start(150);
}
