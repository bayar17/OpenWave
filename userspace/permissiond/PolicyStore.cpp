#include "PolicyStore.h"
#include "../application-id.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

QJsonObject PermissionPolicy::json() const
{
    return {{"application", application}, {"user", qint64(user)}, {"permission", permission},
        {"state", state}, {"duration", duration}, {"session", session},
        {"instance", instance}, {"applicationDigest", applicationDigest}, {"createdAt", created}, {"modifiedAt", modified}, {"expiresAt", expires}, {"revocationPending", revocationPending}, {"requestedAt", requestedAt}};
}
PolicyStore::PolicyStore(QString directory) : m_directory(std::move(directory)) {}
QStringList PolicyStore::permissions() { return {"files.read", "files.write", "files.directory"}; }
bool PolicyStore::validState(const QString &state) { return QStringList{"granted", "denied", "not-determined", "restricted"}.contains(state); }
bool PolicyStore::validDuration(const QString &duration) { return QStringList{"permanent", "session", "application", "once"}.contains(duration); }
bool PolicyStore::validPermission(const QString &permission)
{ static const QRegularExpression identifier(QRegularExpression::anchoredPattern("[A-Za-z0-9][A-Za-z0-9_.-]{0,127}")); return identifier.match(permission).hasMatch(); }
QString PolicyStore::key(quint32 user, const QString &app, const QString &permission) const
{ return QString::number(user) + ':' + app + ':' + permission; }

bool PolicyStore::load(QString *error)
{
    QString path;
    for (const auto &part : m_directory.split('/', Qt::SkipEmptyParts)) {
        path += '/' + part;
        struct stat st;
        if (lstat(QFile::encodeName(path).constData(), &st)) {
            if (errno != ENOENT || mkdir(QFile::encodeName(path).constData(), 0700)) {
                *error = "Cannot create the protected policy directory"; return false;
            }
            if (lstat(QFile::encodeName(path).constData(), &st)) return false;
        }
        if (!S_ISDIR(st.st_mode) || (st.st_uid != 0 && st.st_uid != geteuid())
            || ((st.st_mode & 0022) && !(st.st_uid == 0 && (st.st_mode & S_ISVTX) && path != m_directory))) {
            *error = "Unsafe policy directory ownership or permissions"; return false;
        }
    }
    if (chmod(QFile::encodeName(m_directory).constData(), 0700)) { *error = "Cannot protect policy directory"; return false; }
    const QByteArray filename = QFile::encodeName(m_directory + "/policies.json");
    int fd = open(filename.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) return true;
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid()
        || st.st_nlink != 1 || (st.st_mode & 0077) || st.st_size > 4 * 1024 * 1024) {
        if (fd >= 0) close(fd); *error = "Unsafe policy database"; return false;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { close(fd); *error = "Cannot open policy database"; return false; }
    QJsonParseError failure;
    const auto document = QJsonDocument::fromJson(file.readAll(), &failure);
    const auto object = document.object();
    if (failure.error != QJsonParseError::NoError || !document.isObject() || object.value("version").toInt() != 1
        || !object.value("policies").isArray() || object.value("policies").toArray().size() > 8192) {
        *error = "Unsupported or damaged permission database"; return false;
    }
    QMap<QString, PermissionPolicy> parsed;
    bool migrated = false;
    const QRegularExpression identifier("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    for (const auto &value : object.value("policies").toArray()) {
        const auto row = value.toObject(); PermissionPolicy p;
        p.application = openwaveCanonicalApplicationId(row.value("application").toString());
        migrated |= p.application != row.value("application").toString(); p.permission = row.value("permission").toString();
        p.state = row.value("state").toString(); p.duration = row.value("duration").toString();
        p.session = row.value("session").toString();
        p.instance = row.value("instance").toString(); p.applicationDigest = row.value("applicationDigest").toString();
        p.revocationPending = row.value("revocationPending").toBool();
        const qint64 user = row.value("user").toInteger(-1);
        p.created = row.value("createdAt").toInteger(); p.modified = row.value("modifiedAt").toInteger();
        p.expires = row.value("expiresAt").toInteger();
        p.requestedAt = row.value("requestedAt").toInteger();
        if (user < 0 || user > 0xffffffffLL || !identifier.match(p.application).hasMatch()
            || !validPermission(p.permission) || (!permissions().contains(p.permission) && (p.state != "restricted" || p.duration != "permanent"))
            || !validState(p.state) || !validDuration(p.duration) || p.requestedAt < 0 || p.requestedAt > p.modified
            || (p.duration == "application" && p.instance.isEmpty()) || p.instance.size() > 512
            || (!p.applicationDigest.isEmpty() && !QRegularExpression("^[a-f0-9]{64}$").match(p.applicationDigest).hasMatch())
            || p.duration == "once" || p.created <= 0 || p.modified < p.created || p.session.size() > 128) {
            *error = "Invalid permission record"; return false;
        }
        p.user = user;
        const auto id = key(p.user, p.application, p.permission);
        if (parsed.contains(id)) { *error = "Duplicate permission record"; return false; }
        parsed.insert(id, p);
    }
    if (migrated && !save(parsed, error)) return false;
    m_records = parsed; ++m_revision; return true;
}

