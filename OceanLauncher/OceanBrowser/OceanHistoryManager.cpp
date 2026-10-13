#include "OceanHistoryManager.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <functional>

namespace {
constexpr auto fileAccess = QFile::ReadOwner | QFile::WriteOwner;
constexpr int pageSize = 200;
bool safeDirectory(const QString &path)
{
    QString ancestor = QDir::cleanPath(path);
    while (ancestor != "/") {
        if (QFileInfo(ancestor).isSymbolicLink()) return false;
        ancestor = QFileInfo(ancestor).absolutePath();
    }
    if (!QDir().mkpath(path)) return false;
    struct stat info {};
    return !lstat(QFile::encodeName(path).constData(), &info) && S_ISDIR(info.st_mode)
        && info.st_uid == geteuid() && QFile::setPermissions(path, fileAccess | QFile::ExeOwner);
}
bool safeFile(const QString &path)
{
    struct stat info {};
    return !lstat(QFile::encodeName(path).constData(), &info) && S_ISREG(info.st_mode) && info.st_nlink == 1
        && info.st_uid == geteuid() && QFile::setPermissions(path, fileAccess);
}
bool publicPage(const QUrl &url)
{
    return url.isValid() && !url.isEmpty()
        && (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "file");
}
bool validIcon(const QString &path)
{
    if (!safeFile(path)) return false;
    QImageReader reader(path);
    const QSize size = reader.size();
    return size.isValid() && size.width() <= 1024 && size.height() <= 1024 && !reader.read().isNull();
}
}

