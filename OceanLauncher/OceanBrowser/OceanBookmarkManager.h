#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

class OceanBookmarkManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Role {
        BookmarkId = Qt::UserRole + 1,
        StoredUrl,
        PageTitle,
        Host,
        AddedText
    };
    static constexpr int MaximumBookmarks = 5000;
    static constexpr int MaximumUrlLength = 2048;
    static constexpr int MaximumTitleLength = 200;

    explicit OceanBookmarkManager(const QString &dataPath, QObject *parent = nullptr);

    int count() const { return m_entries.size(); }
    int revision() const { return m_revision; }
    QString error() const { return m_error; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);

    Q_INVOKABLE bool contains(const QString &url) const;
    Q_INVOKABLE bool toggle(const QString &url, const QString &title);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void removeAll(const QStringList &ids);
    Q_INVOKABLE void clear();
    Q_INVOKABLE static QString normalized(const QString &url);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();
    void errorChanged();
    void filterChanged();

private:
    struct Entry {
        QString id;
        QString url;
        QString title;
        qint64 added = 0;
    };
    void load();
    bool save();
    void applyFilter();
    void fail(const QString &message);

    QString m_path;
    QList<Entry> m_entries;
    QList<int> m_visible;
    QString m_filter;
    QString m_error;
    int m_revision = 0;
};
