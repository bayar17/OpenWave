#include "ApplicationChannel.h"
#include "ApplicationsClient.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QDebug>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int maximumMessage = 16384;
QString address(const QString &name)
{
    return QDir(qEnvironmentVariable("XDG_RUNTIME_DIR", "/run/ocean")).filePath(name);
}
bool sameUser(QLocalSocket *socket)
{
    struct ucred peer {};
    socklen_t size = sizeof(peer);
    return getsockopt(socket->socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
        && peer.uid == geteuid();
}
}

QString OpenWaveApplications::launcherAddress()
{
    return address(QStringLiteral("openwave-applications.socket"));
}

QString OpenWaveApplications::instanceAddress(qint64 pid)
{
    return address(QStringLiteral("openwave-app-%1.socket").arg(pid));
}

void OpenWaveApplications::request(const QString &path, const QJsonObject &message,
                                  QObject *owner, Completion completion)
{
    auto *socket = new QLocalSocket(owner);
    auto *timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    const auto finish = [socket, timeout, completion = std::move(completion)](const QString &error) {
        if (socket->property("completed").toBool()) return;
        socket->setProperty("completed", true);
        timeout->stop();
        socket->abort();
        socket->deleteLater();
        completion(error);
    };
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket, message, finish] {
        if (!sameUser(socket)) {
            finish(QStringLiteral("Application endpoint belongs to another user."));
            return;
        }
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    });
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, finish] {
        if (socket->bytesAvailable() > maximumMessage) {
            finish(QStringLiteral("Invalid application response."));
            return;
        }
        if (!socket->canReadLine()) return;
        const QJsonDocument reply = QJsonDocument::fromJson(socket->readLine());
        if (!reply.isObject() || !reply.object().contains(QStringLiteral("error")))
            finish(QStringLiteral("Invalid application response."));
        else finish(reply.object().value(QStringLiteral("error")).toString());
    });
    QObject::connect(socket, &QLocalSocket::errorOccurred, socket,
        [finish](QLocalSocket::LocalSocketError) {
            finish(QStringLiteral("Application manager or instance is unavailable."));
        });
    QObject::connect(timeout, &QTimer::timeout, socket, [finish] {
        finish(QStringLiteral("Application request timed out."));
    });
    timeout->start(2000);
    socket->connectToServer(path);
}

void OpenWaveApplications::launch(const QString &appId, const QStringList &arguments,
                                 QObject *owner, Completion completion)
{
    auto *client = new ApplicationsClient(owner);
    client->launch(appId, arguments, [client, completion = std::move(completion)](const QVariantList &, const QString &error) {
        completion(error);
        client->deleteLater();
    });
}

ApplicationChannel::ApplicationChannel(const QString &path, Handler handler, QObject *parent)
    : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path) && m_server.serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (!probe.waitForConnected(100)) {
            QLocalServer::removeServer(path);
            m_server.listen(path);
        }
    }
    if (!m_server.isListening())
        qWarning().noquote() << "Application endpoint:" << m_server.errorString();
    connect(&m_server, &QLocalServer::newConnection, this, [this, handler] {
        while (auto *socket = m_server.nextPendingConnection()) {
            socket->setParent(this);
            if (!sameUser(socket)) { socket->abort(); socket->deleteLater(); continue; }
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(3000, socket, [socket] { socket->abort(); socket->deleteLater(); });
            connect(socket, &QLocalSocket::readyRead, socket, [socket, handler] {
                if (socket->property("handled").toBool()) return;
                if (socket->bytesAvailable() > maximumMessage) { socket->abort(); return; }
                if (!socket->canReadLine()) return;
                socket->setProperty("handled", true);
                const QJsonDocument message = QJsonDocument::fromJson(socket->readLine());
                const QString error = message.isObject() ? handler(message.object())
                    : QStringLiteral("Invalid application request.");
                socket->write(QJsonDocument(QJsonObject{{"error", error}})
                              .toJson(QJsonDocument::Compact) + '\n');
                socket->disconnectFromServer();
            });
        }
    });
}
