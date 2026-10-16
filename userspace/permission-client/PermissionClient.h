#pragma once
#include <QAbstractListModel>
#include <QVariantList>
#include <functional>
#include <memory>

namespace OpenWave {
class PermissionClient : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
public:
    enum Status { NotDetermined, Granted, Denied, Restricted };
    Q_ENUM(Status)
    enum Role { ApplicationRole = Qt::UserRole + 1, NameRole, PermissionRole, LabelRole, StateRole, DurationRole, TrustedRole };
    using Completion = std::function<void(const QVariantList &, const QString &)>;
    explicit PermissionClient(QObject *parent = nullptr);
    ~PermissionClient() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }
    QString error() const { return m_error; }
    QVariantList records() const { return m_records; }
    Q_INVOKABLE void requestPermission(const QString &permission);
    Q_INVOKABLE void checkPermission(const QString &permission);
    Q_INVOKABLE void getPermissionStatus(const QString &permission);
    Q_INVOKABLE void listPermissions();
    Q_INVOKABLE void subscribePermissionChanges();
    Q_INVOKABLE void refreshApplications();
    Q_INVOKABLE void revokePermission(const QString &application, const QString &permission);
    Q_INVOKABLE void setPermission(const QString &application, const QString &permission, const QString &state, const QString &duration = QStringLiteral("permanent"));
    void call(const QString &method, const QStringList &arguments, Completion completion);
    using PermissionCompletion = std::function<void(Status, const QString &)>;
    void requestPermissionAsync(const QString &permission, PermissionCompletion completion);
    void checkPermissionAsync(const QString &permission, PermissionCompletion completion);
signals:
    void permissionResult(const QString &permission, const QString &state);
    void permissionChanged(const QString &application, const QString &permission, const QString &state);
    void busyChanged();
    void availableChanged();
    void errorChanged();
    void recordsChanged();
    void errorOccurred(const QString &message);
private:
    void refresh(bool administrative);
    void resultError(const QString &error);
    struct Transport;
    std::unique_ptr<Transport> m_transport;
    QVariantList m_records;
    bool m_busy = false, m_available = false, m_administrative = false;
    QString m_error;
};
}
