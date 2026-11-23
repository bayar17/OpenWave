#include "OceanBookmarkManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <algorithm>

OceanBookmarkManager::OceanBookmarkManager(const QString &dataPath, QObject *parent)
    : QAbstractListModel(parent)
    , m_path(dataPath + QStringLiteral("/bookmarks.json"))
{
    load();
    applyFilter();
}

QString OceanBookmarkManager::normalized(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > MaximumUrlLength)
        return QString();
    const QUrl parsed(trimmed, QUrl::TolerantMode);
    if (!parsed.isValid())
        return QString();
    const QString scheme = parsed.scheme().toLower();
    if ((scheme != QLatin1String("http") && scheme != QLatin1String("https")) || parsed.host().isEmpty())
        return QString();
    return parsed.toString(QUrl::FullyEncoded);
}

bool OceanBookmarkManager::contains(const QString &url) const
{
    const QString key = normalized(url);
    if (key.isEmpty())
        return false;
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&key](const Entry &entry) { return entry.url == key; });
}

bool OceanBookmarkManager::toggle(const QString &url, const QString &title)
{
    const QString key = normalized(url);
    if (key.isEmpty()) {
        fail(tr("This page cannot be bookmarked."));
        return false;
    }
    const QList<Entry> previous = m_entries;
    const auto existing = std::find_if(m_entries.begin(), m_entries.end(), [&key](const Entry &entry) { return entry.url == key; });
    if (existing != m_entries.end()) {
        m_entries.erase(existing);
    } else {
        if (m_entries.size() >= MaximumBookmarks) {
            fail(tr("The bookmark list is full."));
            return false;
        }
        Entry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.url = key;
        entry.title = title.simplified().left(MaximumTitleLength);
        entry.added = QDateTime::currentSecsSinceEpoch();
        m_entries.prepend(entry);
    }
    if (!save()) {
        m_entries = previous;
        return false;
    }
    beginResetModel();
    applyFilter();
    endResetModel();
    ++m_revision;
    emit changed();
    return contains(key);
}

void OceanBookmarkManager::remove(const QString &id)
{
    removeAll({id});
}

void OceanBookmarkManager::removeAll(const QStringList &ids)
{
    if (ids.isEmpty())
        return;
    const QList<Entry> previous = m_entries;
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&ids](const Entry &entry) { return ids.contains(entry.id); }),
                    m_entries.end());
    if (m_entries.size() == previous.size())
        return;
    if (!save()) {
        m_entries = previous;
        return;
    }
    beginResetModel();
    applyFilter();
    endResetModel();
    ++m_revision;
    emit changed();
}

void OceanBookmarkManager::clear()
{
    if (m_entries.isEmpty())
        return;
    const QList<Entry> previous = m_entries;
    m_entries.clear();
    if (!save()) {
        m_entries = previous;
        return;
    }
    beginResetModel();
    applyFilter();
    endResetModel();
    ++m_revision;
    emit changed();
}

void OceanBookmarkManager::setFilter(const QString &filter)
{
    const QString cleaned = filter.simplified();
    if (cleaned == m_filter)
        return;
    m_filter = cleaned;
    beginResetModel();
    applyFilter();
    endResetModel();
    emit filterChanged();
}

void OceanBookmarkManager::applyFilter()
{
    m_visible.clear();
    for (int index = 0; index < m_entries.size(); ++index) {
        const Entry &entry = m_entries.at(index);
        if (m_filter.isEmpty() || entry.title.contains(m_filter, Qt::CaseInsensitive)
            || entry.url.contains(m_filter, Qt::CaseInsensitive))
            m_visible.append(index);
    }
}

void OceanBookmarkManager::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
}

void OceanBookmarkManager::load()
{
    QFile file(m_path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        fail(tr("Bookmarks could not be read."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(tr("Bookmarks could not be read."));
        return;
    }
    const QJsonArray items = document.object().value(QStringLiteral("bookmarks")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.url = normalized(object.value(QStringLiteral("url")).toString());
        if (entry.url.isEmpty() || m_entries.size() >= MaximumBookmarks)
            continue;
        entry.id = object.value(QStringLiteral("id")).toString();
        if (entry.id.isEmpty())
            entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.title = object.value(QStringLiteral("title")).toString().simplified().left(MaximumTitleLength);
        entry.added = static_cast<qint64>(object.value(QStringLiteral("added")).toDouble());
        const bool duplicate = std::any_of(m_entries.cbegin(), m_entries.cend(), [&entry](const Entry &known) { return known.url == entry.url; });
        if (!duplicate)
            m_entries.append(entry);
    }
}

bool OceanBookmarkManager::save()
{
    QJsonArray items;
    for (const Entry &entry : std::as_const(m_entries)) {
        items.append(QJsonObject{{QStringLiteral("id"), entry.id},
                                 {QStringLiteral("url"), entry.url},
                                 {QStringLiteral("title"), entry.title},
                                 {QStringLiteral("added"), static_cast<double>(entry.added)}});
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        fail(tr("Bookmarks could not be saved."));
        return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("bookmarks"), items}}).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        fail(tr("Bookmarks could not be saved."));
        return false;
    }
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
    return true;
}

int OceanBookmarkManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant OceanBookmarkManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return QVariant();
    const Entry &entry = m_entries.at(m_visible.at(index.row()));
    const QUrl parsed(entry.url);
    switch (role) {
    case BookmarkId:
        return entry.id;
    case StoredUrl:
        return entry.url;
    case PageTitle:
        return entry.title.isEmpty() ? parsed.host() : entry.title;
    case Host:
        return parsed.host();
    case AddedText:
        return QLocale().toString(QDateTime::fromSecsSinceEpoch(entry.added).date(), QLocale::ShortFormat);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> OceanBookmarkManager::roleNames() const
{
    return {{BookmarkId, "bookmarkId"}, {StoredUrl, "storedUrl"}, {PageTitle, "pageTitle"}, {Host, "host"}, {AddedText, "addedText"}};
}
