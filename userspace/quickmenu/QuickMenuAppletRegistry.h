#pragma once

#include <functional>
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

class QuickMenuAppletRegistry : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AppletRegistry)
    QML_SINGLETON
    Q_PROPERTY(QVariantList panelApplets READ panelApplets NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList residentApplets READ residentApplets NOTIFY changed FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)

public:
    enum Kind {
        Needed,
        System,
        User
    };
    Q_ENUM(Kind)

    struct Root {
        Kind kind;
        QString path;
    };

    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        IconRole,
        KindRole,
        RequiredRole,
        EnabledRole,
        OrderRole,
        ApiVersionRole,
        EntryUrlRole,
        SettingsUrlRole,
        LoadErrorRole
    };
    Q_ENUM(Role)

    static constexpr int SupportedApiVersion = 1;
    static constexpr int DefaultOrder = 1000;

    QuickMenuAppletRegistry(const QList<Root> &roots, const QString &preferencesPath,
                            QObject *parent = nullptr);
    QuickMenuAppletRegistry() = delete;

    static QuickMenuAppletRegistry *shared();
    static QuickMenuAppletRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static QList<Root> defaultRoots();
    static bool isValidAppletId(const QString &id);
    static QString kindName(Kind kind);

    int count() const;
    QVariantList panelApplets() const;
    QVariantList residentApplets() const;
    QVariantList rejections() const;
    QStringList orderedIds() const;

    Q_INVOKABLE QUrl entryUrl(const QString &id);
    Q_INVOKABLE QVariantMap applet(const QString &id) const;
    Q_INVOKABLE void reportLoadFailure(const QString &id, const QString &reason);

    QUrl settingsUrl(const QString &id) const;
    bool setEnabled(const QString &id, bool enabled);
    bool move(const QString &id, int toIndex);
    bool resetState();
    void setRoots(const QList<Root> &roots);
    using PreferenceWriter = std::function<bool(const QString &key, const QVariant &value)>;
    void setPreferencesPath(const QString &path);
    void setPreferenceWriter(PreferenceWriter writer);
    void reload();
    void reloadState();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();
    void appletRejected(const QString &path, const QString &reason);
    void loadFailed(const QString &id, const QString &reason);

private:
    struct Entry {
        QString id;
        QString name;
        QString description;
        QString icon;
        Kind kind = User;
        int order = DefaultOrder;
        int apiVersion = 0;
        QUrl entryUrl;
        QUrl settingsUrl;
        QString directory;
        QString rootPath;
        bool enabled = false;
        QString loadError;
    };

    static QVariantMap toMap(const Entry &entry);
    QList<Entry> scan(QVariantList &rejected);
    bool load(const QString &canonicalRoot, const QString &directory, Kind kind, Entry &entry,
              QString &reason) const;
    void applyState(QList<Entry> &entries) const;
    bool defaultEnabled(Kind kind) const;
    void watchPaths();
    void scheduleReload(bool rescan);
    bool publish(QList<Entry> entries, bool keepFailures);

    QList<Root> m_roots;
    QString m_preferencesPath;
    PreferenceWriter m_writer;
    bool store(const QString &key, const QVariant &value);
    QList<Entry> m_entries;
    QVariantList m_rejections;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    bool m_pendingRescan = false;
};
