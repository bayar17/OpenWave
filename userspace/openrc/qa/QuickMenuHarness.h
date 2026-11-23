#pragma once
#include <QDebug>
#include <QFile>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <memory>
#include "QuickMenuAppearance.h"
#include "QuickMenuAppletRegistry.h"
#include "QuickMenuSystem.h"

namespace quickmenuqa {

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

inline void collect(QQuickItem *parent, const QString &prefix, QList<QQuickItem *> &found)
{
    if (!parent)
        return;
    if (parent->objectName().startsWith(prefix))
        found.append(parent);
    for (QQuickItem *child : parent->childItems())
        collect(child, prefix, found);
}

}

inline void installQuickMenuHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)
        || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=quickmenu")) return;
    struct State {
        int stage = 0;
        int ticks = 0;
        int hold = 0;
        bool noticeSeen = false;
        bool crossfadeSeen = false;
        bool wantsWallpaper = false;
        bool chose = false;
        double noticePeak = 0;
    };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    auto fail = [state, timer](const QString &reason) {
        qCritical().noquote() << "QUICKMENU_QA_FAIL" << reason;
        state->stage = 99;
        timer->stop();
    };
    QObject::connect(timer, &QTimer::timeout, owner, [engine, state, timer, fail] {
        if (state->stage >= 5) { timer->stop(); return; }
        if (++state->ticks > 1200) { fail(QStringLiteral("timeout stage=%1").arg(state->stage)); return; }
        if (engine->rootObjects().isEmpty()) return;
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        if (!window) return;
        QQuickItem *menu = quickmenuqa::search(window->contentItem(), QStringLiteral("quickMenu"));
        if (!menu) return;
        QList<QQuickItem *> hosts;
        quickmenuqa::collect(menu, QStringLiteral("quickApplet_"), hosts);
        QStringList loaded;
        for (QQuickItem *host : hosts) {
            const QVariant applet = host->property("applet");
            if (auto *item = applet.value<QQuickItem *>())
                loaded << host->objectName().mid(12) + QLatin1Char('=') + item->property("title").toString();
        }
        QuickMenuAppletRegistry *registry = QuickMenuAppletRegistry::shared();
        QuickMenuAppearance *appearance = QuickMenuAppearance::shared();
        QuickMenuSystem *system = QuickMenuSystem::shared();
        if (state->stage == 0) {
            if (hosts.isEmpty() || loaded.size() != hosts.size()) return;
            QStringList failed;
            for (QQuickItem *host : hosts) {
                if (host->property("failed").toBool())
                    failed << host->objectName();
            }
            if (!failed.isEmpty()) { fail(QStringLiteral("applets failed to load: %1").arg(failed.join(QLatin1Char(',')))); return; }
            const QStringList required{QStringLiteral("WiFi"), QStringLiteral("Bluetooth"), QStringLiteral("MobileData"), QStringLiteral("FlightMode")};
            QStringList ids;
            for (const QVariant &entry : registry->panelApplets())
                ids << entry.toMap().value(QStringLiteral("id")).toString();
            for (const QString &id : required) {
                if (!ids.contains(id)) { fail(QStringLiteral("required applet %1 is not in the panel").arg(id)); return; }
            }
            qInfo().noquote() << "QUICKMENU_QA STATE loaded=" << loaded.join(QLatin1Char('|'))
                              << "panel=" << ids.join(QLatin1Char(','))
                              << "rejections=" << registry->rejections().size()
                              << "presets=" << appearance->presets().size()
                              << "appearance=" << appearance->current()
                              << "connected=" << system->connected()
                              << "wifi=" << system->wifiAvailable()
                              << "bluetooth=" << system->bluetoothAvailable()
                              << "cellular=" << system->cellularAvailable()
                              << "battery=" << system->batteryAvailable() << system->batteryPercent()
                              << "volume=" << system->volume() << "brightness=" << system->brightness();
            for (const QVariant &rejection : registry->rejections())
                qInfo().noquote() << "QUICKMENU_QA REJECTED" << rejection.toMap().value(QStringLiteral("path")).toString()
                                  << rejection.toMap().value(QStringLiteral("reason")).toString();
            QMetaObject::invokeMethod(menu, "settle", Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant(0)));
            state->stage = 1;
            return;
        }
        if (state->stage == 1) {
            if (menu->property("progress").toDouble() < 0.999) return;
            if (++state->hold < 10) return;
            state->hold = 0;
            qInfo().noquote() << "QUICKMENU_QA OPEN appearance=" << appearance->current()
                              << "pages=" << menu->property("pageCount").toInt()
                              << "unit=" << menu->property("unit").toDouble();
            state->stage = 2;
            return;
        }
        if (state->stage == 2) {
            if (!QFile::exists(QStringLiteral("/run/openwave/qa-quickmenu-next"))) return;
            const QVariantList presets = appearance->presets();
            if (presets.isEmpty()) {
                qInfo().noquote() << "QUICKMENU_QA APPEARANCE none installed strip=" << (quickmenuqa::search(menu, QStringLiteral("quickMenuAppearances")) ? quickmenuqa::search(menu, QStringLiteral("quickMenuAppearances"))->isVisible() : false);
                state->stage = 4;
                return;
            }
            const QVariantMap chosen = presets.last().toMap();
            state->wantsWallpaper = !chosen.value(QStringLiteral("wallpaper")).toUrl().isEmpty();
            QMetaObject::invokeMethod(menu, "chooseAppearance", Q_ARG(QVariant, QVariant(chosen)));
            state->chose = true;
            state->hold = 0;
            state->stage = 3;
            return;
        }
        if (state->stage == 3) {
            QQuickItem *notice = quickmenuqa::search(window->contentItem(), QStringLiteral("appearanceNotice"));
            QQuickItem *wallpaper = quickmenuqa::search(window->contentItem(), QStringLiteral("homeWallpaperImage"));
            if (notice) {
                const double opacity = notice->opacity();
                state->noticePeak = std::max(state->noticePeak, opacity);
                if (opacity > 0.5)
                    state->noticeSeen = true;
            }
            if (wallpaper && wallpaper->property("fading").toBool())
                state->crossfadeSeen = true;
            const bool noticeDone = state->noticeSeen && notice && notice->opacity() == 0;
            if (!noticeDone && ++state->hold < 60) return;
            qInfo().noquote() << "QUICKMENU_QA TRANSITION menuOpen=" << menu->property("open").toBool()
                              << "noticeSeen=" << state->noticeSeen << "noticePeak=" << state->noticePeak
                              << "crossfadeSeen=" << state->crossfadeSeen << "wantsWallpaper=" << state->wantsWallpaper
                              << "current=" << appearance->current();
            if (menu->property("open").toBool()) { fail(QStringLiteral("the menu did not slide up")); return; }
            if (!state->noticeSeen) { fail(QStringLiteral("the appearance name never appeared")); return; }
            if (state->wantsWallpaper && !state->crossfadeSeen) { fail(QStringLiteral("the wallpaper did not crossfade")); return; }
            state->hold = 0;
            state->stage = 4;
            return;
        }
        if (state->stage == 4) {
            if (!QFile::exists(QStringLiteral("/run/openwave/qa-quickmenu-done"))) return;
            qInfo().noquote() << "QUICKMENU_QA PASS Quick Menu loaded its applets, opened and switched appearance";
            state->stage = 5;
        }
    });
    timer->start(250);
}
