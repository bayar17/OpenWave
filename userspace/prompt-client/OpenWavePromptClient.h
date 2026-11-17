#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include "PromptTransport.h"

class OpenWavePromptClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString requestId READ requestId NOTIFY requestIdChanged)
public:
    explicit OpenWavePromptClient(QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    QString requestId() const { return m_id; }
    Q_INVOKABLE void message(const QString &title, const QString &body, const QVariantMap &options = {});
    Q_INVOKABLE void confirmation(const QString &title, const QString &body, const QVariantMap &options = {});
    Q_INVOKABLE void textInput(const QString &title, const QString &body, const QVariantMap &options = {});
    Q_INVOKABLE void passwordInput(const QString &title, const QString &body, const QVariantMap &options = {});
    Q_INVOKABLE void form(const QString &title, const QString &body, const QVariantMap &options);
    Q_INVOKABLE void selection(const QString &title, const QString &body, const QVariantMap &options);
    Q_INVOKABLE void cancel();
signals:
    void busyChanged();
    void requestIdChanged();
    void finished(const QString &requestId, const QString &result, const QString &text);
    void fieldsFinished(const QString &requestId, const QString &result, const QVariantMap &values);
    void errorOccurred(const QString &message);
private:
    void start(const QString &type, const QString &title, const QString &body, QVariantMap options);
    void poll();
    void finish(const QString &result, const QString &text, const QString &error = {});
    PromptTransport m_transport;
    QTimer m_timer;
    QString m_id;
    QString m_type;
    bool m_busy = false, m_polling = false, m_cancelPending = false, m_cancelSent = false;
};
