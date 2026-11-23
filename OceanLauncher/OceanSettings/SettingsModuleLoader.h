#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QQmlComponent;
class QQmlContext;
class SettingsModuleRegistry;

class SettingsModuleLoader : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    Q_PROPERTY(QQuickItem *item READ item NOTIFY itemChanged FINAL)
    Q_PROPERTY(QString activeModuleId READ activeModuleId NOTIFY activeModuleIdChanged FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged FINAL)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)

public:
    enum Status { Idle, Loading, Ready, Error };
    Q_ENUM(Status)

    explicit SettingsModuleLoader(QObject *parent = nullptr);
    ~SettingsModuleLoader() override;

    QQuickItem *host() const;
    void setHost(QQuickItem *host);
    QQuickItem *item() const;
    QString activeModuleId() const;
    bool loaded() const;
    Status status() const;
    QString errorString() const;

    void setRegistry(SettingsModuleRegistry *registry);
    SettingsModuleRegistry *registry() const;

    Q_INVOKABLE bool load(const QString &moduleId);
    Q_INVOKABLE void unload();

signals:
    void hostChanged();
    void itemChanged();
    void activeModuleIdChanged();
    void loadedChanged();
    void statusChanged();
    void errorStringChanged();
    void moduleLoaded(const QString &moduleId);
    void moduleUnloaded(const QString &moduleId);
    void loadFailed(const QString &moduleId, const QString &reason);

private:
    bool verifyTree(const QString &directory, QString &reason) const;
    bool compile(const QString &moduleId, const QByteArray &source, const QUrl &url);
    void finishComponent(const QString &moduleId, quint64 token);
    void install(const QString &moduleId, QQuickItem *item, QQmlContext *context);
    void fail(const QString &moduleId, const QString &reason);
    void discard();
    void setStatus(Status status);
    void setErrorString(const QString &errorString);
    void fitToHost();

    QPointer<QQuickItem> m_host;
    QPointer<QQuickItem> m_item;
    QPointer<QQmlContext> m_context;
    QPointer<QQmlComponent> m_component;
    QPointer<SettingsModuleRegistry> m_registry;
    QString m_activeModuleId;
    QString m_pendingModuleId;
    QString m_errorString;
    Status m_status = Idle;
    quint64 m_token = 0;
};
