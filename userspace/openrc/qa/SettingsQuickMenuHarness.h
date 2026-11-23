#pragma once
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QSettings>
#include <OpenWave/preferences.h>
#include <unistd.h>
#include "../../../OceanLauncher/OceanSettings/AppearanceManager.h"
#include "../../../OceanLauncher/OceanSettings/PreferenceBroker.h"
#include <QQuickItem>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

namespace settingsquickmenuqa {

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

inline void collect(QQuickItem *parent, const QString &prefix, QStringList &names)
{
    if (!parent)
        return;
    if (parent->objectName().startsWith(prefix))
        names << parent->objectName().mid(prefix.size());
    for (QQuickItem *child : parent->childItems())
        collect(child, prefix, names);
}

}

inline void installSettingsQuickMenuHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=quickmenu")) return;
    struct State {
        int stage = 0;
        int ticks = 0;
        QString createdId;
        QString outcome;
    };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    auto fail = [state, timer](const QString &reason) {
        qCritical().noquote() << "QUICKMENU_QA_FAIL settings" << reason;
        state->stage = 99;
        timer->stop();
    };
    QObject::connect(timer, &QTimer::timeout, owner, [engine, state, timer, fail] {
        if (state->stage >= 7) { timer->stop(); return; }
        if (++state->ticks > 1200) { fail(QStringLiteral("timeout stage=%1").arg(state->stage)); return; }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = qobject_cast<QQuickItem *>(engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsPage")));
        if (!root || !root->isVisible()) return;
        if (state->stage == 0) {
            const int index = root->property("appearanceTab").toInt();
            QMetaObject::invokeMethod(root, "selectTab", Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(false)));
            qInfo().noquote() << "QUICKMENU_QA SETTINGS appearance tab index=" << index;
            state->stage = 10;
            state->ticks = 0;
            return;
        }
        if (state->stage == 10) {
            auto *page = settingsquickmenuqa::search(root, QStringLiteral("settingsAppearancePageHost"));
            auto *row = settingsquickmenuqa::search(root, QStringLiteral("settingsAppearanceQuickMenu"));
            if (!page || !row || !row->isVisible()) return;
            QMetaObject::invokeMethod(page, "openQuickMenu");
            state->stage = 1;
            state->ticks = 0;
            return;
        }
        if (state->stage == 1) {
        auto *pane = settingsquickmenuqa::search(root, QStringLiteral("quickMenuMainPane"));
        if (!pane || !pane->isVisible()) return;
        QStringList rows;
        settingsquickmenuqa::collect(pane, QStringLiteral("quickMenuApplet_"), rows);
        if (rows.size() < 5) return;
        auto *empty = settingsquickmenuqa::search(root, QStringLiteral("quickMenuEmpty"));
        qInfo().noquote() << "QUICKMENU_QA SETTINGS rows=" << rows.join(QLatin1Char(','))
                          << "empty=" << (empty ? empty->isVisible() : false);
        qInfo().noquote() << "QUICKMENU_QA SETTINGS PASS Settings listed the Quick Menu applets";
        state->stage = 2;
        state->ticks = 0;
        return;
        }
        auto *manager = qobject_cast<AppearanceManager *>(engine->rootContext()->contextProperty(QStringLiteral("Appearances")).value<QObject *>());
        if (!manager) { fail(QStringLiteral("the Appearances object is missing")); return; }
        const auto hasPreset = [manager](const QString &id, bool *user) {
            for (const QVariant &item : manager->property("presets").toList()) {
                const QVariantMap preset = item.toMap();
                if (preset.value(QStringLiteral("id")).toString() == id) {
                    if (user) *user = preset.value(QStringLiteral("user")).toBool();
                    return true;
                }
            }
            return false;
        };
        if (state->stage == 2) {
            const QString path = openwavePreferencesPath();
            QSettings probe(path, QSettings::IniFormat);
            probe.setValue(QStringLiteral("qa/probe"), 1);
            probe.sync();
            const bool writable = probe.status() == QSettings::NoError;
            probe.remove(QStringLiteral("qa"));
            probe.sync();
            static bool pathAnnounced = false;
            if (!pathAnnounced && (pathAnnounced = true))
                qInfo().noquote() << "QUICKMENU_QA SETTINGS prefs path=" << path << "euid=" << int(geteuid()) << "writable=" << writable;
            QObject *applets = engine->rootContext()->contextProperty(QStringLiteral("QuickMenuApplets")).value<QObject *>();
            QObject *backend = engine->rootObjects().first()->property("settingsBackend").value<QObject *>();
            const auto mirror = [&path](const QByteArray &needle) {
                QFile file(path);
                return file.open(QIODevice::ReadOnly) && file.readAll().contains(needle);
            };
            QElapsedTimer elapsed;
            elapsed.start();
            bool appletOff = false, appletMirror = false, appletReset = false, motionOn = false, motionMirror = false, motionOff = false, motionBack = false;
            if (applets) {
                QMetaObject::invokeMethod(applets, "setEnabled", Q_RETURN_ARG(bool, appletOff), Q_ARG(QString, QStringLiteral("Lock")), Q_ARG(bool, false));
                appletMirror = mirror("applets\\state\\Lock=false");
                QMetaObject::invokeMethod(applets, "reset", Q_RETURN_ARG(bool, appletReset));
            }
            if (backend) {
                QMetaObject::invokeMethod(backend, "setReduceMotion", Q_RETURN_ARG(bool, motionOn), Q_ARG(bool, true));
                motionMirror = mirror("reduceMotion=true");
                QMetaObject::invokeMethod(backend, "setReduceMotion", Q_RETURN_ARG(bool, motionOff), Q_ARG(bool, false));
                motionBack = mirror("reduceMotion=false");
            }
            const bool persisted = appletOff && appletMirror && appletReset && motionOn && motionMirror && motionOff && motionBack;
            qInfo().noquote() << "QUICKMENU_QA SETTINGS PREFS appletSaved=" << appletOff << "appletInMirror=" << appletMirror
                              << "appletReset=" << appletReset << "motionSaved=" << motionOn << "motionInMirror=" << motionMirror
                              << "motionRestored=" << motionOff << "restoredInMirror=" << motionBack
                              << "brokered=" << PreferenceBroker::brokered() << "applets=" << (applets != nullptr)
                              << "backend=" << (backend != nullptr) << "elapsedMs=" << elapsed.elapsed() << "lastFailure=" << PreferenceBroker::lastFailure()
                              << (persisted ? "PASS Settings persisted preferences through appd" : "FAIL preferences were not persisted");
            QMetaObject::invokeMethod(settingsquickmenuqa::search(root, QStringLiteral("settingsAppearancePageHost")), "back");
            state->stage = 3;
            state->ticks = 0;
            return;
        }
        if (state->stage == 3) {
            QStringList cards;
            settingsquickmenuqa::collect(root, QStringLiteral("settingsAppearanceCard_"), cards);
            qInfo().noquote() << "QUICKMENU_QA SETTINGS appearance cards=" << cards.join(QLatin1Char(','))
                              << "current=" << manager->property("current").toString();
            QObject::connect(manager, &AppearanceManager::finished, qApp, [state](const QString &operation, const QString &id) {
                if (operation == QLatin1String("save")) {
                    state->createdId = id;
                    state->outcome = operation;
                } else {
                    state->outcome = operation;
                }
            });
            QObject::connect(manager, &AppearanceManager::failed, qApp, [state](const QString &operation, const QString &message) {
                state->outcome = QStringLiteral("failed:") + operation + QLatin1Char(':') + message;
            });
            const QUrl image = QUrl::fromLocalFile(QStringLiteral("/System/Applications/com.openwave.settings/share/icons/icon.png"));
            QMetaObject::invokeMethod(manager, "save", Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral("QA Sunset")),
                                      Q_ARG(double, 0.1), Q_ARG(QUrl, image), Q_ARG(double, 0), Q_ARG(double, 0),
                                      Q_ARG(double, 768), Q_ARG(double, 1366));
            state->stage = 4;
            state->ticks = 0;
            return;
        }
        if (state->stage == 4) {
            if (state->outcome.isEmpty()) return;
            if (state->outcome.startsWith(QLatin1String("failed:"))) { fail(state->outcome); return; }
            bool user = false;
            if (state->createdId.isEmpty() || !hasPreset(state->createdId, &user) || !user) {
                fail(QStringLiteral("the saved appearance is not listed as a user appearance: ") + state->createdId);
                return;
            }
            qInfo().noquote() << "QUICKMENU_QA SETTINGS appearance saved id=" << state->createdId;
            state->outcome.clear();
            QMetaObject::invokeMethod(manager, "set", Q_ARG(QString, state->createdId));
            state->stage = 5;
            state->ticks = 0;
            return;
        }
        if (state->stage == 5) {
            if (state->outcome.isEmpty()) return;
            if (state->outcome.startsWith(QLatin1String("failed:"))) { fail(state->outcome); return; }
            if (manager->property("current").toString() != state->createdId) { fail(QStringLiteral("the saved appearance was not selected")); return; }
            qInfo().noquote() << "QUICKMENU_QA SETTINGS appearance applied id=" << state->createdId;
            state->outcome.clear();
            QMetaObject::invokeMethod(manager, "remove", Q_ARG(QString, state->createdId));
            state->stage = 6;
            state->ticks = 0;
            return;
        }
        if (state->stage == 6) {
            if (state->outcome.isEmpty()) return;
            if (state->outcome.startsWith(QLatin1String("failed:"))) { fail(state->outcome); return; }
            if (hasPreset(state->createdId, nullptr)) { fail(QStringLiteral("the deleted appearance is still listed")); return; }
            qInfo().noquote() << "QUICKMENU_QA SETTINGS appearance removed id=" << state->createdId;
            qInfo().noquote() << "QUICKMENU_QA SETTINGS APPEARANCE PASS Settings saved, applied and removed an appearance through the daemon";
            state->stage = 7;
            timer->stop();
            QTimer::singleShot(25000, qApp, &QCoreApplication::quit);
        }
    });
    timer->start(150);
}
