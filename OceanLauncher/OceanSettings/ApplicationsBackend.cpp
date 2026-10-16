#include "ApplicationsBackend.h"
#include <QCollator>
#include <algorithm>

ApplicationsBackend::ApplicationsBackend(QObject *parent) : QObject(parent)
{
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