class HistoryWorker : public QObject {
public:
    HistoryWorker(OceanHistoryManager *owner, QString path) : m_owner(owner), m_path(std::move(path)) { }
    void post(std::function<void(OceanHistoryManager *)> action) {
        QMetaObject::invokeMethod(m_owner, [owner = m_owner, action = std::move(action)] {
            if (owner) action(owner);
        }, Qt::QueuedConnection);
    }
    void fail(const QString &detail) {
        qWarning("Ocean history: %s", qPrintable(detail));
        post([message = tr("Could not access browsing history: %1").arg(detail)](auto *owner) {
            owner->m_querying = false;
            owner->setState(owner->ready(), message);
        });
    }
    bool exec(const QString &sql) {
        QSqlQuery query(m_db);
        if (query.exec(sql)) return true;
        fail(query.lastError().text()); return false;
    }
    bool begin() { if (m_db.transaction()) return true; fail(m_db.lastError().text()); return false; }
    bool commit() {
        if (m_db.commit()) return true;
        fail(m_db.lastError().text()); m_db.rollback(); return false;
    }
    void changed() {
        post([](auto *owner) { owner->setState(true, {}); owner->reload(); emit owner->historyChanged(); });
    }
    void initialize() {
        if (m_path.isEmpty() || !safeDirectory(m_path) || !safeDirectory(m_path + "/Browser")
            || !safeDirectory(m_path + "/Cache") || !safeDirectory(m_path + "/Cache/Favicons")) {
            fail(tr("The private profile directory is unavailable.")); return;
        }
        const QString file = m_path + "/Browser/History.sqlite";
        const int fd = ::open(QFile::encodeName(file).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd >= 0) ::close(fd);
        if (!safeFile(file)) { fail(tr("The history file is not private or is unsafe.")); return; }
        for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QString sidecar = file + suffix;
            if ((QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymbolicLink()) && !safeFile(sidecar)) {
                fail(tr("An unsafe SQLite sidecar file was found.")); return;
            }
        }
        m_connection = "ocean-history-" + QUuid::createUuid().toString(QUuid::Id128);
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
        m_db.setDatabaseName(file);
        if (!m_db.open()) { fail(m_db.lastError().text()); return; }
        if (!exec("PRAGMA busy_timeout = 5000") || !exec("PRAGMA journal_mode = WAL")
            || !exec("PRAGMA foreign_keys = ON") || !exec("PRAGMA secure_delete = ON")
            || !exec("CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT NOT NULL, title TEXT, favicon TEXT, visited_at INTEGER NOT NULL)")
            || !exec("CREATE INDEX IF NOT EXISTS idx_history_date ON history(visited_at DESC)")) return;
        if (!migrate()) return;
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")})
            if (QFileInfo::exists(file + suffix)) safeFile(file + suffix);
        m_network = new QNetworkAccessManager(this);
        m_usable = true;
        post([](auto *owner) { owner->setState(true, {}); owner->reload(); });
    }
    bool migrate() {
        QSqlQuery version(m_db);
        if (!version.exec("PRAGMA user_version") || !version.next()) { fail(version.lastError().text()); return false; }
        if (version.value(0).toInt() >= 1) return true;
        QFile legacy(m_path + "/history.json");
        QVariantList entries;
        if (legacy.exists()) {
            if (!safeFile(legacy.fileName()) || !legacy.open(QIODevice::ReadOnly)) {
                fail(tr("The previous history could not be migrated.")); return false;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(legacy.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isArray()) {
                fail(tr("The previous history is invalid; it has been preserved.")); return false;
            }
            entries = document.toVariant().toList();
            legacy.close();
        }
        if (!begin()) return false;
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO history(url,title,favicon,visited_at) VALUES(?,?,'',?)");
        for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
            const auto entry = it->toMap();
            const QUrl url(entry.value("address").toString());
            const QDateTime when = QDateTime::fromString(entry.value("visited").toString(), Qt::ISODate);
            if (!publicPage(url) || !when.isValid()) continue;
            insert.bindValue(0, url.toString(QUrl::FullyEncoded));
            insert.bindValue(1, entry.value("title").toString());
            insert.bindValue(2, when.toMSecsSinceEpoch());
            if (!insert.exec()) { fail(insert.lastError().text()); m_db.rollback(); return false; }
        }
        if (!exec("PRAGMA user_version = 1")) { m_db.rollback(); return false; }
        if (!commit()) return false;
        // Never discard the old store until every visit has committed.
        if (legacy.exists()) QFile::remove(legacy.fileName());
        return true;
    }
    void record(const QString &token, const QUrl &url, const QString &title, qint64 timestamp) {
        if (!m_usable || !begin()) return;
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO history(url,title,favicon,visited_at) VALUES(?,?,'',?)");
        insert.addBindValue(url.toString(QUrl::FullyEncoded)); insert.addBindValue(title); insert.addBindValue(timestamp);
        if (!insert.exec()) { fail(insert.lastError().text()); m_db.rollback(); return; }
        const qint64 id = insert.lastInsertId().toLongLong();
        if (!commit()) return;
        m_tokens.insert(token, id);
        // Tokens are only needed while a page's asynchronous metadata arrives.
        if (m_tokens.size() > 512) m_tokens.erase(m_tokens.begin());
        post([token, id](auto *owner) { emit owner->visitRecorded(token, id); });
        changed();
    }
    void query(quint64 generation, const QString &filter, const QString &sort, int offset) {
        if (!m_usable) return;
        QString escaped = filter;
        escaped.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        const QString match = "%" + escaped + "%";
        const QString where = " WHERE (title LIKE ? ESCAPE '\\' OR url LIKE ? ESCAPE '\\')";
        QSqlQuery count(m_db);
        count.prepare("SELECT count(*) FROM history" + where);
        count.addBindValue(match); count.addBindValue(match);
        if (!count.exec() || !count.next()) { fail(count.lastError().text()); return; }
        const int total = count.value(0).toInt();
        const QString order = sort == "oldest" ? "visited_at ASC,id ASC" : sort == "title" ? "title COLLATE NOCASE ASC,id DESC" : "visited_at DESC,id DESC";
        QSqlQuery result(m_db);
        result.prepare("SELECT id,url,title,favicon,visited_at FROM history" + where + " ORDER BY " + order + " LIMIT ? OFFSET ?");
        result.addBindValue(match); result.addBindValue(match); result.addBindValue(pageSize); result.addBindValue(offset);
        if (!result.exec()) { fail(result.lastError().text()); return; }
        QList<QVariantMap> rows;
        QHash<QString, bool> icons;
        const QDate today = QDate::currentDate();
        const QLocale locale;
        static const QRegularExpression iconPath("^Cache/Favicons/[a-f0-9]{64}\\.png$");
        while (result.next()) {
            const auto when = QDateTime::fromMSecsSinceEpoch(result.value(4).toLongLong());
            const QString date = when.date() == today ? tr("Today") : when.date() == today.addDays(-1) ? tr("Yesterday") : locale.toString(when.date(), QLocale::LongFormat);
            const QString icon = result.value(3).toString();
            if (!icons.contains(icon)) icons[icon] = iconPath.match(icon).hasMatch() && validIcon(m_path + "/" + icon);
            const QString title = result.value(2).toString().trimmed();
            rows.append({{"historyId", result.value(0)}, {"storedUrl", result.value(1)},
                {"pageTitle", title.isEmpty() ? tr("Untitled page") : title},
                {"faviconSource", icons.value(icon) ? QUrl::fromLocalFile(m_path + "/" + icon).toString() : QString()},
                {"visitTime", locale.toString(when, QLocale::ShortFormat)}, {"dateGroup", date}, {"visitedAt", result.value(4)}});
        }
        post([generation, offset, rows, total](auto *owner) { owner->applyRows(generation, offset, rows, total); });
    }
    void remove(const QVariantList &ids, qint64 from, qint64 until, bool range) {
        if (!m_usable || !begin()) return;
        QSqlQuery deletion(m_db);
        if (range) {
            deletion.prepare("DELETE FROM history WHERE visited_at >= ? AND (? = 0 OR visited_at < ?)");
            deletion.addBindValue(from); deletion.addBindValue(until); deletion.addBindValue(until);
            if (!deletion.exec()) { fail(deletion.lastError().text()); m_db.rollback(); return; }
        } else {
            deletion.prepare("DELETE FROM history WHERE id = ?");
            for (const auto &id : ids) {
                deletion.bindValue(0, id.toLongLong());
                if (!deletion.exec()) { fail(deletion.lastError().text()); m_db.rollback(); return; }
            }
        }
        if (commit()) {
            // Deleted visits must not be resurrected by pending favicon replies.
            exec("PRAGMA wal_checkpoint(TRUNCATE)");
            changed();
        }
    }
    void updateIcons(const QSet<qint64> &ids, const QString &relative) {
        if (!begin()) return;
        QSqlQuery update(m_db);
        update.prepare("UPDATE history SET favicon = ? WHERE id = ?");
        for (qint64 id : ids) {
            update.bindValue(0, relative); update.bindValue(1, id);
            if (!update.exec()) { fail(update.lastError().text()); m_db.rollback(); return; }
        }
        if (commit()) changed();
    }
    void details(const QString &token, const QString &title, const QUrl &iconUrl, const QUrl &documentUrl) {
        const qint64 id = m_tokens.value(token);
        if (!m_usable || !id) return;
        if (!documentUrl.isEmpty()) {
            QSqlQuery visit(m_db);
            visit.prepare("SELECT url FROM history WHERE id = ?");
            visit.addBindValue(id);
            if (!visit.exec()) { fail(visit.lastError().text()); return; }
            if (!visit.next() || QUrl(visit.value(0).toString()) != documentUrl) return;
        }
        if (!title.trimmed().isEmpty() && begin()) {
            QSqlQuery update(m_db);
            update.prepare("UPDATE history SET title = ? WHERE id = ?");
            update.addBindValue(title); update.addBindValue(id);
            if (!update.exec()) { fail(update.lastError().text()); m_db.rollback(); }
            else if (commit()) changed();
        }
        if (!iconUrl.isEmpty()) favicon(token, iconUrl);
    }
    void favicon(const QString &token, QUrl url) {
        const qint64 id = m_tokens.take(token);
        if (!id || !m_network) return;
        url.setFragment({});
        if (url.scheme() != "http" && url.scheme() != "https" && url.scheme() != "data") return;
        if (!url.userName().isEmpty() || !url.password().isEmpty()) return;
        const QString key = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
        const QString relative = "Cache/Favicons/" + key + ".png";
        if (!safeDirectory(m_path + "/Cache") || !safeDirectory(m_path + "/Cache/Favicons")) return;
        if (validIcon(m_path + "/" + relative)) { updateIcons({id}, relative); return; }
        if (m_icons.contains(key)) { m_icons[key].insert(id); return; }
        m_icons[key].insert(id);
        if (url.scheme() == "data") {
            const QByteArray data = url.toEncoded();
            const int comma = data.indexOf(',');
            if (comma < 0 || data.size() > 700000) { m_icons.remove(key); return; }
            saveIcon(key, relative, data.left(comma).endsWith(";base64") ? QByteArray::fromBase64(data.mid(comma + 1)) : QByteArray::fromPercentEncoding(data.mid(comma + 1)));
            return;
        }
        QNetworkRequest request(url);
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply = m_network->get(request);
        connect(reply, &QNetworkReply::readyRead, reply, [reply] { if (reply->bytesAvailable() > 512 * 1024) reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, relative] {
            if (!m_stopping && reply->error() == QNetworkReply::NoError) saveIcon(key, relative, reply->readAll());
            else m_icons.remove(key);
            reply->deleteLater();
        });
    }
    void saveIcon(const QString &key, const QString &relative, const QByteArray &bytes) {
        const auto ids = m_icons.take(key);
        if (bytes.size() > 512 * 1024) return;
        QBuffer buffer;
        buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > 1024 || size.height() > 1024) return;
        QImage image = reader.read();
        if (image.isNull() || !safeDirectory(m_path + "/Cache/Favicons")) return;
        image = image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QSaveFile file(m_path + "/" + relative);
        if (!file.open(QIODevice::WriteOnly)) return;
        file.setPermissions(fileAccess);
        if (image.save(&file, "PNG") && file.commit()) updateIcons(ids, relative);
    }
    void shutdown() {
        m_stopping = true;
        delete m_network; m_network = nullptr;
        if (m_db.isOpen()) { if (m_usable) exec("PRAGMA wal_checkpoint(TRUNCATE)"); m_db.close(); }
        m_db = {};
        if (!m_connection.isEmpty()) QSqlDatabase::removeDatabase(m_connection);
    }