PermissionPolicy PolicyStore::get(quint32 user, const QString &app, const QString &permission, const QString &session, const QString &instance) const
{
    auto policy = m_records.value(key(user, app, permission));
    policy.user = user; policy.application = app; policy.permission = permission;
    if (!permissions().contains(permission)) policy.state = "restricted";
    else if (((policy.duration == "session" || policy.duration == "application") && policy.session != session)
        || (policy.duration == "application" && (instance.isEmpty() || policy.instance != instance))
        || (policy.duration == "once" && !policy.instance.isEmpty() && policy.instance != instance)
        || (policy.expires && policy.expires <= QDateTime::currentMSecsSinceEpoch())) policy.state = "not-determined";
    return policy;
}

bool PolicyStore::save(const QMap<QString, PermissionPolicy> &records, QString *error)
{
    QJsonArray policies;
    for (auto policy : records) {
        if (policy.duration == "once") {
            if (!policy.requestedAt) continue;
            policy.state = "not-determined"; policy.duration = "permanent"; policy.expires = 0; policy.session.clear();
        }
        policies.append(policy.json());
    }
    QSaveFile file(m_directory + "/policies.json"); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { *error = file.errorString(); return false; }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"policies", policies}}).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || fsync(file.handle()) || !file.commit()) { *error = file.errorString(); return false; }
    int fd = open(QFile::encodeName(m_directory).constData(), O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    const bool flushed = fd >= 0 && !fsync(fd); if (fd >= 0) close(fd);
    if (!flushed) { *error = "Cannot flush the policy directory"; return false; }
    return true;
}

bool PolicyStore::set(PermissionPolicy policy, QString *error)
{
    if (!validPermission(policy.permission) || (!permissions().contains(policy.permission) && (policy.state != "restricted" || policy.duration != "permanent"))
        || !validState(policy.state) || !validDuration(policy.duration) || policy.requestedAt < 0
        || (policy.duration == "application" && policy.instance.isEmpty()) || policy.instance.size() > 512
        || (!policy.applicationDigest.isEmpty() && !QRegularExpression("^[a-f0-9]{64}$").match(policy.applicationDigest).hasMatch())) {
        *error = "Unsupported policy"; return false;
    }
    auto next = m_records;
    const QString id = key(policy.user, policy.application, policy.permission);
    if (next.contains(id) && next.value(id).requestedAt) policy.requestedAt = next.value(id).requestedAt;
    if (!next.contains(id) && next.size() >= 8192) { *error = "Permission database is full"; return false; }
    policy.created = next.contains(id) ? next.value(id).created : QDateTime::currentMSecsSinceEpoch();
    policy.modified = qMax(policy.created, qMax(policy.requestedAt, QDateTime::currentMSecsSinceEpoch()));
    next.insert(id, policy);
    if (!save(next, error)) return false;
    m_records = next; ++m_revision; return true;
}
QList<PermissionPolicy> PolicyStore::list(quint32 user, const QString &app) const
{
    QList<PermissionPolicy> result;
    for (const auto &policy : m_records) if (policy.user == user && (app.isEmpty() || app == policy.application)) result.append(policy);
    return result;
}

bool PolicyStore::removeApplication(quint32 user, const QString &app, QString *error)
{
    auto next = m_records;
    for (auto it = next.begin(); it != next.end(); ) {
        if (it->user == user && it->application == app) it = next.erase(it); else ++it;
    }
    if (next.size() == m_records.size()) return true;
    if (!save(next, error)) return false;
    m_records = next; ++m_revision; return true;
}
bool PolicyStore::expireInstance(quint32 user, const QString &app, const QString &instance, QString *error)
{
    auto next = m_records; bool changed = false;
    for (auto &policy : next) {
        if (policy.user != user || policy.application != app || policy.instance != instance
            || (policy.duration != "application" && policy.duration != "once")) continue;
        policy.state = "not-determined"; policy.duration = "permanent";
        policy.instance.clear(); policy.expires = 0; policy.modified = qMax(policy.modified, QDateTime::currentMSecsSinceEpoch()); changed = true;
    }
    if (!changed) return true;
    if (!save(next, error)) return false;
    m_records = next; ++m_revision; return true;
}
