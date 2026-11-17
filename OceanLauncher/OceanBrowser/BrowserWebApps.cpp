#include "BrowserWebApps.h"
#include "../../userspace/webapp/WebAppDefinition.h"
#include <QPointer>

BrowserWebApps::BrowserWebApps(QObject *parent) : QObject(parent)
{
    connect(&m_prompt, &OpenWavePromptClient::busyChanged, this, &BrowserWebApps::busyChanged);
    connect(&m_prompt, &OpenWavePromptClient::errorOccurred, this, &BrowserWebApps::errorOccurred);
    connect(&m_prompt, &OpenWavePromptClient::finished, this,
        [this](const QString &, const QString &result, const QString &text) {
        if (result != "accepted" || m_address.isEmpty()) { m_address.clear(); m_metadata.clear(); return; }
        m_name = text.trimmed();
        if (!OpenWaveWebApp::validName(m_name)) {
            m_address.clear(); m_metadata.clear();
            emit errorOccurred(tr("Enter a valid web app name."));
            return;
        }
        m_installing = true;
        emit busyChanged();
        m_icons.fetch(m_address, m_metadata);
    });
    connect(&m_icons, &OpenWave::WebsiteIconFetcher::finished, this, [this](const QByteArray &icon) {
        const QString name = m_name;
        const QUrl address = m_address;
        m_address.clear(); m_metadata.clear(); m_name.clear();
        QPointer<BrowserWebApps> guard(this);
        m_apps.createWebApp(name, address.toString(QUrl::FullyEncoded), icon,
            [guard, name](const QVariantList &reply, const QString &error) {
            if (!guard) return;
            guard->m_installing = false;
            emit guard->busyChanged();
            if (!error.isEmpty()) emit guard->errorOccurred(error);
            else if (reply.size() != 1 || reply.first().toString().isEmpty())
                emit guard->errorOccurred(tr("The application service returned an invalid web app."));
            else emit guard->created(reply.first().toString(), name);
        });
    });
}

bool BrowserWebApps::canInstall(const QString &url) const
{
    return !OpenWaveWebApp::address(url).isEmpty();
}

void BrowserWebApps::install(const QString &url, const QString &title, const QString &metadata)
{
    if (busy()) return;
    const auto address = OpenWaveWebApp::address(url);
    if (address.isEmpty()) {
        emit errorOccurred(tr("Only HTTP and HTTPS pages without embedded credentials can be installed."));
        return;
    }
    m_address = address;
    m_metadata = metadata.left(65536);
    m_prompt.textInput(tr("Install as web app"), address.toString(QUrl::FullyEncoded),
        {{"initialText", OpenWaveWebApp::suggestedName(title, address)}, {"placeholder", tr("Web app name")},
         {"minLength", 1}, {"maxLength", 256}, {"acceptLabel", tr("Create")}, {"cancelLabel", tr("Cancel")}});
}
