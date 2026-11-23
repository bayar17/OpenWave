#include "UninstallController.h"
#include "AppController.h"
#include <QUuid>
#include <QDebug>

void UninstallController::ask(const QString &appId, const QString &name)
{
    if (m_busy || !m_request.isEmpty() || appId.isEmpty() || !m_apps->canUninstall(appId)) return;
    m_appId = appId;
    m_request = {{"id", QUuid::createUuid().toString()}, {"type", "confirmation"},
        {"title", tr("Uninstall %1?").arg(name.left(80))},
        {"message", tr("This will permanently remove the app and its data, settings and cache. Files saved in shared folders will be kept.")},
        {"acceptLabel", tr("Uninstall")}, {"cancelLabel", tr("Cancel")}};
    emit changed();
}

void UninstallController::respond(const QString &id, const QString &result, const QString &value)
{
    Q_UNUSED(value)
    if (id.isEmpty() || id != m_request.value("id").toString() || m_busy) return;
    if (result != "accepted" && result != "cancelled") return;
    const QString appId = m_appId;
    m_request.clear();
    m_busy = result == "accepted";
    emit changed();
    if (!m_busy) return;
    qInfo().noquote() << "UNINSTALL_UI accepted app=" + appId;
    auto *connection = new QMetaObject::Connection;
    *connection = connect(m_apps, &AppController::uninstallFinished, this, [this, connection, appId](const QString &finished, const QString &error) {
        if (finished != appId) return;
        disconnect(*connection); delete connection;
        m_busy = false; emit changed();
        if (!error.isEmpty()) {
            qWarning().noquote() << "Uninstall failed:" << error;
            emit errorOccurred(tr("Could not uninstall the app. Please try again."));
        }
    });
    m_apps->uninstall(appId);
}
