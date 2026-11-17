#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariantMap>

class PowerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap request READ request NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit PowerController(QObject *parent = nullptr) : QObject(parent) {}
    ~PowerController() override { m_pool.waitForDone(); }
    QVariantMap request() const { return m_request; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE void ask(const QString &action);
    Q_INVOKABLE void respond(const QString &id, const QString &result, const QString &value = {});
signals:
    void changed();
    void errorOccurred(const QString &error);
private:
    QVariantMap m_request;
    bool m_busy = false;
    QString m_action;
    QThreadPool m_pool;
};
