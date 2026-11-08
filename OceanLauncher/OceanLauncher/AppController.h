#pragma once

#include <QObject>
#include <OpenWave/ApplicationsClient.h>
#include <QVariantMap>
#include <QHash>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QSet>

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionStateChanged)
    Q_PROPERTY(QStringList installedApps READ installedApps NOTIFY registryChanged)
    Q_PROPERTY(QVariantMap appArtwork READ appArtwork NOTIFY registryChanged)
    Q_PROPERTY(QStringList favoriteApps READ favoriteApps NOTIFY appLayoutChanged)
    Q_PROPERTY(QStringList unpinnedApps READ unpinnedApps NOTIFY appLayoutChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY preferencesChanged)
    Q_PROPERTY(QStringList quickMenuOrder READ quickMenuOrder NOTIFY preferencesChanged)
    Q_PROPERTY(QStringList quickMenuHiddenControls READ quickMenuHiddenControls NOTIFY preferencesChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    QStringList installedApps() const { return m_installedApps; }
    QVariantMap appArtwork() const { return m_appArtwork; }
    Q_INVOKABLE void setSessionReady(bool ready);
    void setGraphicsReady(bool ready);
    QString sessionState() const { return m_sessionState; }
    Q_INVOKABLE QString appIdForProcess(qint64 pid) const;
    QStringList favoriteApps() const { return m_favoriteApps; }
    QStringList unpinnedApps() const { return m_unpinnedApps; }
    bool reduceMotion() const;
    QStringList quickMenuOrder() const;
    QStringList quickMenuHiddenControls() const;
    Q_INVOKABLE bool setQuickMenuOrder(const QStringList &order);
    Q_INVOKABLE bool setQuickMenuHiddenControls(const QStringList &hidden);
    Q_INVOKABLE bool moveApp(const QString &appId, bool toFavorites, int index);
    Q_INVOKABLE void launch(const QString &appId);
    Q_INVOKABLE void launchExternalApp(const QString &appId);
    Q_INVOKABLE void cancelExternalLaunch(const QString &appId);
    Q_INVOKABLE bool killApp(const QString &appId);
    Q_INVOKABLE qint64 processId(const QString &appId) const;
    Q_INVOKABLE QString snapshotPath(const QString &appId);
    Q_INVOKABLE void removeSnapshot(const QString &appId);
signals:
    void sessionStateChanged();
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
    void announceSessionReady();
    void setStartupState(const QString &state);
    bool m_graphicsReady = false;
    bool m_announcing = false;
    QString m_sessionState = QStringLiteral("initializing");
    void refreshRegistry();
    void scheduleRegistryRetry();
    void watchPreferences();
    void checkQuickMenuPreferences();
    ApplicationsClient m_client;
    QString m_preferencesPath;
    QFileSystemWatcher m_preferencesWatcher;
    QStringList m_observedQuickMenuOrder;
    QStringList m_observedQuickMenuHidden;
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
    bool m_registryRetryScheduled = false;
    bool m_registryRefreshInFlight = false;
    int m_registryRetryDelayMs = 1000;
};
