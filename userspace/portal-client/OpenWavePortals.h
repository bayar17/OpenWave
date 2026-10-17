#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QTemporaryDir;

class PortalGrant : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool writable READ writable CONSTANT)
    Q_PROPERTY(bool directory READ directory CONSTANT)
    Q_PROPERTY(QString directoryToken READ directoryToken CONSTANT)
public:
    PortalGrant(QString name, int descriptor, bool writable, bool directory,
                QObject *parent = nullptr);
    PortalGrant(QString name, int descriptor, bool writable, bool directory, QObject *parent, QString directoryToken);
    ~PortalGrant() override;
    QString name() const { return m_name; }
    bool writable() const { return m_writable; }
    bool directory() const { return m_directory; }
    QString directoryToken() const { return property("_openwaveDirectoryToken").toString(); }
    int descriptor() const { return m_descriptor; }
    Q_INVOKABLE QUrl temporaryCopy();
    Q_INVOKABLE QByteArray readAll(int limit = 16 * 1024 * 1024);
    Q_INVOKABLE bool writeAll(const QByteArray &bytes);
private:
    QString m_name;
    int m_descriptor;
    bool m_writable;
    bool m_directory;
    QTemporaryDir *m_temporaryDir = nullptr;
    QUrl m_temporaryUrl;
};

class OpenWavePortals : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit OpenWavePortals(QObject *parent = nullptr);
    ~OpenWavePortals() override;
    bool busy() const { return m_busy; }
    Q_INVOKABLE void openFile(const QVariantMap &options = {});
    Q_INVOKABLE void openFiles(const QVariantMap &options = {});
    Q_INVOKABLE void saveFile(const QVariantMap &options = {});
    Q_INVOKABLE void createFile(const QString &directoryToken, const QString &filename);
    Q_INVOKABLE void selectDirectory(const QVariantMap &options = {});
    Q_INVOKABLE void cancelRequest();
signals:
    void busyChanged();
    void completed(const QVariantList &grants);
    void cancelled();
    void errorOccurred(const QString &message);
private:
    struct Result {
        uint status = 0;
        QString error;
        QString directoryToken;
        QStringList names;
        QVector<int> descriptors;
    };
    void start(const QString &method, const QVariantMap &options);
    void poll();
    void finish(const Result &result);
    static Result takeResult(const QString &requestId, void *connection, bool directory);
    QThreadPool m_pool;
    QTimer m_timer;
    QString m_requestId;
    QString m_method;
    QList<PortalGrant *> m_grants;
    bool m_busy = false;
    bool m_polling = false;
    bool m_cancelWhenReady = false;
    void *m_bus = nullptr;
};
