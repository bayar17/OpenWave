#pragma once
#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/WebsiteIconFetcher.h>

class WebAppAppearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString iconScript READ iconScript CONSTANT)
public:
    WebAppAppearance(const QUrl &url, bool needsIcon, QObject *parent = nullptr)
        : QObject(parent), m_url(url), m_needsIcon(needsIcon)
    {
        connect(&m_icons, &OpenWave::WebsiteIconFetcher::finished, this, [this](const QByteArray &png) {
            if (png.isEmpty()) return;
            m_apps.updateWebAppIcon(png, [](const QVariantList &, const QString &error) {
                if (!error.isEmpty()) qWarning("Website icon: %s", qPrintable(error));
            });
        });
    }
    QString iconScript() const { return OpenWave::WebsiteIconFetcher::discoveryScript(); }
    Q_INVOKABLE void refreshIcon(const QString &metadata) {
        if (!m_needsIcon) return;
        m_needsIcon = false;
        m_icons.fetch(m_url, metadata);
    }
private:
    QUrl m_url;
    bool m_needsIcon;
    OpenWave::WebsiteIconFetcher m_icons;
    ApplicationsClient m_apps;
};
