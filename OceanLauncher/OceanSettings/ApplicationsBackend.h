#pragma once
#include <QObject>
#include <QVariantList>
#include <OpenWave/ApplicationsClient.h>

class ApplicationsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList applications READ applications NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit ApplicationsBackend(QObject *parent = nullptr);
    QVariantList applications() const { return m_applications; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
signals:
    void changed();
private:
    ApplicationsClient m_client;
    QVariantList m_applications;
    QString m_error;
    bool m_busy = false, m_again = false;
};
