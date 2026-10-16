#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct PermissionPolicy {
    QString application, permission, state = "not-determined", duration = "permanent", session, instance, applicationDigest;
    quint32 user = 0;
    bool revocationPending = false;
    qint64 created = 0, modified = 0, expires = 0, requestedAt = 0;
    QJsonObject json() const;
};
class PolicyStore {
public:
    explicit PolicyStore(QString directory);
    bool load(QString *error);
    PermissionPolicy get(quint32 user, const QString &app, const QString &permission, const QString &session, const QString &instance = {}) const;
    bool set(PermissionPolicy policy, QString *error);
    QList<PermissionPolicy> list(quint32 user, const QString &app = {}) const;
    bool removeApplication(quint32 user, const QString &app, QString *error);
    bool expireInstance(quint32 user, const QString &app, const QString &instance, QString *error);
    quint64 revision() const { return m_revision; }
    static QStringList permissions();
    static bool validState(const QString &state);
    static bool validDuration(const QString &duration);
    static bool validPermission(const QString &permission);
private:
    QString key(quint32 user, const QString &app, const QString &permission) const;
    bool save(const QMap<QString, PermissionPolicy> &records, QString *error);
    QString m_directory;
    QMap<QString, PermissionPolicy> m_records;
    quint64 m_revision = 1;
};
