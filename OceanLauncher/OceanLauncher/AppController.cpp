#include <QRegularExpression>
#include <OpenWave/application-id.h>
#include "AppController.h"
#include "ApplicationMetadata.h"
#include <OpenWave/preferences.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QDebug>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace {
const QStringList quickMenuDefaults{
    QStringLiteral("wifi"), QStringLiteral("bluetooth"),
    QStringLiteral("airplane"), QStringLiteral("lock"),
    QStringLiteral("volume"), QStringLiteral("brightness")
};
const QStringList legacyQuickMenuDefaults{
    QStringLiteral("wifi"), QStringLiteral("bluetooth"),
    QStringLiteral("airplane"), QStringLiteral("volume"),
    QStringLiteral("brightness"), QStringLiteral("lock")
};

QStringList normalizeQuickMenuOrder(const QStringList &saved)
{
    if (saved == legacyQuickMenuDefaults)
        return quickMenuDefaults;
    QStringList result;
    for (const QString &id : saved)
        if (quickMenuDefaults.contains(id) && !result.contains(id))
            result.append(id);
    for (const QString &id : quickMenuDefaults)
        if (!result.contains(id)) result.append(id);
    return result;
}

QStringList normalizeQuickMenuHidden(const QStringList &saved)
{
    QStringList result;
    for (const QString &id : saved)
        if (quickMenuDefaults.contains(id) && !result.contains(id))
            result.append(id);
    return result;
}
}

