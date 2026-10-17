#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <memory>

class ApplicationsClient : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QVariantList &, const QString &)>;
    explicit ApplicationsClient(QObject *parent = nullptr);
    ~ApplicationsClient() override;
    void listApplications(Completion completion);
    void getApplicationInfo(const QString &appId, Completion completion);
    void getRunningApplications(Completion completion);
    void launch(const QString &appId, const QStringList &arguments, Completion completion);
    void createWebApp(const QString &name, const QString &url, Completion completion);
    void createWebApp(const QString &name, const QString &url, const QByteArray &icon, Completion completion);
    void updateWebAppIcon(const QByteArray &icon, Completion completion);
    void uninstallWebApp(const QString &appId, Completion completion);
    void activate(const QString &appId, Completion completion);
    void terminate(const QString &appId, Completion completion);
    void call(const QString &method, const QVariantList &arguments, Completion completion);
signals:
    void applicationStarted(const QString &appId, quint32 pid);
    void applicationStopped(const QString &appId, int status);
    void applicationCrashed(const QString &appId, int status);
    void applicationStateChanged(const QString &appId, const QString &state, quint32 pid, const QString &error);
    void activationRequested(const QString &appId, quint32 pid);
    void launchRequested(const QString &ticket, const QString &appId, const QStringList &arguments);
    void operationFailed(const QString &appId, const QString &error);
    void registryChanged();
    void serviceChanged(bool available);
private:
    struct Transport;
    std::unique_ptr<Transport> m_transport;
};
