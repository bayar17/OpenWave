#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/WallpaperClient.h>

inline void installPortalSettingsHarness(QObject *owner, QQmlApplicationEngine *engine,
    OpenWavePortals *portals, WallpaperClient *wallpaper)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly) || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=portal")) return;
    auto stage = std::make_shared<int>(0);
    QObject::connect(portals, &OpenWavePortals::errorOccurred, owner, [stage](const QString &error) {
        qCritical().noquote() << "PORTAL_QA_FAIL client" << error; *stage = 99;
    });
    QObject::connect(portals, &OpenWavePortals::cancelled, owner, [stage] {
        qInfo() << "PORTAL_QA cancelled";
        if (*stage == 1) *stage = 2;
    });
    QObject::connect(portals, &OpenWavePortals::completed, owner, [stage](const QVariantList &grants) {
        qInfo() << "PORTAL_QA grant" << "stage=" << *stage << "count=" << grants.size();
        if (grants.size() != 1) { qCritical() << "PORTAL_QA_FAIL grant count"; *stage = 99; return; }
        auto *grant = qobject_cast<PortalGrant *>(grants.first().value<QObject *>());
        if (!grant || !grant->readAll().startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
            qCritical() << "PORTAL_QA_FAIL image descriptor"; *stage = 99; return;
        }
        if (*stage == 3) *stage = 4;
        else if (*stage == 7) *stage = 8;
    });
    QObject::connect(wallpaper, &WallpaperClient::saved, owner, [stage] {
        qInfo() << "PORTAL_QA saved stage=" << *stage;
        if (*stage == 5) *stage = 6;
        else if (*stage == 9) *stage = 10;
    });
    QObject::connect(wallpaper, &WallpaperClient::errorOccurred, owner, [stage](const QString &error) {
        qWarning().noquote() << "PORTAL_QA wallpaper error" << error;
        if (*stage == 5 || *stage == 9) { qCritical() << "PORTAL_QA_FAIL save"; *stage = 99; }
    });
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [engine, portals, wallpaper, stage, timer] {
        if (*stage == 99) { timer->stop(); return; }
        if (engine->rootObjects().isEmpty()) return;
        auto *root = engine->rootObjects().first();
        auto *page = root->findChild<QObject *>(QStringLiteral("settingsPage"));
        if (!page || !root->property("visible").toBool()) return;
        if (page->property("currentTab").toInt() != 4) {
            if (!QMetaObject::invokeMethod(page, "selectTab", Q_ARG(QVariant, QVariant(4)), Q_ARG(QVariant, QVariant(true)))) {
                qCritical() << "PORTAL_QA_FAIL Wallpaper tab invocation"; *stage = 99;
            }
            return;
        }
        if (page->property("tabSwitching").toBool() || page->property("tabCapturePending").toBool()) return;
        if (*stage == 0 || *stage == 2 || *stage == 6) {
            if (portals->busy()) return;
            const QString source = *stage == 6 ? QStringLiteral("photos") : QStringLiteral("files");
            ++*stage;
            qInfo().noquote() << "PORTAL_QA request" << source << "stage=" << *stage;
            QMetaObject::invokeMethod(page, "chooseWallpaperFile", Q_ARG(QVariant, QVariant(source)));
        } else if (*stage == 4 || *stage == 8) {
            auto *apply = root->findChild<QObject *>(QStringLiteral("settingsCropApply"));
            if (!apply || !apply->property("enabled").toBool()) return;
            qInfo() << "PORTAL_QA crop ready stage=" << *stage;
            ++*stage; QMetaObject::invokeMethod(apply, "clicked");
        } else if (*stage == 10 && !wallpaper->source().isEmpty()) {
            const auto size = wallpaper->imageSize(wallpaper->source());
            if (size.value("width").toInt() != 768 || size.value("height").toInt() != 1366) return;
            qInfo() << "PORTAL_QA_PASS Settings Wallpaper: cancel, Files, Photos, descriptor, crop, save and refresh";
            *stage = 11; timer->stop();
        }
    });
    timer->start(100);
    QTimer::singleShot(60000, owner, [stage, timer] {
        if (*stage != 11) qCritical() << "PORTAL_QA_FAIL Settings timeout stage=" << *stage;
        timer->stop();
    });
}
