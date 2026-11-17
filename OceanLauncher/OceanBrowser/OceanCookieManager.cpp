#include "OceanCookieManager.h"
#include "BrowserPreferences.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <sys/stat.h>
#include <unistd.h>
#undef signals
#include <wpe/webkit.h>

OceanCookieManager::OceanCookieManager(const QString &path, BrowserPreferences *preferences, QObject *parent)
    : QObject(parent), m_preferences(preferences)
{
    connect(preferences, &BrowserPreferences::changed, this, [this] {
        const auto views = m_views;
        for (auto it = views.cbegin(); it != views.cend(); ++it) attachView(it.key(), it.value());
    });
    qputenv("OCEAN_BROWSER_PROFILE_DIRECTORY", QByteArray());
    m_profile = path + "/Data/WebEngine";
    QString ancestor = m_profile;
    while (ancestor != "/") {
        if (QFileInfo(ancestor).isSymbolicLink()) { m_error = tr("Cookie storage contains a symbolic link."); break; }
        ancestor = QFileInfo(ancestor).absolutePath();
    }
    if (m_error.isEmpty() && !QDir().mkpath(m_profile)) m_error = tr("Could not create cookie storage.");
    if (!m_error.isEmpty()) return;
    const QString cookies = m_profile + "/cookies";
    const QString previous = path + "/Data/cookies";
    if (QFileInfo(previous).isSymbolicLink() || QFileInfo(cookies).isSymbolicLink()) {
        m_error = tr("Cookie storage is unsafe."); return;
    }
    if (m_error.isEmpty() && !QFileInfo::exists(cookies) && QFileInfo::exists(previous)
        && !QDir().rename(previous, cookies)) m_error = tr("Could not migrate cookie storage.");
    if (m_error.isEmpty() && !QDir().mkpath(cookies)) m_error = tr("Could not create cookie storage.");
    if (!m_error.isEmpty()) return;
    for (const QString &dir : {m_profile, cookies}) {
        if (QFileInfo(dir).isSymbolicLink()
            || !QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
            m_error = tr("Cookie storage is unavailable.");
    }
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const QString file = cookies + "/cookies.sqlite" + suffix;
        struct stat metadata;
        if (!::lstat(QFile::encodeName(file).constData(), &metadata)) {
            if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 || metadata.st_uid != ::geteuid()
                || !QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner)) {
                m_error = tr("Cookie storage is unsafe."); return;
            }
        }
    }
    qputenv("OCEAN_BROWSER_PROFILE_DIRECTORY", ready() ? m_profile.toUtf8() : QByteArray());
}

void OceanCookieManager::attachView(QObject *object, bool privateTab)
{
    WebKitWebView *view = nullptr;
    if (!object || !QMetaObject::invokeMethod(object, "webView", Qt::DirectConnection,
            QGenericReturnArgument("WebKitWebView*", &view)) || !view) return;
    if (!m_views.contains(object)) {
        m_views.insert(object, privateTab);
        connect(object, &QObject::destroyed, this, [this, object] { m_views.remove(object); });
    }
    auto *session = webkit_web_view_get_network_session(view);
    if (privateTab && !webkit_network_session_is_ephemeral(session)) {
        emit storageError(tr("The private cookie profile is not isolated.")); return;
    }
    webkit_cookie_manager_set_accept_policy(webkit_network_session_get_cookie_manager(session),
        m_preferences->blockThirdPartyCookies() ? WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY
                                               : WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    webkit_network_session_set_persistent_credential_storage_enabled(session, FALSE);
    if (!ready()) emit storageError(m_error);
}
