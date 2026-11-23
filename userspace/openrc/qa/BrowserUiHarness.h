#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <memory>

inline QString browserUiQaUrl()
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) return {};
    const auto words = cmdline.readAll().trimmed().split(' ');
    if (!words.contains("openwave.qa=browser-ui")) return {};
    for (const auto &word : words)
        if (word.startsWith("openwave.browser-fixture=http://10.0.2.2:"))
            return QString::fromUtf8(word.mid(sizeof("openwave.browser-fixture=") - 1));
    return {};
}

inline void installBrowserUiHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    const QString url = browserUiQaUrl();
    if (url.isEmpty()) return;
    const QString base = url.left(url.lastIndexOf('/'));
    struct State {
        int step = -1;
        QElapsedTimer clock;
        bool started = false;
    };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    timer->setInterval(250);
    constexpr qint64 FirstStepMs = 4000;
    constexpr qint64 StepMs = 14000;
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *view = root->property("webView").value<QObject *>();
        if (!state->started) {
            if (!view || !view->property("ready").toBool()) return;
            state->started = true;
            state->clock.start();
            qInfo() << "BROWSER_UI_QA READY";
        }
        const int due = state->clock.elapsed() < FirstStepMs ? -1 : int((state->clock.elapsed() - FirstStepMs) / StepMs);
        if (due <= state->step) return;
        state->step = due;
        const auto call = [root](const char *method, const QVariant &argument = QVariant()) {
            if (argument.isValid()) QMetaObject::invokeMethod(root, method, Q_ARG(QVariant, argument));
            else QMetaObject::invokeMethod(root, method);
        };
        qInfo() << "BROWSER_UI_QA STEP" << state->step;
        switch (state->step) {
        case 0:
            break;
        case 1:
            call("navigate", base + QStringLiteral("/mobile"));
            break;
        case 2:
            root->setProperty("menuOpen", true);
            break;
        case 3:
            root->setProperty("menuOpen", false);
            call("openFind");
            if (auto *input = root->findChild<QObject *>(QStringLiteral("browserFindInput")))
                input->setProperty("text", QStringLiteral("mobile"));
            break;
        case 4:
            call("closeFind");
            call("bookmarkCurrent");
            root->setProperty("menuOpen", true);
            break;
        case 5:
            root->setProperty("menuOpen", false);
            call("navigate", QStringLiteral("about:bookmarks"));
            break;
        case 6:
            call("navigate", QStringLiteral("about:history"));
            break;
        case 7:
            call("navigate", QStringLiteral("about:downloads"));
            break;
        case 8:
            call("menuAction", QStringLiteral("New tab"));
            break;
        case 9:
            call("menuAction", QStringLiteral("New Private Tab"));
            break;
        case 10:
            call("menuAction", QStringLiteral("Show all tabs"));
            break;
        case 11:
            root->setProperty("overviewPrivate", false);
            break;
        case 12:
            root->setProperty("menuOpen", true);
            break;
        case 13:
            root->setProperty("menuOpen", false);
            call("goBack");
            break;
        default:
            qInfo() << "BROWSER_UI_QA PASS walkthrough complete";
            timer->stop();
            break;
        }
    });
    timer->start();
}
