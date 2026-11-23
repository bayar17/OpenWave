#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <OpenWave/ApplicationsClient.h>

class FileOpener : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList handlers READ handlers NOTIFY changed)
    Q_PROPERTY(QString effectiveId READ effectiveId NOTIFY changed)
    Q_PROPERTY(QString effectiveName READ effectiveName NOTIFY changed)
    Q_PROPERTY(QString extension READ extension NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    static constexpr auto PackageInstaller = "system.package-installer";

    explicit FileOpener(QObject *parent = nullptr);

    QVariantList handlers() const { return m_handlers; }
    QString effectiveId() const { return m_effectiveId; }
    QString effectiveName() const { return m_effectiveName; }
    QString extension() const { return m_extension; }
    bool ready() const { return m_ready; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }

    Q_INVOKABLE void prepare(const QString &path);
    Q_INVOKABLE void open(const QString &path, const QString &handlerId, bool always = false);
    Q_INVOKABLE void clearMessage();

    static QString extensionOf(const QString &path);
    static QString summarize(int status, const QStringList &lines);

signals:
    void changed();
    void busyChanged();
    void messageChanged();
    void opened();

private:
    void setMessage(const QString &message);
    void install(const QString &path, const QString &handlerId);
    ApplicationsClient m_client;
    QVariantList m_handlers;
    QString m_path;
    QString m_effectiveId, m_effectiveName, m_extension, m_message;
    quint64 m_generation = 0;
    bool m_ready = false, m_busy = false;
};
