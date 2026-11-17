#pragma once
#include <QFile>
#include <QDir>
#include <QImage>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <OpenWave/ApplicationsClient.h>

inline bool associationQaEnabled()
{
    QFile commandLine("/proc/cmdline");
    return commandLine.open(QIODevice::ReadOnly) && commandLine.readAll().trimmed().split(' ').contains("openwave.qa=associations");
}
inline QString associationQaDefault(const QVariantList &types, const QString &type)
{
    for (const auto &item : types) if (item.toMap().value("extension").toString() == type)
        return item.toMap().value("defaultId").toString();
    return {};
}
inline void installSettingsAssociationHarness(QObject *owner, ApplicationsBackend *backend)
{
    if (!associationQaEnabled()) return;
    auto stage = std::make_shared<int>(0);
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [backend, stage, timer] {
        const auto types = backend->fileTypes();
        const auto png = associationQaDefault(types, "image/png"), jpg = associationQaDefault(types, "image/jpeg");
        if (*stage == 0 && png == "com.openwave.photos") {
            qInfo("ASSOC_QA_PASS Files default arrived in Settings");
            *stage = 1; backend->setFileDefault("png", ""); backend->setFileDefault("jpg", "com.openwave.photos");
        } else if (*stage == 1 && jpg == "com.openwave.photos") {
            *stage = 2;
        } else if (*stage == 2 && jpg.isEmpty()) {
            qInfo("ASSOC_QA_PASS Files clear arrived in Settings");
            qInfo("ASSOC_QA_SETTINGS_DONE"); timer->stop();
        }
    });
    backend->refreshFileTypes(); timer->start(500);
}

template<class Opener>
inline void installFilesAssociationHarness(QObject *owner, ApplicationsBackend *backend, Opener *opener, const QString &home)
{
    if (!associationQaEnabled()) return;
    struct State { int stage = 0, ticks = 0; QString image, rpm, apk; bool pending = false; };
    auto state = std::make_shared<State>();
    const QString dir = home + "/Documents/AssociationQA";
    QDir().mkpath(dir);
    state->image = dir + "/test.png"; state->rpm = dir + "/invalid.rpm"; state->apk = dir + "/invalid.apk";
    QImage picture(16, 16, QImage::Format_RGB32); picture.fill(Qt::blue); picture.save(state->image);
    for (const auto &path : {state->rpm, state->apk}) { QFile f(path); if (f.open(QIODevice::WriteOnly)) f.write("not a package\n"); }
    auto *client = new ApplicationsClient(owner);
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [state, client, backend, opener, timer] {
        if (++state->ticks > 150) { qWarning("ASSOC_QA_FAIL Files timed out at stage %d: %s", state->stage, qPrintable(opener->message())); timer->stop(); return; }
        if (state->pending) return;
        const auto fail = [timer](const QString &message) { qWarning().noquote() << "ASSOC_QA_FAIL" << message; timer->stop(); };
        if (state->stage == 0) {
            state->pending = true;
            client->call("ClearFileDefaults", {"com.openwave.photos"}, [state, opener, fail](const QVariantList &, const QString &error) {
                state->pending = false;
                if (!error.isEmpty()) { fail(error); return; }
                state->stage = 1; opener->prepare(state->image);
            });
        } else if (state->stage == 1 && opener->ready()) {
            if (!opener->effectiveId().isEmpty() || opener->handlers().isEmpty()) { fail("First open did not require a compatible choice"); return; }
            qInfo("ASSOC_QA_PASS first open requires choice");
            state->stage = 2; opener->open(state->image, "com.openwave.photos", false);
        } else if (state->stage == 2 && !opener->busy()) {
            if (!opener->message().isEmpty()) { fail(opener->message()); return; }
            if (!associationQaDefault(backend->fileTypes(), "image/png").isEmpty()) { fail("Just Once saved a default"); return; }
            qInfo("ASSOC_QA_PASS Just Once preserved defaults");
            state->stage = 3; opener->open(state->image, "com.openwave.photos", true);
        } else if (state->stage == 3 && !opener->busy()) {
            if (!opener->message().isEmpty()) { fail(opener->message()); return; }
            qInfo("ASSOC_QA_PASS Always request succeeded"); state->stage = 4;
        } else if (state->stage == 4 && associationQaDefault(backend->fileTypes(), "image/png").isEmpty()
                   && associationQaDefault(backend->fileTypes(), "image/jpeg") == "com.openwave.photos") {
            qInfo("ASSOC_QA_PASS Settings defaults arrived in Files"); state->stage = 5; state->pending = true;
            QTimer::singleShot(3000, timer, [state, backend] { backend->setFileDefault("jpg", ""); state->pending = false; });
        } else if (state->stage == 5) {
            state->stage = 6; opener->clearMessage(); opener->open(state->rpm, "system.package-installer");
        } else if (state->stage == 6 && !opener->busy()) {
            if (!opener->message().contains("Not installed", Qt::CaseInsensitive)) { fail("Malformed RPM did not fail closed: " + opener->message()); return; }
            qInfo("ASSOC_QA_PASS malformed RPM rejected");
            state->stage = 7; opener->clearMessage(); opener->open(state->apk, "system.android-installer");
        } else if (state->stage == 7 && !opener->busy()) {
            if (!opener->message().contains("Not installed", Qt::CaseInsensitive)) { fail("Malformed APK did not fail closed: " + opener->message()); return; }
            qInfo("ASSOC_QA_PASS malformed APK rejected"); qInfo("ASSOC_QA_FILES_DONE"); timer->stop();
        }
    });
    timer->start(1000);
}
