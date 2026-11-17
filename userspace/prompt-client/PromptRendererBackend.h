#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include "PromptTransport.h"

class PromptRendererBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap request READ request NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit PromptRendererBackend(QObject *parent = nullptr);
    QVariantMap request() const { return m_request; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE void respond(const QString &id, const QString &result, const QString &text = {});
    Q_INVOKABLE void respondFields(const QString &id, const QString &result, const QVariantMap &values);
signals:
    void changed();
    void errorOccurred(const QString &message);
private:
    void poll();
    void fetch();
    void unavailable();
    PromptTransport m_transport;
    QTimer m_timer;
    QVariantMap m_request;
    bool m_registered = false, m_polling = false, m_busy = false;
};
