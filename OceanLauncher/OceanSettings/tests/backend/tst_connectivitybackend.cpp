#include "../../ConnectivityBackend.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <functional>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

static bool waitUntil(const std::function<bool()> &ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    return ready();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ConnectivityBackend backend;
    QStringList phases;
    QStringList errors;
    QObject::connect(&backend, &ConnectivityBackend::changed, [&] {
        if (!backend.bluetoothActionState().isEmpty() &&
            (phases.isEmpty() || phases.last() != backend.bluetoothActionState()))
            phases.append(backend.bluetoothActionState());
    });
    QObject::connect(&backend, &ConnectivityBackend::errorOccurred,
        [&](const QString &error) { errors.append(error); });
    const QString normal = QStringLiteral("/org/bluez/hci0/dev_01_02_03_04_05_06");
    const QString pairFailure = QStringLiteral("/org/bluez/hci0/dev_02_02_03_04_05_06");
    const QString connectFailure = QStringLiteral("/org/bluez/hci0/dev_03_02_03_04_05_06");
    const auto device = [&](const QString &path) {
        for (const auto &entry : backend.bluetoothDevices())
            if (entry.toMap().value(QStringLiteral("path")) == path) return entry.toMap();
        return QVariantMap();
    };
    CHECK(waitUntil([&] { return backend.serviceAvailable() && backend.bluetoothDevices().size() == 3; }));
    backend.pairBluetoothDevice(normal);
    CHECK(backend.bluetoothActionState() == QStringLiteral("pairing"));
    CHECK(backend.bluetoothActionPath() == normal);
    CHECK(waitUntil([&] { return !backend.busy() && device(normal).value("connected").toBool(); }));
    CHECK(phases == QStringList({QStringLiteral("pairing"), QStringLiteral("connecting")}));
    CHECK(device(normal).value("paired").toBool());
    CHECK(backend.bluetoothActionPath().isEmpty());
    CHECK(errors.isEmpty());

    backend.renameBluetoothDevice(normal, QStringLiteral("  My headphones  "));
    CHECK(waitUntil([&] { return !backend.busy() && device(normal).value("alias") == QStringLiteral("My headphones"); }));
    backend.disconnectBluetoothDevice(normal);
    CHECK(waitUntil([&] { return !backend.busy() && !device(normal).value("connected").toBool(); }));
    CHECK(device(normal).value("paired").toBool());

    const int beforeFailure = errors.size();
    backend.pairBluetoothDevice(pairFailure);
    CHECK(waitUntil([&] { return !backend.busy() && errors.size() > beforeFailure; }));
    CHECK(!device(pairFailure).value("paired").toBool());
    CHECK(backend.bluetoothActionState().isEmpty());
    const int beforeConnectFailure = errors.size();
    backend.pairBluetoothDevice(connectFailure);
    CHECK(waitUntil([&] { return !backend.busy() && errors.size() > beforeConnectFailure; }));
    CHECK(device(connectFailure).value("paired").toBool());
    CHECK(!device(connectFailure).value("connected").toBool());
    CHECK(backend.bluetoothActionState().isEmpty());

    backend.renameBluetoothDevice(normal, QStringLiteral("\n"));
    CHECK(errors.last() == QStringLiteral("Enter a valid device nickname."));
    backend.forgetBluetoothDevice(normal);
    CHECK(waitUntil([&] { return !backend.busy() && device(normal).isEmpty(); }));
    // A power-off queued during Pair must take precedence over automatic Connect.
    backend.pairBluetoothDevice(connectFailure);
    backend.setBluetoothEnabled(false);
    CHECK(waitUntil([&] { return !backend.busy() && !backend.bluetoothEnabled(); }));
    CHECK(backend.bluetoothActionState().isEmpty());
    std::puts("PASS: pairing/connect phases, nickname, disconnect, forget, failures, queued power-off");
    return 0;
}