AppController::AppController(QObject *parent)
    : QObject(parent), m_client(this), m_preferencesPath(openwavePreferencesPath())
{
    m_observedQuickMenuOrder = quickMenuOrder();
    m_observedQuickMenuHidden = quickMenuHiddenControls();
    watchPreferences();
    const auto schedulePreferenceCheck = [this] {
        QTimer::singleShot(40, this, &AppController::checkQuickMenuPreferences);
    };
    connect(&m_preferencesWatcher, &QFileSystemWatcher::fileChanged, this,
            [schedulePreferenceCheck](const QString &) { schedulePreferenceCheck(); });
    connect(&m_preferencesWatcher, &QFileSystemWatcher::directoryChanged, this,
            [schedulePreferenceCheck](const QString &) { schedulePreferenceCheck(); });
    connect(&m_client, &ApplicationsClient::serviceChanged, this, [this](bool available) {
        if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
            qInfo() << "OCEAN_QA appd-service-available=" << available;
        m_registered = false;
        setStartupState(QStringLiteral("initializing"));
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
                if (id == "com.openwave.settings") emit preferencesChanged();
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

    // Fetch the current catalog after all signal connections are installed;
    // this avoids depending on an edge-triggered availability notification
    // for the initial model load. ApplicationsClient still verifies the
    // authenticated service identity on the request.
    QTimer::singleShot(250, this, [this] { refreshRegistry(); });
}

void AppController::watchPreferences()
{
    const QFileInfo preferences(m_preferencesPath);
    const QString directory = preferences.absolutePath();
    if (QDir(directory).exists() && !m_preferencesWatcher.directories().contains(directory))
        m_preferencesWatcher.addPath(directory);
    if (QFile::exists(m_preferencesPath) && !m_preferencesWatcher.files().contains(m_preferencesPath))
        m_preferencesWatcher.addPath(m_preferencesPath);
}

void AppController::checkQuickMenuPreferences()
{
    watchPreferences();
    const QStringList order = quickMenuOrder();
    const QStringList hidden = quickMenuHiddenControls();
    if (order == m_observedQuickMenuOrder && hidden == m_observedQuickMenuHidden)
        return;
    m_observedQuickMenuOrder = order;
    m_observedQuickMenuHidden = hidden;
    emit preferencesChanged();
}

void AppController::setStartupState(const QString &state)
{
    if (m_sessionState == state) return;
    m_sessionState = state;
    qInfo().noquote() << "Ocean startup:" << state;
    emit sessionStateChanged();
}

void AppController::setSessionReady(bool ready)
{
    if (m_waylandReady == ready) return;
    m_waylandReady = ready;
    announceSessionReady();
}

void AppController::setGraphicsReady(bool ready)
{
    if (m_graphicsReady == ready) return;
    m_graphicsReady = ready;
    announceSessionReady();
}

void AppController::announceSessionReady()
{
    if (m_announcing) return;
    const bool ready = m_waylandReady && m_graphicsReady;
    if (ready) {
        const QByteArray path = QFile::encodeName(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/wayland-0"));
        struct stat st;
        if (lstat(path.constData(), &st) || !S_ISSOCK(st.st_mode) || st.st_uid != getuid()) {
            qWarning() << "Ocean startup: compositor socket missing or wrong owner" << path;
            setStartupState(QStringLiteral("failed"));
            return;
        }
        setStartupState(QStringLiteral("compositor-ready"));
    }
    if (!m_registered) { registerShell(); return; }
    m_announcing = true;
    m_client.call("SetSessionReady", {ready}, [this, ready](const QVariantList &values, const QString &failure) {
        m_announcing = false;
        if (!failure.isEmpty() || (ready && !values.value(0).toBool())) {
            qWarning() << "Ocean startup: readiness handshake failed:" << failure;
            setStartupState(QStringLiteral("failed"));
            m_registered = false;
            QTimer::singleShot(500, this, &AppController::registerShell);
            return;
        }
        setStartupState(ready ? QStringLiteral("session-ready") : QStringLiteral("initializing"));
        if (ready != (m_waylandReady && m_graphicsReady)) announceSessionReady();
    });
}

void AppController::registerShell()
{
    if (m_registering || m_registered) return;
    m_registering = true;
    m_client.call("RegisterShell", {}, [this](const QVariantList &, const QString &error) {
        m_registering = false;
        if (!error.isEmpty()) { qWarning() << "Ocean startup: RegisterShell failed:" << error;
            QTimer::singleShot(1000, this, &AppController::registerShell); return; }
        m_registered = true;
        announceSessionReady();
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
    if (m_registryRefreshInFlight)
        return;
    m_registryRefreshInFlight = true;
    const bool qa = qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1");
    if (qa)
        std::fprintf(stderr, "OCEAN_QA AppController ListApplications stage=begin\n");
    m_client.listApplications([this](const QVariantList &values, const QString &error) {
        m_registryRefreshInFlight = false;
        if (!error.isEmpty()) {
            qWarning() << "Ocean registry unavailable:" << error;
            if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
                std::fprintf(stderr, "OCEAN_QA AppController ListApplications stage=method-call failed: %s\n",
                             qPrintable(error));
            scheduleRegistryRetry();
            return;
        }
        m_registryRetryDelayMs = 1000;
        QStringList installed; QVariantMap artwork;
        const auto entries = values.value(0).toList();
        for (const auto &value : entries) {
            const auto info = value.toMap(); const QString id = info.value("appId").toString();
            const bool launchable = oceanLaunchableApplication(info);
            if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
                qInfo().noquote() << "OCEAN_QA catalog-entry appId=" << id
                                  << "name=" << info.value("name").toString()
                                  << "icon-present=" << !info.value("icon").toString().isEmpty()
                                  << "launchable=" << launchable
                                  << "owner=" << info.value("owner").toUInt();
            // Android catalog records stay hidden until Ocean has a verified
            // native surface handoff and appd marks their provider launchable.
            if (!launchable) continue;
            installed.append(id);
            artwork.insert(id, QStringList{info.value("name").toString(), info.value("icon").toString()});
        }
        installed.sort(); m_installedApps = installed; m_appArtwork = artwork;
        QSettings preferences(m_preferencesPath, QSettings::IniFormat);
        QStringList favorites = m_layoutLoaded ? m_favoriteApps
            : preferences.contains("launcher/favorites") ? preferences.value("launcher/favorites").toStringList()
            : QStringList{"com.openwave.files", "com.openwave.settings", "com.openwave.ocean"};
        QStringList unpinned = m_layoutLoaded ? m_unpinnedApps : preferences.value("launcher/unpinned").toStringList();
        for (auto &id : favorites) id = openwaveCanonicalApplicationId(id);
        for (auto &id : unpinned) id = openwaveCanonicalApplicationId(id);
        m_favoriteApps.clear(); m_unpinnedApps.clear();
        for (const auto &id : favorites) if (installed.contains(id) && !m_favoriteApps.contains(id)) m_favoriteApps.append(id);
        for (const auto &id : unpinned) if (installed.contains(id) && !m_favoriteApps.contains(id) && !m_unpinnedApps.contains(id)) m_unpinnedApps.append(id);
        for (const auto &id : installed) if (!m_favoriteApps.contains(id) && !m_unpinnedApps.contains(id)) m_unpinnedApps.append(id);
        if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
            std::fprintf(stderr, "OCEAN_QA AppController model decoded=%lld launchable=%lld favorites=%lld unpinned=%lld artwork=%lld\n",
                         static_cast<long long>(entries.size()),
                         static_cast<long long>(installed.size()),
                         static_cast<long long>(m_favoriteApps.size()),
                         static_cast<long long>(m_unpinnedApps.size()),
                         static_cast<long long>(artwork.size()));
        preferences.setValue("launcher/favorites", m_favoriteApps);
        preferences.setValue("launcher/unpinned", m_unpinnedApps);
        m_layoutLoaded = true;
        emit registryChanged(); emit appLayoutChanged();
    });
}

void AppController::scheduleRegistryRetry()
{
    if (m_registryRetryScheduled)
        return;
    m_registryRetryScheduled = true;
    const int delay = m_registryRetryDelayMs;
    m_registryRetryDelayMs = qMin(m_registryRetryDelayMs * 2, 10000);
    QTimer::singleShot(delay, this, [this] {
        m_registryRetryScheduled = false;
        refreshRegistry();
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

QStringList AppController::quickMenuOrder() const
{
    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.sync();
    return normalizeQuickMenuOrder(preferences.value(QStringLiteral("quickmenu/order"),
        quickMenuDefaults).toStringList());
}

QStringList AppController::quickMenuHiddenControls() const
{
    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.sync();
    return normalizeQuickMenuHidden(preferences.value(
        QStringLiteral("quickmenu/hidden"), QStringList{}).toStringList());
}

bool AppController::setQuickMenuOrder(const QStringList &order)
{
    if (order.size() != quickMenuDefaults.size()) return false;
    QSet<QString> ids;
    for (const QString &id : order) {
        if (!quickMenuDefaults.contains(id) || ids.contains(id)) return false;
        ids.insert(id);
    }
    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.setValue(QStringLiteral("quickmenu/order"), order);
    preferences.sync();
    if (preferences.status() != QSettings::NoError) return false;
    m_observedQuickMenuOrder = order;
    emit preferencesChanged();
    return true;
}

bool AppController::setQuickMenuHiddenControls(const QStringList &hidden)
{
    QSet<QString> ids;
    for (const QString &id : hidden) {
        if (!quickMenuDefaults.contains(id) || ids.contains(id)) return false;
        ids.insert(id);
    }
    QSettings preferences(m_preferencesPath, QSettings::IniFormat);
    preferences.setValue(QStringLiteral("quickmenu/hidden"), hidden);
    preferences.sync();
    if (preferences.status() != QSettings::NoError) return false;
    m_observedQuickMenuHidden = hidden;
    emit preferencesChanged();
    return true;
}

QString AppController::snapshotPath(const QString &appId)
{
    const qint64 pid = processId(appId);
    if (pid <= 0 || !m_installedApps.contains(appId)) return {};
    const QDir directory(QStringLiteral("/tmp/openwave/process/preview"));
    if (!directory.exists() && !QDir().mkpath(directory.path())) return {};
    QString appName = m_appArtwork.value(appId).toStringList().value(0, appId).left(80);
    appName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
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
    if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
        qInfo().noquote() << "OCEAN_QA AppController launch-request appId=" << id;
    m_client.launch(id, {}, [this, id](const QVariantList &values, const QString &error) {
        if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
            qInfo().noquote() << "OCEAN_QA launch-result appId=" << id
                              << "pid=" << values.value(0).toUInt()
                              << "error=" << (error.isEmpty() ? QStringLiteral("none") : error);
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
        if (qEnvironmentVariable("OPENWAVE_QA_GRAPHICS") == QStringLiteral("1"))
            qInfo().noquote() << "OCEAN_QA launch-commit appId=" << id
                              << "pid=" << values.value(0).toUInt()
                              << "error=" << (error.isEmpty() ? QStringLiteral("none") : error);
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
