#pragma once
#include <QObject>
#include <QVariantMap>

class AppController;

class UninstallController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap request READ request NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit UninstallController(AppController *apps, QObject *parent = nullptr) : QObject(parent), m_apps(apps) {}
    QVariantMap request() const { return m_request; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE void ask(const QString &appId, const QString &name);
    Q_INVOKABLE void respond(const QString &id, const QString &result, const QString &value = {});
signals:
    void changed();
    void errorOccurred(const QString &error);
private:
    AppController *m_apps;
    QVariantMap m_request;
    QString m_appId;
    bool m_busy = false;
};
