#pragma once
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QDebug>
#include <csignal>
#include <memory>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace OpenWaveWaydroidQa {
inline int &signalWriteFd() { static int fd = -1; return fd; }
inline void onSignal(int)
{
    const char byte = 1;
    if (signalWriteFd() >= 0)
        (void)!::write(signalWriteFd(), &byte, 1);
}
}

inline void installWaydroidShellHarness(QObject *owner, QQmlApplicationEngine *engine)
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) return;
    const auto words = cmdline.readAll().trimmed().split(' ');
    if (!words.contains("openwave.qa=waydroid") && !words.contains("openwave.qa=appdeploy")) return;
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds) != 0) return;
    OpenWaveWaydroidQa::signalWriteFd() = fds[0];
    struct sigaction action = {};
    action.sa_handler = OpenWaveWaydroidQa::onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (::sigaction(SIGUSR1, &action, nullptr) != 0) return;
    auto *notifier = new QSocketNotifier(fds[1], QSocketNotifier::Read, owner);
    auto sequence = std::make_shared<int>(0);
    QObject::connect(notifier, &QSocketNotifier::activated, owner, [engine, sequence, fd = fds[1]] {
        char drain[16];
        while (::read(fd, drain, sizeof(drain)) > 0) {}
        if (engine->rootObjects().isEmpty()) return;
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        if (!window) return;
        const QImage image = window->grabWindow();
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&buffer, "JPG", 90)) {
            std::fprintf(stderr, "OCEAN_QA_SHOT_FAILED seq=%d\n", ++*sequence);
            std::fflush(stderr);
            return;
        }
        const QByteArray encoded = jpeg.toBase64();
        const int seq = ++*sequence;
        std::fprintf(stderr, "OCEAN_QA_SHOT_BEGIN seq=%d size=%dx%d bytes=%lld chunks=%lld\n",
                     seq, image.width(), image.height(), static_cast<long long>(jpeg.size()),
                     static_cast<long long>((encoded.size() + 767) / 768));
        for (qsizetype offset = 0, index = 0; offset < encoded.size(); offset += 768, ++index) {
            const QByteArray chunk = encoded.mid(offset, 768);
            const QByteArray digest = QCryptographicHash::hash(chunk, QCryptographicHash::Md5).toHex().left(8);
            std::fprintf(stderr, "OCEAN_QA_SHOT_DATA %lld %s %s\n", static_cast<long long>(index),
                         digest.constData(), chunk.constData());
        }
        std::fprintf(stderr, "OCEAN_QA_SHOT_END seq=%d\n", seq);
        std::fflush(stderr);
    });
}
