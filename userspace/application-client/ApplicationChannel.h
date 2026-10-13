#pragma once

#include <QObject>
#include <QJsonObject>
#include <QLocalServer>
#include <functional>

// appd owns launching. PID endpoints retain the existing activation/quit wire
// protocol so applications can migrate without changing their handlers.
namespace OpenWaveApplications {
QString launcherAddress();
QString instanceAddress(qint64 pid);
using Completion = std::function<void(const QString &error)>;
void request(const QString &address, const QJsonObject &message,
             QObject *owner, Completion completion);
void launch(const QString &appId, const QStringList &arguments,
            QObject *owner, Completion completion);
}

class ApplicationChannel : public QObject
{
public:
    using Handler = std::function<QString(const QJsonObject &)>;
    ApplicationChannel(const QString &address, Handler handler, QObject *parent = nullptr);
private:
    QLocalServer m_server;
};
