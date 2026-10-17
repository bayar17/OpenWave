#include "ApplicationsBackend.h"
#include <QCollator>
#include <QPointer>
#include <algorithm>

ApplicationsBackend::ApplicationsBackend(QObject *parent) : QObject(parent)
{
    connect(&m_prompt, &OpenWavePromptClient::busyChanged, this, &ApplicationsBackend::changed);
    connect(&m_prompt, &OpenWavePromptClient::errorOccurred, this, [this](const QString &error) { m_uninstallError = error; emit changed(); });
    connect(&m_prompt, &OpenWavePromptClient::finished, this, [this](const QString &, const QString &result, const QString &) {
        const QString id = m_pendingUninstall;
        m_pendingUninstall.clear();
        if (result != "accepted" || id.isEmpty()) return;
        m_uninstalling = true; m_uninstallError.clear(); emit changed();
        QPointer<ApplicationsBackend> guard(this);
        m_client.uninstallWebApp(id, [guard, id](const QVariantList &, const QString &error) {
            if (!guard) return;
            guard->m_uninstalling = false;
            guard->m_uninstallError = error;
            emit guard->changed();
            if (error.isEmpty()) { emit guard->uninstalled(id); guard->refresh(); }
        });
    });
    connect(&m_client, &ApplicationsClient::registryChanged, this, &ApplicationsBackend::refresh);
    connect(&m_client, &ApplicationsClient::serviceChanged, this, [this](bool) { refresh(); });
}
void ApplicationsBackend::refresh()
{
    if (m_busy) { m_again = true; return; }
    m_busy = true; m_error.clear(); emit changed();
    m_client.listApplications([this](const QVariantList &values, const QString &error) {
        m_error = error;
        m_applications = error.isEmpty() ? values.value(0).toList() : QVariantList();
        QCollator collator; collator.setCaseSensitivity(Qt::CaseInsensitive); collator.setNumericMode(true);
        std::sort(m_applications.begin(), m_applications.end(), [&](const QVariant &a, const QVariant &b) {
            const auto first = a.toMap(), second = b.toMap();
            const int result = collator.compare(first.value("name").toString(), second.value("name").toString());
            return result ? result < 0 : first.value("appId").toString() < second.value("appId").toString();
        });
        m_busy = false; emit changed();
        if (m_again) { m_again = false; refresh(); }
    });
}

void ApplicationsBackend::requestUninstall(const QString &id)
{
    if (uninstallBusy()) return;
    for (const auto &entry : m_applications) {
        const auto application = entry.toMap();
        if (application.value("appId").toString() != id || !application.value("canUninstall").toBool()) continue;
        m_pendingUninstall = id;
        m_uninstallError.clear(); emit changed();
        m_prompt.confirmation(tr("Uninstall %1?").arg(application.value("name").toString()),
            tr("This will permanently remove the web app, cookies, website data, settings, and cache. Files saved in shared folders will be kept."),
            {{"acceptLabel", tr("Uninstall")}, {"cancelLabel", tr("Cancel")}});
        return;
    }
}
