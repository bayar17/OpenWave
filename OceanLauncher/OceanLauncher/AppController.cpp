#include "AppController.h"
#include <OpenWave/preferences.h>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTimer>

AppController::AppController(QObject *parent)
    : QObject(parent), m_client(this), m_preferencesPath(openwavePreferencesPath())
{
    connect(&m_client, &ApplicationsClient::serviceChanged, this, [this](bool available) {
        m_registered = false;
        if (available) { refreshRegistry(); registerShell(); }
        else {
            if (!m_externalAppId.isEmpty()) emit launchFinished(m_externalAppId, QStringLiteral("The application manager restarted."));
            m_externalAppId.clear(); m_externalTicket.clear(); m_terminating.clear();
            const QStringList old = m_processIds.keys();
            m_processIds.clear(); m_pidApps.clear();
            for (const auto &id : old) {
                removeSnapshot(id); emit appProcessExited(id);
                emit launchFinished(id, QStringLiteral("The application manager restarted."));
            }
            emit runningAppsChanged();
        }
    });
    connect(&m_client, &ApplicationsClient::registryChanged, this, &AppController::refreshRegistry);
    connect(&m_client, &ApplicationsClient::applicationStarted, this, [this](const QString &id, quint32 pid) {
        m_processIds.insert(id, pid); m_pidApps.insert(pid, id); emit runningAppsChanged();
    });
    connect(&m_client, &ApplicationsClient::applicationStateChanged, this,
        [this](const QString &id, const QString &state, quint32 pid, const QString &error) {
            if (state == "Running" || state == "Starting" || state == "Stopping") {
                if (pid) { m_processIds.insert(id, pid); m_pidApps.insert(pid, id); }
            } else {
                const qint64 previous = m_processIds.take(id); m_pidApps.remove(previous);
                m_terminating.remove(id); removeSnapshot(id);
                if (previous) emit appProcessExited(id);
                if (id == "settings") emit preferencesChanged();
                emit launchFinished(id, error);
            }
            emit runningAppsChanged();
        });
    connect(&m_client, &ApplicationsClient::activationRequested, this, [this](const QString &id, quint32 pid) {
        m_processIds.insert(id, pid); m_pidApps.insert(pid, id);
        emit runningAppsChanged(); emit showExistingApp(id);
    });
    connect(&m_client, &ApplicationsClient::operationFailed, this, &AppController::launchFinished);
    connect(&m_client, &ApplicationsClient::launchRequested, this,
        [this](const QString &ticket, const QString &id, const QStringList &) {
            m_externalAppId = id; m_externalTicket = ticket; emit externalLaunchRequested(id);
        });
}

void AppController::setSessionReady(bool ready)
{
    m_waylandReady = ready;
    if (!m_registered) registerShell();
    else m_client.call("SetSessionReady", {ready}, [](const QVariantList &, const QString &) {});
}

void AppController::registerShell()
{
    if (m_registering || m_registered) return;
    m_registering = true;
    m_client.call("RegisterShell", {}, [this](const QVariantList &, const QString &error) {
        m_registering = false;
        if (!error.isEmpty()) { QTimer::singleShot(1000, this, &AppController::registerShell); return; }
        m_registered = true;
        m_client.call("SetSessionReady", {m_waylandReady}, [this](const QVariantList &values, const QString &failure) {
            if (!failure.isEmpty()) { m_registered = false; QTimer::singleShot(1000, this, &AppController::registerShell); }
            else if (m_waylandReady && !values.value(0).toBool()) {
                m_registered = false; QTimer::singleShot(200, this, &AppController::registerShell);
            }
        });
        m_client.getRunningApplications([this](const QVariantList &values, const QString &failure) {
            if (!failure.isEmpty()) return;
            m_processIds.clear(); m_pidApps.clear();
            for (const auto &value : values.value(0).toList()) {
                const auto info = value.toMap(); const QString id = info.value("appId").toString();
                const qint64 pid = info.value("pid").toUInt();
                m_processIds.insert(id, pid); m_pidApps.insert(pid, id);
            }
            emit runningAppsChanged();
        });
    });
}

void AppController::refreshRegistry()
{
    m_client.listApplications([this](const QVariantList &values, const QString &error) {
        if (!error.isEmpty()) return;
        QStringList installed; QVariantMap artwork;
        for (const auto &value : values.value(0).toList()) {
            const auto info = value.toMap(); const QString id = info.value("appId").toString();
            installed.append(id);
            artwork.insert(id, QStringList{info.value("name").toString(), info.value("icon").toString()});
        }
        installed.sort(); m_installedApps = installed; m_appArtwork = artwork;
        QSettings preferences(m_preferencesPath, QSettings::IniFormat);
        const QStringList favorites = m_layoutLoaded ? m_favoriteApps
            : preferences.contains("launcher/favorites") ? preferences.value("launcher/favorites").toStringList()
            : QStringList{"files", "settings", "browser"};
        const QStringList unpinned = m_layoutLoaded ? m_unpinnedApps : preferences.value("launcher/unpinned").toStringList();
        m_favoriteApps.clear(); m_unpinnedApps.clear();
        for (const auto &id : favorites) if (installed.contains(id) && !m_favoriteApps.contains(id)) m_favoriteApps.append(id);
        for (const auto &id : unpinned) if (installed.contains(id) && !m_favoriteApps.contains(id) && !m_unpinnedApps.contains(id)) m_unpinnedApps.append(id);
        for (const auto &id : installed) if (!m_favoriteApps.contains(id) && !m_unpinnedApps.contains(id)) m_unpinnedApps.append(id);
        m_layoutLoaded = true;
        emit registryChanged(); emit appLayoutChanged();
    });
}

