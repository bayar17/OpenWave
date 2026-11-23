#pragma once
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <sys/stat.h>
#include <unistd.h>

inline void installPermissionHarness(QObject *owner, AppController *controller,
    PromptRendererBackend *prompts, PortalFileChooserBackend *portal)
{
    QObject::connect(controller, &AppController::launchFinished, owner, [](const QString &id, const QString &error) { qWarning().noquote() << "PERMISSION_QA_LAUNCH" << id << error; });
    QTimer::singleShot(8000, owner, [controller] { controller->launch("com.openwave.settings"); });
    auto *timer = new QTimer(owner);
    auto *answered = new QSet<QString>();
    QObject::connect(owner, &QObject::destroyed, [answered] { delete answered; });
    QObject::connect(timer, &QTimer::timeout, owner, [prompts, portal, answered] {
        const auto request = prompts->request(); const auto id = request.value("id").toString();
        if (!id.isEmpty() && request.value("applicationId").toString().startsWith("org.openwave.qa.permission-") && !answered->contains(id)) {
            const QString appPath = "/Data/Users/root/.DataLocation/Applications/" + request.value("applicationId").toString();
            QFile visible(appPath + "/qa-visible-prompt");
            if (visible.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                visible.write(id.toUtf8()); visible.close(); struct stat st;
                if (!stat(QFile::encodeName(appPath).constData(), &st)) chown(QFile::encodeName(visible.fileName()).constData(), st.st_uid, st.st_gid);
            }
            QFile control("/tmp/permission-qa-choice");
            if (control.open(QIODevice::ReadOnly)) {
                const auto choice = QString::fromUtf8(control.readAll()).trimmed();
                if (choice != "hold") {
                    answered->insert(id);
                    QFile log("/tmp/permission-qa-prompts");
                    if (log.open(QIODevice::Append)) log.write(id.toUtf8() + '\n');
                    QTimer::singleShot(250, prompts, [prompts, id, choice] { prompts->respond(id, "accepted", choice); });
                }
            }
        }
        if (!portal->requestId().isEmpty() && portal->applicationId().startsWith("org.openwave.qa.permission-") && !portal->busy()) {
            QFile hold("/tmp/permission-qa-picker-hold");
            if (hold.exists()) return;
            if (portal->mode() == "SelectDirectory") portal->choose({"/Data/Users/root/Documents"});
            else if (portal->mode() == "SaveFile") portal->choose({"/Data/Users/root/Documents/portal-saved.txt"});
            else portal->choose({"/Data/Users/root/Documents/permission-test.txt"});
        }
    });
    timer->start(100);
}