private:
    QPointer<OceanHistoryManager> m_owner;
    QString m_path, m_connection;
    QSqlDatabase m_db;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, qint64> m_tokens;
    QHash<QString, QSet<qint64>> m_icons;
    bool m_stopping = false;
    bool m_usable = false;
};

OceanHistoryManager::OceanHistoryManager(QString path, QObject *parent)
    : QAbstractListModel(parent), m_worker(new HistoryWorker(this, std::move(path)))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->initialize(); }, Qt::QueuedConnection);
}
OceanHistoryManager::~OceanHistoryManager()
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
}
int OceanHistoryManager::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }
QVariant OceanHistoryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    return m_rows.at(index.row()).value(QString::fromUtf8(roleNames().value(role)));
}
QHash<int, QByteArray> OceanHistoryManager::roleNames() const
{
    return {{HistoryId,"historyId"}, {StoredUrl,"storedUrl"}, {PageTitle,"pageTitle"}, {FaviconSource,"faviconSource"},
        {VisitTime,"visitTime"}, {DateGroup,"dateGroup"}, {VisitedAt,"visitedAt"}};
}
void OceanHistoryManager::setState(bool ready, const QString &error)
{
    m_ready = ready; m_error = error; emit stateChanged();
}
void OceanHistoryManager::setFilter(const QString &filter)
{
    if (m_filter == filter) return;
    m_filter = filter; emit filterChanged(); reload();
}
void OceanHistoryManager::setSortOrder(const QString &sort)
{
    if ((sort != "newest" && sort != "oldest" && sort != "title") || sort == m_sort) return;
    m_sort = sort; emit sortOrderChanged(); reload();
}
bool OceanHistoryManager::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_ready && !m_querying && m_rows.size() < m_total;
}
void OceanHistoryManager::fetchMore(const QModelIndex &parent) { if (canFetchMore(parent)) query(m_rows.size()); }
void OceanHistoryManager::reload() { if (m_ready) { ++m_generation; query(0); } }
void OceanHistoryManager::query(int offset)
{
    m_querying = true;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation = m_generation, filter = m_filter, sort = m_sort, offset] {
        worker->query(generation, filter, sort, offset);
    }, Qt::QueuedConnection);
}
void OceanHistoryManager::applyRows(quint64 generation, int offset, const QList<QVariantMap> &rows, int total)
{
    if (generation != m_generation) return;
    m_querying = false;
    if (offset == 0) { beginResetModel(); m_rows = rows; endResetModel(); }
    else if (!rows.isEmpty() && offset == m_rows.size()) {
        beginInsertRows({}, offset, offset + rows.size() - 1); m_rows.append(rows); endInsertRows();
    }
    m_total = total; emit countChanged();
}
QString OceanHistoryManager::recordVisit(const QUrl &url, const QString &title, bool privateTab)
{
    if (privateTab || !publicPage(url)) return {};
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, token, url, title, now] { worker->record(token, url, title, now); }, Qt::QueuedConnection);
    return token;
}
void OceanHistoryManager::setVisitFavicon(const QString &token, const QUrl &url)
{
    if (token.isEmpty() || url.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, token, url] { worker->favicon(token, url); }, Qt::QueuedConnection);
}
void OceanHistoryManager::setVisitDetails(const QString &token, const QString &title, const QUrl &url,
                                         const QUrl &documentUrl)
{
    if (token.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, token, title, url, documentUrl] {
        worker->details(token, title, url, documentUrl);
    }, Qt::QueuedConnection);
}
void OceanHistoryManager::removeEntries(const QVariantList &ids)
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, ids] { worker->remove(ids, 0, 0, false); }, Qt::QueuedConnection);
}
void OceanHistoryManager::clearHistory(qint64 from, qint64 until)
{
    if (from < 0 || until < 0 || (until && until <= from)) { setState(m_ready, tr("Invalid history date range.")); return; }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, from, until] { worker->remove({}, from, until, true); }, Qt::QueuedConnection);
}
void OceanHistoryManager::clearDateRange(const QString &from, const QString &through)
{
    const QDate first = QDate::fromString(from, Qt::ISODate), last = QDate::fromString(through, Qt::ISODate);
    if (!first.isValid() || !last.isValid() || first > last) { setState(m_ready, tr("Enter valid dates in YYYY-MM-DD format.")); return; }
    clearHistory(first.startOfDay().toMSecsSinceEpoch(), last.addDays(1).startOfDay().toMSecsSinceEpoch());
}
void OceanHistoryManager::clearPeriod(const QString &period)
{
    const QDate today = QDate::currentDate();
    if (period == "all") clearHistory();
    else if (period == "today") clearDateRange(today.toString(Qt::ISODate), today.toString(Qt::ISODate));
    else if (period == "yesterday") clearDateRange(today.addDays(-1).toString(Qt::ISODate), today.addDays(-1).toString(Qt::ISODate));
    else if (period == "week") clearDateRange(today.addDays(-6).toString(Qt::ISODate), today.toString(Qt::ISODate));
}