bool AppController::moveApp(const QString &appId, bool toFavorites, int index)
{
    if (!installedApps().contains(appId))
        return false;
    QStringList favorites = m_favoriteApps;
    QStringList unpinned = m_unpinnedApps;
    const bool wasFavorite = favorites.contains(appId);
    const int oldIndex = wasFavorite ? favorites.indexOf(appId) : unpinned.indexOf(appId);
    favorites.removeAll(appId);
    unpinned.removeAll(appId);
    QStringList &destination = toFavorites ? favorites : unpinned;
    int insertion = index;
    if (wasFavorite == toFavorites && oldIndex >= 0 && oldIndex < index)
        --insertion;
    insertion = qBound(0, insertion, destination.size());
    destination.insert(insertion, appId);
    if (favorites == m_favoriteApps && unpinned == m_unpinnedApps)
        return true;

    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.setValue(QStringLiteral("launcher/favorites"), favorites);
    preferences.setValue(QStringLiteral("launcher/unpinned"), unpinned);
    preferences.sync();
    if (preferences.status() != QSettings::NoError)
        return false;
    m_favoriteApps = favorites;
    m_unpinnedApps = unpinned;
    emit appLayoutChanged();
    return true;
}

bool AppController::reduceMotion() const
{
    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.sync();
    return preferences.value(QStringLiteral("accessibility/reduceMotion"), false).toBool();
}

QString AppController::snapshotPath(const QString &appId)
{
    const qint64 pid = processId(appId);
    if (pid <= 0 || !m_installedApps.contains(appId)) return {};
    const QDir directory(QStringLiteral("/tmp/openwave/process/preview"));
    if (!directory.exists() && !QDir().mkpath(directory.path())) return {};
    const QString appName = appId.left(1).toUpper() + appId.mid(1);
    const QString path = directory.filePath(QStringLiteral("%1-%2.jpg").arg(pid).arg(appName));
    const QString previous = m_snapshotPaths.value(appId);
    if (!previous.isEmpty() && previous != path) QFile::remove(previous);
    m_snapshotPaths.insert(appId, path);
    return path;
}
void AppController::removeSnapshot(const QString &id)
{
    const QString path = m_snapshotPaths.take(id);
    if (!path.isEmpty()) QFile::remove(path);
}
qint64 AppController::processId(const QString &id) const { return m_processIds.value(id); }
QString AppController::appIdForProcess(qint64 pid) const { return m_pidApps.value(pid); }
void AppController::launch(const QString &id)
{
    m_client.launch(id, {}, [this, id](const QVariantList &values, const QString &error) {
        if (!error.isEmpty()) emit launchFinished(id, error);
        else if (values.value(0).toUInt()) {
            const qint64 pid = values.value(0).toUInt(); m_processIds.insert(id, pid); m_pidApps.insert(pid, id);
            emit runningAppsChanged();
        }
    });
}
void AppController::launchExternalApp(const QString &id)
{
    if (id != m_externalAppId || m_externalTicket.isEmpty()) return;
    const QString ticket = m_externalTicket; m_externalAppId.clear(); m_externalTicket.clear();
    m_client.call("CommitLaunch", {ticket}, [this, id](const QVariantList &values, const QString &error) {
        if (!error.isEmpty()) emit launchFinished(id, error);
        else {
            const qint64 pid = values.value(0).toUInt(); m_processIds.insert(id, pid); m_pidApps.insert(pid, id);
            emit runningAppsChanged();
        }
    });
}
void AppController::cancelExternalLaunch(const QString &id)
{
    if (id != m_externalAppId || m_externalTicket.isEmpty()) return;
    const QString ticket = m_externalTicket; m_externalAppId.clear(); m_externalTicket.clear();
    m_client.call("CancelLaunch", {ticket}, [](const QVariantList &, const QString &) {});
}
bool AppController::killApp(const QString &id)
{
    if (!processId(id) || m_terminating.contains(id)) return false;
    m_terminating.insert(id);
    m_client.terminate(id, [this, id](const QVariantList &, const QString &error) {
        if (!error.isEmpty()) { m_terminating.remove(id); emit terminationFailed(id, error); }
    });
    return true;
}
