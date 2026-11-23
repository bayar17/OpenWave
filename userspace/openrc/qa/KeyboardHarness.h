#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>
#include <memory>

// Only focuses/observes fields. Every key is supplied through QEMU's hardware
// input device, never QCoreApplication::sendEvent or a direct text assignment.
inline void installKeyboardHarness(QObject *owner, QQmlApplicationEngine *engine, bool shell)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=keyboard")) return;
    struct State { QQuickItem *field = nullptr; int ticks = 0; };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [engine, shell, state, timer] {
        if (++state->ticks > 240) {
            qCritical() << "KEYBOARD_QA_FAIL" << (shell ? "shell" : "client") << "timeout";
            timer->stop();
            if (!shell) QCoreApplication::exit(1);
            return;
        }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        if (!window || !window->isVisible() || state->ticks < 15) return;
        if (!state->field) {
            if (shell) {
                QQmlComponent component(engine);
                component.setData("import QtQuick\nTextInput { width: 480; height: 80; color: \"white\"; font.pixelSize: 32 }", QUrl());
                state->field = qobject_cast<QQuickItem *>(component.create());
                if (state->field) {
                    state->field->setParent(window->contentItem());
                    state->field->setParentItem(window->contentItem());
                    state->field->setPosition(QPointF(22, 100));
                    state->field->setZ(10000);
                }
            } else {
                auto *page = root->findChild<QObject *>(QStringLiteral("settingsPage"));
                if (!page) return;
                page->setProperty("searching", true);
                state->field = root->findChild<QQuickItem *>(QStringLiteral("settingsSearchField"));
            }
            if (!state->field) return;
            state->field->forceActiveFocus();
            qInfo().noquote() << "KEYBOARD_QA_READY" << (shell ? "shell" : "client");
        }
        if (state->field->property("text").toString() == QStringLiteral("abD")) {
            qInfo().noquote() << "KEYBOARD_QA_PASS" << (shell ? "shell" : "client") << "hardware text/editing";
            timer->stop();
            if (shell) state->field->deleteLater();
            else QTimer::singleShot(300, QCoreApplication::instance(), &QCoreApplication::quit);
        }
    });
    timer->start(150);
}
