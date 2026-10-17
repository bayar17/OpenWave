#pragma once

#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/OpenWavePromptClient.h>
#include <QUrl>
#include <OpenWave/WebsiteIconFetcher.h>

class BrowserWebApps : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString iconScript READ iconScript CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit BrowserWebApps(QObject *parent = nullptr);
    QString iconScript() const { return OpenWave::WebsiteIconFetcher::discoveryScript(); }
    bool busy() const { return m_prompt.busy() || m_installing; }
    Q_INVOKABLE bool canInstall(const QString &url) const;
    Q_INVOKABLE void install(const QString &url, const QString &title, const QString &metadata);
signals:
    void busyChanged();
    void created(const QString &appId, const QString &name);
    void errorOccurred(const QString &message);
private:
    OpenWavePromptClient m_prompt;
    ApplicationsClient m_apps;
    QUrl m_address;
    QString m_metadata;
    QString m_name;
    OpenWave::WebsiteIconFetcher m_icons;
    bool m_installing = false;
};
