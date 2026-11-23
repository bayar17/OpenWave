#pragma once
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <OpenWave/PromptRendererBackend.h>
#include <memory>

inline void installBrowserActionsShellHarness(QObject *owner, QQmlApplicationEngine *engine,
    PromptRendererBackend *prompts, PortalFileChooserBackend *portal)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly) || !cmdline.readAll().trimmed().split(' ').contains("openwave.qa=browser-actions")) return;
    struct State { int install = 0, save = 0, uninstall = 0; QSet<QString> answered, verified; };
    auto state = std::make_shared<State>();
    auto *timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [=] {
        const auto request = prompts->request();
        const QString id = request.value("id").toString();
        if (!id.isEmpty() && !prompts->busy() && request.value("applicationId").toString() == "com.openwave.settings"
            && request.value("title").toString() == "Uninstall OpenWave Browser QA Site?"
            && !state->answered.contains(id) && !engine->rootObjects().isEmpty()) {
            auto *root = engine->rootObjects().first();
            auto *dialog = root->findChild<QObject *>("systemPrompt");
            if (dialog && dialog->property("phase").toString() == "open") {
                auto *button = dialog->findChild<QObject *>(state->uninstall == 0 ? "systemPromptCancel" : "systemPromptAccept");
                if (button && button->property("enabled").toBool()) {
                    state->answered.insert(id); ++state->uninstall;
                    qInfo() << "UNINSTALL_QA visible confirmation" << state->uninstall;
                    powerQaClick(root, button);
                }
            }
        }
        if (!id.isEmpty() && !prompts->busy() && request.value("applicationId").toString() == "com.openwave.ocean" && !state->answered.contains(id)) {
            const QString type = request.value("type").toString();
            if (type == "selection" && request.value("choices").toMap().contains("allow_application")) {
                state->answered.insert(id); prompts->respond(id, "accepted", "allow_application");
                qInfo() << "BROWSER_QA permission prompt visible: files.write";
            } else if (type == "text_input" && request.value("title").toString() == "Install as web app") {
                state->answered.insert(id);
                qInfo() << "BROWSER_QA install prompt visible action=" << state->install;
                if (state->install++ == 0) prompts->respond(id, "cancelled");
                else prompts->respond(id, "accepted", "OpenWave Browser QA Site");
            }
        }
        if (!portal->requestId().isEmpty() && !portal->busy() && portal->applicationId() == "com.openwave.ocean" && !state->answered.contains(portal->requestId()) && !engine->rootObjects().isEmpty()) {
            auto *picker = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("portalFileChooser"));
            if (picker && picker->property("phase").toString() == "open" && picker->property("displayedRequestId").toString() == portal->requestId()) {
                state->answered.insert(portal->requestId());
                if (state->save++ == 0) { qInfo() << "BROWSER_QA save picker visible: cancel"; portal->cancel(); }
                else { qInfo() << "BROWSER_QA save picker visible: choose"; portal->choose({"/Data/Users/ocean/Downloads/ow-browser-actions-qa.bin"}); }
            }
        }
        const QByteArray expected = QByteArray("OpenWave browser download fixture 38\n").repeated(1024);
        for (const QString &name : {QStringLiteral("ow-browser-actions-qa.bin"), QStringLiteral("ow-browser-actions-qa (1).bin")}) {
            if (state->verified.contains(name)) continue;
            QFile file(QStringLiteral("/Data/Users/ocean/Downloads/") + name);
            if (file.open(QIODevice::ReadOnly) && file.readAll() == expected && QFileInfo(file).ownerId() == 1000 && (QFileInfo(file).permissions() & (QFile::ReadGroup | QFile::ReadOther)) == 0) {
                state->verified.insert(name);
                qInfo().noquote() << "BROWSER_QA_PASS saved payload and owner" << name;
            }
        }
    });
    timer->start(150);
}
