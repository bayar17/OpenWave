#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QStringList>

class TimeBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString timezone READ timezone NOTIFY changed)
    Q_PROPERTY(QStringList zones READ zones NOTIFY changed)
public:
    explicit TimeBackend(QObject *parent = nullptr);
    ~TimeBackend() override;
    bool serviceAvailable() const { return m_available; }
    bool busy() const { return m_busy; }
    QString timezone() const { return m_timezone; }
    QStringList zones() const { return m_zones; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setTimezone(const QString &zone);
signals:
    void changed();
    void errorOccurred(const QString &message);
private:
    struct Result {
        bool available = false;
        QString timezone;
        QStringList zones;
        QString error;
    };
    void start(const QString &zone);
    static Result execute(const QString &zone, bool fetchZones);
    QThreadPool m_pool;
    QTimer m_refreshTimer;
    bool m_inFlight = false;
    bool m_busy = false;
    bool m_available = false;
    QString m_timezone;
    QStringList m_zones;
    QString m_pendingZone;
};
