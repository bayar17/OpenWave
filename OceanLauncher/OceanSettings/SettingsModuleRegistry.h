#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

class SettingsModuleRegistry : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(QString moduleRoot READ moduleRoot NOTIFY moduleRootChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        IconRole,
        CategoryRole,
        OrderRole,
        ApiVersionRole,
        EntryUrlRole
    };
    Q_ENUM(Role)

    static constexpr int SupportedApiVersion = 1;
    static constexpr int DefaultOrder = 1000;

    explicit SettingsModuleRegistry(const QString &moduleRoot, QObject *parent = nullptr);
    SettingsModuleRegistry() = delete;

    static SettingsModuleRegistry *shared();
    static SettingsModuleRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static QString defaultModuleRoot();
    static bool isValidModuleId(const QString &id);

    QAbstractItemModel *model();
    int count() const;
    QString moduleRoot() const;
    void setModuleRoot(const QString &moduleRoot);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QUrl entryUrl(const QString &id) const;
    Q_INVOKABLE QVariantMap module(const QString &id) const;
    Q_INVOKABLE QVariantList modules() const;

    struct ValidatedModule {
        QString directory;
        QUrl entryUrl;
        QByteArray entrySource;
    };
    bool revalidate(const QString &id, ValidatedModule &validated, QString &reason) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void moduleRootChanged();
    void moduleRejected(const QString &path, const QString &reason);
    void scanFailed(const QString &root, const QString &reason);
    void reloaded();

private:
    struct Entry {
        QString id;
        QString name;
        QString description;
        QString icon;
        QString category;
        int order = DefaultOrder;
        int apiVersion = 0;
        QUrl entryUrl;
        QVariantList search;
        QString directory;
    };

    static QVariantMap toMap(const Entry &entry);
    QList<Entry> scan(const QString &root);
    bool load(const QString &canonicalRoot, const QString &directory, Entry &entry,
              QString &reason, QByteArray *entrySource = nullptr) const;
    
    QString m_moduleRoot;
    QList<Entry> m_entries;
};
