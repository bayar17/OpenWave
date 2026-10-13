#pragma once
#include <QObject>
#include <QVariantMap>

class OpenWavePortals;

// Persistent UI preferences use the application's resolved private data directory.
// Runtime services remain responsible for downloads, cookies and portal grants.
class BrowserPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool askOnDownload READ askOnDownload WRITE setAskOnDownload NOTIFY changed)
    Q_PROPERTY(bool startPrivate READ startPrivate WRITE setStartPrivate NOTIFY changed)
    Q_PROPERTY(bool rememberTabs READ rememberTabs WRITE setRememberTabs NOTIFY changed)
    Q_PROPERTY(bool alwaysShowFullUrl READ alwaysShowFullUrl WRITE setAlwaysShowFullUrl NOTIFY changed)
    Q_PROPERTY(bool rememberHistory READ rememberHistory WRITE setRememberHistory NOTIFY changed)
    Q_PROPERTY(bool blockThirdPartyCookies READ blockThirdPartyCookies WRITE setBlockThirdPartyCookies NOTIFY changed)
    Q_PROPERTY(QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY changed)
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY changed)
    Q_PROPERTY(bool choosingFolder READ choosingFolder NOTIFY choosingFolderChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    BrowserPreferences(const QString &applicationPath, const QString &userPath, QObject *parent = nullptr);
    bool askOnDownload() const { return m_values.value("askOnDownload", true).toBool(); }
    bool startPrivate() const { return m_values.value("startPrivate", false).toBool(); }
    bool rememberTabs() const { return m_values.value("rememberTabs", true).toBool(); }
    bool alwaysShowFullUrl() const { return m_values.value("alwaysShowFullUrl", true).toBool(); }
    bool rememberHistory() const { return m_values.value("rememberHistory", true).toBool(); }
    bool blockThirdPartyCookies() const { return m_values.value("blockThirdPartyCookies", true).toBool(); }
    QString searchEngine() const { return m_values.value("searchEngine", "google").toString(); }
    QString downloadFolder() const { return m_values.value("downloadFolder", m_user + "/Downloads").toString(); }
    bool choosingFolder() const;
    QString error() const { return m_error; }
    void setAskOnDownload(bool value) { setValue("askOnDownload", value); }
    void setStartPrivate(bool value) { setValue("startPrivate", value); }
    void setRememberTabs(bool value) { setValue("rememberTabs", value); }
    void setAlwaysShowFullUrl(bool value) { setValue("alwaysShowFullUrl", value); }
    void setRememberHistory(bool value) { setValue("rememberHistory", value); }
    void setBlockThirdPartyCookies(bool value) { setValue("blockThirdPartyCookies", value); }
    void setSearchEngine(const QString &value);
    Q_INVOKABLE QString searchUrl(const QString &query) const;
    Q_INVOKABLE void chooseDownloadFolder();
signals:
    void changed();
    void choosingFolderChanged();
    void errorChanged();
private:
    void setValue(const QString &key, const QVariant &value);
    void reportError(const QString &message);
    bool safeStorage() const;
    QString m_file, m_user, m_error;
    QVariantMap m_values;
    OpenWavePortals *m_portal;
};
