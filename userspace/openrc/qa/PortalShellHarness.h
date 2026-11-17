#pragma once
#include <QFile>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QSet>
#include <QDebug>
#include <memory>
#include "../../../OceanLauncher/OceanFiles/FilesBackend.h"

inline void installPortalShellHarness(QObject *owner, QQmlApplicationEngine *engine,
    PortalFileChooserBackend *backend, FilesBackend *files)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly) || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=portal")) return;
    auto processed = std::make_shared<QSet<QString>>();
    auto *timer = new QTimer(owner);
    QObject::connect(backend, &PortalFileChooserBackend::errorOccurred, owner, [](const QString &error) {
        qCritical().noquote() << "PORTAL_QA_FAIL backend" << error;
    });
    QObject::connect(timer, &QTimer::timeout, owner, [engine, backend, files, processed] {
        if (backend->requestId().isEmpty() || backend->busy() || processed->contains(backend->requestId())
            || backend->applicationId() != QStringLiteral("com.openwave.settings") || engine->rootObjects().isEmpty()) return;
        auto *picker = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("portalFileChooser"));
        if (!picker || picker->property("phase").toString() != QStringLiteral("open")
            || picker->property("displayedRequestId").toString() != backend->requestId()) return;
        if (processed->isEmpty()) {
            processed->insert(backend->requestId()); qInfo() << "PORTAL_QA picker visible: cancel"; backend->cancel();
        } else if (backend->imageSource() == QStringLiteral("gallery")) {
            backend->loadGallery();
            if (backend->galleryLoading()) return;
            for (const auto &item : backend->galleryItems()) {
                const auto row = item.toMap();
                if (row.value("name").toString() != QStringLiteral("Imported portal test.png")) continue;
                if (backend->galleryImage(backend->requestId(), row.value("token").toString(), QSize(96, 96)).isNull()) {
                    qCritical() << "PORTAL_QA_FAIL gallery preview"; return;
                }
                processed->insert(backend->requestId()); qInfo() << "PORTAL_QA picker visible: Photos";
                backend->chooseGallery({row.value("token").toString()}); return;
            }
        } else {
            const QString path = QStringLiteral("/Data/Users/ocean/Pictures/OpenWave portal test.png");
            if (!files->goToDirectory(QStringLiteral("/Data/Users/ocean/Pictures"))) {
                qCritical() << "PORTAL_QA_FAIL Files directory" << files->error(); return;
            }
            bool listed = false;
            for (int i = 0; i < files->rowCount(); ++i) if (files->entryPath(i) == path) listed = true;
            if (!listed || !backend->acceptsPath(path)) { qCritical() << "PORTAL_QA_FAIL Files listing or image filter"; return; }
            processed->insert(backend->requestId()); qInfo() << "PORTAL_QA picker visible: Files";
            backend->choose({QStringLiteral("/Data/Users/ocean/Pictures/OpenWave portal test.png")});
        }
    });
    timer->start(100);
}
