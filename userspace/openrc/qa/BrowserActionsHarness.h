#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTimer>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <sys/auxv.h>
#include <memory>

inline QString browserActionsQaUrl()
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) return {};
    const auto words = cmdline.readAll().trimmed().split(' ');
    if (!words.contains("openwave.qa=browser-actions")) return {};
    for (const auto &word : words)
        if (word.startsWith("openwave.browser-fixture=http://10.0.2.2:"))
            return QString::fromUtf8(word.mid(sizeof("openwave.browser-fixture=") - 1));
    return {};
}

inline void installBrowserActionsHarness(QObject *owner, QQmlApplicationEngine *engine,
    BrowserWebApps *webApps, OceanDownloadManager *downloads, BrowserPreferences *preferences)
{
    const QString url = browserActionsQaUrl();
    if (url.isEmpty()) return;
    struct State { int stage = 0, ticks = 0, wait = 0, expectedRows = 0; bool navigated = false, busy = false, failed = false; QString installed, error; };
    auto state = std::make_shared<State>();
    auto *apps = new ApplicationsClient(owner);
    auto *network = new QNetworkAccessManager(owner);
    QNetworkRequest probeRequest(QUrl(url + QStringLiteral("?probe=qt")));
    probeRequest.setTransferTimeout(5000);
    auto *probe = network->get(probeRequest);
    QObject::connect(probe, &QNetworkReply::finished, owner, [probe] {
        qInfo() << "BROWSER_QA HTTP probe" << probe->attribute(QNetworkRequest::HttpStatusCodeAttribute)
            << probe->error() << probe->errorString();
        probe->deleteLater();
    });
    auto *timer = new QTimer(owner);
    qInfo() << "BROWSER_QA secure execution AT_SECURE=" << getauxval(AT_SECURE);
    QObject::connect(webApps, &BrowserWebApps::errorOccurred, owner, [state](const QString &error) {
        state->error = error;
        qWarning().noquote() << "BROWSER_QA install popup:" << error;
    });
    QObject::connect(webApps, &BrowserWebApps::created, owner, [state](const QString &id, const QString &) {
        state->installed = id;
        qInfo().noquote() << "BROWSER_QA_PASS installed" << id;
    });
    QObject::connect(webApps, &BrowserWebApps::busyChanged, owner, [state, webApps] {
        if (webApps->busy()) state->busy = true;
    });
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        if (++state->ticks > 650) {
            qCritical() << "BROWSER_QA_FAIL timeout stage=" << state->stage;
            timer->stop(); QCoreApplication::exit(1); return;
        }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *view = root->property("webView").value<QObject *>();
        const QString resource = url.left(url.lastIndexOf('/')) + QStringLiteral("/attachment");
        auto install = [=] {
            state->busy = false; state->error.clear(); state->wait = 0;
            QMetaObject::invokeMethod(root, "menuAction", Q_ARG(QVariant, QVariant("Install as web app")));
        };
        if (state->stage == 0) {
            if (!state->navigated && state->ticks > 12 && view && view->property("ready").toBool()) {
                state->navigated = true;
                QMetaObject::invokeMethod(root, "navigate", Q_ARG(QVariant, QVariant(url)));
                qInfo() << "BROWSER_QA navigate fixture" << url;
            }
            if (!view || view->property("title").toString() != QStringLiteral("OpenWave Browser Actions QA") || !downloads->ready()) {
                if (state->ticks > 160) {
                    qCritical() << "BROWSER_QA_FAIL fixture readiness: view=" << bool(view)
                        << "url=" << (view ? view->property("url") : QVariant())
                        << "title=" << (view ? view->property("title") : QVariant())
                        << "nativeReady=" << (view ? view->property("ready") : QVariant())
                        << "engineError=" << root->property("engineError")
                        << "downloadsReady=" << downloads->ready();
                    timer->stop(); QCoreApplication::exit(1);
                }
                return;
            }
            qInfo() << "BROWSER_QA_PASS fixture loaded in WPE";
            preferences->setAskOnDownload(true);
            state->stage = 1; state->wait = 0;
            state->expectedRows = downloads->rowCount() + 1;
            downloads->saveResource(resource, false, true);
        } else if (state->stage >= 1 && state->stage <= 3) {
            ++state->wait;
            if (downloads->rowCount() < state->expectedRows) {
                if (state->wait > 30) { qCritical() << "BROWSER_QA_FAIL no active download view"; state->failed = true; state->stage = 4; }
                return;
            }
            const auto index = downloads->index(0);
            const QString status = downloads->data(index, OceanDownloadManager::Status).toString();
            if (status != "Cancelled" && status != "Completed" && status != "Failed") return;
            const bool okay = state->stage == 1 ? status == "Cancelled" : status == "Completed";
            if (!okay) {
                state->failed = true;
                qCritical().noquote() << "BROWSER_QA_FAIL download stage=" << state->stage << status
                    << downloads->data(index, OceanDownloadManager::ErrorDetail).toString();
            } else qInfo() << "BROWSER_QA_PASS download stage=" << state->stage << status;
            if (state->stage == 1) {
                state->stage = 2; state->wait = 0;
                state->expectedRows = downloads->rowCount() + 1;
                downloads->saveResource(resource, false, true);
            } else if (state->stage == 2) {
                QMetaObject::invokeMethod(root, "menuAction", Q_ARG(QVariant, QVariant("Downloads")));
                auto *page = root->findChild<QQuickItem *>(QStringLiteral("browserDownloadsPage"));
                if (!page || !page->isVisible()) { qCritical() << "BROWSER_QA_FAIL Downloads page"; state->failed = true; }
                else qInfo() << "BROWSER_QA_PASS Downloads page";
                QMetaObject::invokeMethod(root, "goBack");
                preferences->setAskOnDownload(false);
                state->stage = 3; state->wait = 0;
                state->expectedRows = downloads->rowCount() + 1;
                downloads->saveResource(resource, false, false);
            } else state->stage = 4;
        } else if (state->stage == 4) {
            state->stage = 5; install();
        } else if (state->stage == 5 && ((state->busy && !webApps->busy()) || !state->error.isEmpty())) {
            if (!state->error.isEmpty() || !state->installed.isEmpty()) {
                qCritical() << "BROWSER_QA_FAIL install cancellation"; state->failed = true;
            } else qInfo() << "BROWSER_QA_PASS install cancellation";
            state->stage = 6; install();
        } else if (state->stage == 6 && !webApps->busy() && (state->busy || !state->error.isEmpty())) {
            if (state->installed.isEmpty()) {
                qCritical().noquote() << "BROWSER_QA_FAIL install" << state->error;
                timer->stop(); QCoreApplication::exit(1); return;
            }
            state->stage = 7; install();
        } else if (state->stage == 7 && !webApps->busy() && !state->error.isEmpty()) {
            if (!state->error.contains("already installed")) {
                qCritical() << "BROWSER_QA_FAIL duplicate result"; state->failed = true;
            } else qInfo() << "BROWSER_QA_PASS duplicate rejected";
            state->stage = 8;
            apps->launch(state->installed, {}, [=](const QVariantList &, const QString &error) {
                if (!error.isEmpty()) { qCritical().noquote() << "BROWSER_QA_FAIL launch" << error; state->failed = true; }
                if (!state->failed) qInfo() << "BROWSER_QA_PASS Browser actions complete";
                timer->stop();
                QTimer::singleShot(2500, QCoreApplication::instance(), &QCoreApplication::quit);
            });
        }
    });
    timer->start(150);
}
