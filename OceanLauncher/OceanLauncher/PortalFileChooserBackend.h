#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QStringList>

class PortalFileChooserBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString mutedTitle READ mutedTitle NOTIFY changed)
    Q_PROPERTY(QString primaryTitle READ primaryTitle NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString initialDirectory READ initialDirectory NOTIFY changed)
    Q_PROPERTY(QString suggestedFilename READ suggestedFilename NOTIFY changed)
    Q_PROPERTY(QString applicationId READ applicationId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit PortalFileChooserBackend(QObject *parent = nullptr);
    ~PortalFileChooserBackend() override;
    QString requestId() const { return m_requestId; }
    QString title() const { return m_title; }
    QString mutedTitle() const { return m_mutedTitle; }
    QString primaryTitle() const { return m_primaryTitle; }
    QString mode() const { return m_mode; }
    QString initialDirectory() const { return m_initialDirectory; }
    QString suggestedFilename() const { return m_suggestedFilename; }
    QString applicationId() const { return m_applicationId; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE bool acceptsPath(const QString &path) const;
    Q_INVOKABLE void choose(const QStringList &paths);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void errorOccurred(const QString &message);
private:
    struct Pending {
        QString id, title, mutedTitle, primaryTitle, mode, initialDirectory;
        QString suggestedFilename, applicationId;
        QStringList mimeTypes;
        QString error;
    };
    static Pending next(bool *registered, void *connection);
    void poll();
    void complete(uint status, const QStringList &paths);
    QThreadPool m_pool;
    QTimer m_timer;
    QString m_requestId, m_title, m_mutedTitle, m_primaryTitle;
    QString m_mode, m_initialDirectory;
    QString m_suggestedFilename, m_applicationId;
    QStringList m_mimeTypes;
    bool m_registered = false;
    bool m_polling = false;
    bool m_busy = false;
    QString m_lastError;
    void *m_bus = nullptr;
};
