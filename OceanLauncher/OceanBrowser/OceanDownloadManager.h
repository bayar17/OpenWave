#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <memory>

class BrowserPreferences;
class DownloadStore;
class OceanDownloadManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
public:
    enum Role { DownloadId = Qt::UserRole+1, FileName, Destination, MimeType, TotalBytes,
        ReceivedBytes, Progress, Status, Speed, DateText, ErrorDetail, CanCancel, CanPause,
        CanResume, CanRetry, FileExists, PrivateDownload, ThumbnailSource };
    Q_INVOKABLE void refresh() { if (rowCount()) emit dataChanged(index(0), index(rowCount()-1)); }
    OceanDownloadManager(QString applicationPath, QString userPath, BrowserPreferences *preferences, QObject *parent = nullptr);
    ~OceanDownloadManager() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    QString error() const { return m_error; }
    bool ready() const { return m_ready; }
    Q_INVOKABLE void attachView(QObject *view, bool privateTab);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void retry(const QString &id);
    Q_INVOKABLE void removeRecord(const QString &id);
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void openFile(const QString &id);
    Q_INVOKABLE void showInFiles(const QString &id);
    Q_INVOKABLE void saveResource(const QString &url, bool image, bool saveAs);
    Q_INVOKABLE void savePageAs();
    Q_INVOKABLE void setActiveView(QObject *view) { m_activeView = view; }
    static QString sanitizeFilename(QString);
signals:
    void stateChanged();
    void completed(const QString &filename);
    void contextMenuRequested(const QString &imageUrl, const QString &linkUrl);
private:
    friend class DownloadStore;
    struct Entry;
    struct Context;
    struct Session;
    using Item = std::shared_ptr<Entry>;
    Item find(const QString &) const;
    Item track(void *native, Session *session);
    void decide(const Item &, const QString &name);
    void beginTransfer(const Item &);
    void finishTransfer(const Item &);
    void fail(const Item &, const QString &, bool cancelled = false);
    void changed(const Item &, bool persist = true);
    void cleanup(const Item &);
    void releaseView(QObject *);
    void errorMessage(const QString &);
    QString m_path, m_user, m_error;
    BrowserPreferences *m_preferences;
    bool m_ready = false, m_closing = false;
    QList<Item> m_items;
    QHash<QObject *, Context *> m_views;
    QHash<void *, Session *> m_sessions;
    QPointer<QObject> m_activeView;
    QThread m_storeThread;
    DownloadStore *m_store;
    QThreadPool m_io;
    QTimer m_progress;
};
