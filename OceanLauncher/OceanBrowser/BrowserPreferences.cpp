#include "BrowserPreferences.h"
#include <OpenWave/OpenWavePortals.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

BrowserPreferences::BrowserPreferences(const QString &path, const QString &user, QObject *parent)
    : QObject(parent), m_file(path + "/Browser/Settings.json"), m_user(QDir::cleanPath(user)),
      m_portal(new OpenWavePortals(this))
{
    if (!safeStorage() || !QDir().mkpath(QFileInfo(m_file).absolutePath()))
        reportError(tr("Browser settings storage is unavailable."));
    else {
        QFile::setPermissions(QFileInfo(m_file).absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile file(m_file);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly)) reportError(tr("Could not read browser settings."));
            else {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError || !document.isObject())
                    reportError(tr("Browser settings are damaged; defaults are in use."));
                else {
                    const auto saved = document.object().toVariantMap();
                    for (const auto &key : {"askOnDownload", "startPrivate", "rememberTabs", "alwaysShowFullUrl",
                                            "rememberHistory", "blockThirdPartyCookies"})
                        if (saved.value(key).metaType().id() == QMetaType::Bool) m_values.insert(key, saved.value(key));
                    const auto engine = saved.value("searchEngine").toString();
                    if (QStringList{"google", "duckduckgo", "bing"}.contains(engine)) m_values.insert("searchEngine", engine);
                    const auto folder = QFileInfo(saved.value("downloadFolder").toString()).canonicalFilePath();
                    if ((folder == m_user || folder.startsWith(m_user + "/")) && QFileInfo(folder).isDir())
                        m_values.insert("downloadFolder", folder);
                }
            }
        }
    }
    connect(m_portal, &OpenWavePortals::busyChanged, this, &BrowserPreferences::choosingFolderChanged);
    connect(m_portal, &OpenWavePortals::errorOccurred, this, &BrowserPreferences::reportError);
    connect(m_portal, &OpenWavePortals::completed, this, [this](const QVariantList &grants) {
        auto *grant = grants.size() == 1 ? qobject_cast<PortalGrant *>(grants.first().value<QObject *>()) : nullptr;
        if (!grant || !grant->directory()) { reportError(tr("No folder was selected.")); return; }
        char buffer[4096];
        const auto link = QStringLiteral("/proc/self/fd/%1").arg(grant->descriptor()).toLocal8Bit();
        const auto count = readlink(link.constData(), buffer, sizeof(buffer));
        const QString path = count > 0 ? QFileInfo(QString::fromLocal8Bit(buffer, count)).canonicalFilePath() : QString();
        if ((path != m_user && !path.startsWith(m_user + "/")) || !QFileInfo(path).isDir() || !QFileInfo(path).isWritable()) {
            reportError(tr("Choose a writable folder in your data directory.")); return;
        }
        setValue("downloadFolder", path);
    });
}

bool BrowserPreferences::safeStorage() const
{
    for (QString path = m_file; path != "/"; path = QFileInfo(path).absolutePath())
        if (QFileInfo(path).isSymbolicLink()) return false;
    struct stat metadata;
    if (::lstat(QFile::encodeName(m_file).constData(), &metadata) == 0)
        return S_ISREG(metadata.st_mode) && metadata.st_nlink == 1 && metadata.st_uid == ::geteuid();
    return errno == ENOENT;
}

void BrowserPreferences::reportError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

void BrowserPreferences::setValue(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value) return;
    auto updated = m_values;
    updated.insert(key, value);
    if (!safeStorage()) { reportError(tr("Browser settings storage is unsafe.")); emit changed(); return; }
    QSaveFile file(m_file);
    const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(updated)).toJson();
    if (!file.open(QIODevice::WriteOnly)) { reportError(tr("Could not save browser settings.")); emit changed(); return; }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        reportError(tr("Could not save browser settings.")); emit changed(); return;
    }
    m_values = updated;
    reportError({});
    emit changed();
}

void BrowserPreferences::setSearchEngine(const QString &value)
{
    if (QStringList{"google", "duckduckgo", "bing"}.contains(value)) setValue("searchEngine", value);
}

QString BrowserPreferences::searchUrl(const QString &query) const
{
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query));
    if (searchEngine() == "duckduckgo") return "https://duckduckgo.com/?q=" + encoded;
    if (searchEngine() == "bing") return "https://www.bing.com/search?q=" + encoded;
    return "https://www.google.com/search?q=" + encoded;
}

bool BrowserPreferences::choosingFolder() const { return m_portal->busy(); }
void BrowserPreferences::chooseDownloadFolder()
{
    if (choosingFolder()) return;
    m_portal->selectDirectory({{"mutedTitle", tr("Choosing")}, {"primaryTitle", tr("Folder")},
                              {"initialDirectory", downloadFolder()}});
}
