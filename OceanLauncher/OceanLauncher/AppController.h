#pragma once

#include <QObject>
#include <OpenWave/ApplicationsClient.h>
#include <QVariantMap>
#include <QHash>
#include <QStringList>
#include <QSet>

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList installedApps READ installedApps NOTIFY registryChanged)
    Q_PROPERTY(QVariantMap appArtwork READ appArtwork NOTIFY registryChanged)
    Q_PROPERTY(QStringList favoriteApps READ favoriteApps NOTIFY appLayoutChanged)
    Q_PROPERTY(QStringList unpinnedApps READ unpinnedApps NOTIFY appLayoutChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY preferencesChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    QStringList installedApps() const { return m_installedApps; }
    QVariantMap appArtwork() const { return m_appArtwork; }
    Q_INVOKABLE void setSessionReady(bool ready);
    Q_INVOKABLE QString appIdForProcess(qint64 pid) const;
    QStringList favoriteApps() const { return m_favoriteApps; }
    QStringList unpinnedApps() const { return m_unpinnedApps; }
    bool reduceMotion() const;
    Q_INVOKABLE bool moveApp(const QString &appId, bool toFavorites, int index);
    Q_INVOKABLE void launch(const QString &appId);
    Q_INVOKABLE void launchExternalApp(const QString &appId);
    Q_INVOKABLE void cancelExternalLaunch(const QString &appId);
    Q_INVOKABLE bool killApp(const QString &appId);
    Q_INVOKABLE qint64 processId(const QString &appId) const;
    Q_INVOKABLE QString snapshotPath(const QString &appId);
    Q_INVOKABLE void removeSnapshot(const QString &appId);
signals:
    void launchFinished(const QString &appId, const QString &errorMessage);
    void showExistingApp(const QString &appId);
    void externalLaunchRequested(const QString &appId);
    void appProcessExited(const QString &appId);
    void terminationFailed(const QString &appId, const QString &message);
    void preferencesChanged();
    void appLayoutChanged();
    void registryChanged();
    void runningAppsChanged();
private:
    void registerShell();
    void refreshRegistry();
    ApplicationsClient m_client;
    QString m_preferencesPath;
    QStringList m_installedApps;
    QVariantMap m_appArtwork;
    QStringList m_favoriteApps;
    QStringList m_unpinnedApps;
    QHash<QString, QString> m_snapshotPaths;
    QHash<QString, qint64> m_processIds;
    QHash<qint64, QString> m_pidApps;
    QSet<QString> m_terminating;
    QString m_externalAppId;
    QString m_externalTicket;
    bool m_layoutLoaded = false;
    bool m_waylandReady = false;
    bool m_registered = false;
    bool m_registering = false;
};
