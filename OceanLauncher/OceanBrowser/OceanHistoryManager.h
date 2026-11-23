#pragma once

#include <QAbstractListModel>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

class HistoryWorker;

class OceanHistoryManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
public:
    enum Role { HistoryId = Qt::UserRole + 1, StoredUrl, PageTitle, FaviconSource, VisitTime, DateGroup, VisitedAt };
    explicit OceanHistoryManager(QString applicationDataPath, QObject *parent = nullptr);
    ~OceanHistoryManager() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    int count() const { return m_total; }
    QString filter() const { return m_filter; }
    QString sortOrder() const { return m_sort; }
    void setFilter(const QString &);
    void setSortOrder(const QString &);
    Q_INVOKABLE QString recordVisit(const QUrl &url, const QString &title, bool privateTab = false);
    Q_INVOKABLE void setVisitFavicon(const QString &visitToken, const QUrl &iconUrl);
    Q_INVOKABLE void setVisitDetails(const QString &visitToken, const QString &title, const QUrl &iconUrl,
                                    const QUrl &documentUrl = {});
    Q_INVOKABLE void removeEntries(const QVariantList &ids);
    Q_INVOKABLE void clearHistory(qint64 from = 0, qint64 until = 0);
    Q_INVOKABLE void clearDateRange(const QString &from, const QString &through);
    Q_INVOKABLE void clearPeriod(const QString &period);
    Q_INVOKABLE void reload();
signals:
    void stateChanged();
    void countChanged();
    void filterChanged();
    void sortOrderChanged();
    void visitRecorded(const QString &token, qint64 id);
    void historyChanged();
private:
    friend class HistoryWorker;
    void setState(bool ready, const QString &error);
    void query(int offset);
    void applyRows(quint64 generation, int offset, const QList<QVariantMap> &rows, int total);
    QThread m_thread;
    HistoryWorker *m_worker;
    QList<QVariantMap> m_rows;
    QString m_filter, m_sort = QStringLiteral("newest"), m_error;
    bool m_ready = false, m_querying = false;
    int m_total = 0;
    quint64 m_generation = 0;
};
