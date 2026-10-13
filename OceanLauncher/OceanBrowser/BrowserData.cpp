#include "BrowserData.h"
#include <OpenWave/preferences.h>
#include <OpenWave/application-data.h>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQuickItemGrabResult>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <pwd.h>
#include <unistd.h>

namespace {
bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9-]{1,80}$"));
    return pattern.match(id).hasMatch();
}
constexpr auto ownerAccess = QFile::ReadOwner | QFile::WriteOwner;
}

QImage BrowserPreviews::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    QMutexLocker lock(&m_mutex);
    QImage image = m_images.value(id.section('?', 0, 0));
    if (size) *size = image.size();
    return requested.isValid() ? image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
}
void BrowserPreviews::put(const QString &id, const QImage &image)
{
    QMutexLocker lock(&m_mutex);
    m_images.insert(id, image);
}
void BrowserPreviews::remove(const QString &id)
{
    QMutexLocker lock(&m_mutex);
    m_images.remove(id);
}

BrowserData::BrowserData(BrowserPreviews *previews, QString usersPath, QString userName, QString applicationId, QObject *parent)
    : QObject(parent), m_previews(previews)
{
    if (applicationId.isEmpty()) applicationId = openwaveApplicationId();
    m_path = openwaveApplicationDataPath(applicationId, usersPath, userName);
    m_userPath = openwaveUserDataPath(usersPath, userName);
    if (m_path.isEmpty()) qFatal("Invalid browser application data identity");
    QString ancestor = QDir::cleanPath(m_path);
    while (ancestor != QStringLiteral("/")) {
        if (QFileInfo(ancestor).isSymbolicLink()) qFatal("Browser profile path is a symbolic link");
        ancestor = QFileInfo(ancestor).absolutePath();
    }
    for (const QString &relative : {QString(), QStringLiteral("cache/images"),
            QStringLiteral("cache/GPUCache"), QStringLiteral("cache/JSCache"),
            QStringLiteral("cache/HTTP"), QStringLiteral("passwords")}) {
        const QString directory = QDir(m_path).filePath(relative);
        if (QFileInfo(directory).isSymbolicLink() || !QDir().mkpath(directory))
            qFatal("Cannot create browser profile directory");
        QFile::setPermissions(directory, ownerAccess | QFile::ExeOwner);
    }
    // Retain only previews belonging to the restored regular session.
    QStringList retained;
    for (const QVariant &tab : session().value(QStringLiteral("tabs")).toList())
        retained.append(tab.toMap().value(QStringLiteral("id")).toString() + QStringLiteral(".jpg"));
    const QDir previewsDirectory(m_path + QStringLiteral("/cache/images"));
    for (const QString &name : previewsDirectory.entryList({QStringLiteral("*.jpg")}, QDir::Files))
        if (!retained.contains(name)) QFile::remove(previewsDirectory.filePath(name));
    for (const QString &name : {QStringLiteral("bookmarks.json")})
        if (!QFile::exists(QDir(m_path).filePath(name))) writeJson(name, QVariantList{});
}

bool BrowserData::reduceMotion() const
{
    QSettings settings(openwavePreferencesPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("accessibility/reduceMotion"), false).toBool();
}
QVariant BrowserData::readJson(const QString &name) const
{
    QFile file(QDir(m_path).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).toVariant();
}
bool BrowserData::writeJson(const QString &name, const QVariant &value)
{
    QSaveFile file(QDir(m_path).filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        emit storageError(QStringLiteral("Could not save browser data."));
        return false;
    }
    file.setPermissions(ownerAccess);
    const QByteArray bytes = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        emit storageError(QStringLiteral("Could not save browser data."));
        return false;
    }
    return true;
}
QVariantMap BrowserData::session() const { return readJson(QStringLiteral("tabs.json")).toMap(); }
void BrowserData::saveSession(const QVariantList &tabs, const QString &activeId)
{
    QVariantList regularTabs;
    QString regularActiveId;
    for (const QVariant &entry : tabs) {
        const QVariantMap tab = entry.toMap();
        if (tab.value(QStringLiteral("privateTab")).toBool()) continue;
        if (!validId(tab.value(QStringLiteral("id")).toString())) continue;
        if (tab.value(QStringLiteral("id")).toString() == activeId) regularActiveId = activeId;
        regularTabs.append(QVariantMap{{QStringLiteral("id"), tab.value(QStringLiteral("id"))},
            {QStringLiteral("url"), tab.value(QStringLiteral("url"))},
            {QStringLiteral("title"), tab.value(QStringLiteral("title"))}});
    }
    if (regularActiveId.isEmpty() && !regularTabs.isEmpty())
        regularActiveId = regularTabs.first().toMap().value(QStringLiteral("id")).toString();
    writeJson(QStringLiteral("tabs.json"), QVariantMap{{QStringLiteral("tabs"), regularTabs},
        {QStringLiteral("activeId"), regularActiveId}});
}
QUrl BrowserData::previewSource(const QString &id) const
{
    if (!validId(id)) return {};
    const QString path = m_path + QStringLiteral("/cache/images/%1.jpg").arg(id);
    return QFile::exists(path) ? QUrl::fromLocalFile(path) : QUrl{};
}
bool BrowserData::capturePreview(QQuickItem *item, const QString &id, bool privateTab)
{
    if (!item || !validId(id)) return false;
    auto grab = item->grabToImage(QSize(480, qRound(480 * item->height() / qMax(1.0, item->width()))));
    if (!grab) return false;
    const quint64 revision = ++m_revision;
    m_previewGeneration.insert(id, revision);
    connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab, id, privateTab, revision] {
        if (m_previewGeneration.value(id) != revision) return;
        const QImage image = grab->image();
        QUrl source;
        if (!image.isNull()) {
            if (privateTab) {
                m_previews->put(id, image);
                source = QUrl(QStringLiteral("image://browserpreviews/%1?%2").arg(id).arg(revision));
            } else {
                const QString path = m_path + QStringLiteral("/cache/images/%1.jpg").arg(id);
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.setPermissions(ownerAccess);
                    if (image.save(&file, "JPG", 88) && file.commit()) {
                        source = QUrl::fromLocalFile(path);
                        source.setQuery(QString::number(revision));
                    }
                }
            }
        }
        emit previewReady(id, source);
    }, Qt::SingleShotConnection);
    return true;
}
void BrowserData::removePreview(const QString &id)
{
    if (!validId(id)) return;
    m_previewGeneration.remove(id);
    m_previews->remove(id);
    QFile::remove(m_path + QStringLiteral("/cache/images/%1.jpg").arg(id));
}
