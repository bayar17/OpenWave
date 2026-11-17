#include <QCoreApplication>
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QRegularExpression>
#include <OpenWave/OpenWavePromptClient.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/WallpaperClient.h>
#include <OpenWave/TimeBackend.h>
#include <OpenWave/ConnectivityBackend.h>
#include <OpenWave/QuickMenuBackend.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile report("/var/lib/openwave/sdk-probe.log");
    if (!report.open(QIODevice::WriteOnly)) return 2;
    const auto log = [&report](const QString &text) { report.write((text + '\n').toUtf8()); report.flush(); std::printf("SDK-PROBE %s\n", text.toUtf8().constData()); std::fflush(stdout); };
    TimeBackend time;
    WallpaperClient wallpaper;
    ConnectivityBackend connectivity;
    QuickMenuBackend controls;
    OpenWavePromptClient prompt;
    OpenWavePortals portal;
    bool failed = false, promptDone = false, portalDone = false, lifecycleDone = false;
    const auto error = [&](const QString &message) { failed = true; log("ERROR " + message); };
    QObject::connect(&prompt, &OpenWavePromptClient::errorOccurred, &app, error);
    QObject::connect(&portal, &OpenWavePortals::errorOccurred, &app, error);
    QObject::connect(&time, &TimeBackend::errorOccurred, &app, error);
    QObject::connect(&wallpaper, &WallpaperClient::errorOccurred, &app, error);
    QObject::connect(&prompt, &OpenWavePromptClient::finished, &app,
        [&](const QString &, const QString &result, const QString &) {
            promptDone = result == "cancelled"; log("PROMPT " + result);
            portal.openFile();
            QTimer::singleShot(2000, &portal, &OpenWavePortals::cancelRequest);
        });
    const auto instancePid = [](const QString &name) -> qint64 {
        const QDir runtime(qEnvironmentVariable("XDG_RUNTIME_DIR", "/run/ocean"));
        for (const QString &socket : runtime.entryList({"openwave-app-*.socket"}, QDir::System | QDir::Files)) {
            const qint64 pid = socket.mid(13).section('.', 0, 0).toLongLong();
            QFile comm(QString("/proc/%1/comm").arg(pid));
            if (comm.open(QIODevice::ReadOnly) && QString::fromUtf8(comm.readAll()).trimmed() == name) return pid;
        }
        return 0;
    };
    QObject::connect(&portal, &OpenWavePortals::cancelled, &app, [&] {
        portalDone = true; log("PORTAL cancelled");
        OpenWaveApplications::launch("files", {}, &app, [&](const QString &message) {
            if (!message.isEmpty()) { error(message); return; }
            log("LAUNCH files");
            QTimer::singleShot(3000, &app, [&] {
                const qint64 pid = instancePid("ocean-files");
                if (!pid) { error("Files did not start"); return; }
                OpenWaveApplications::request(OpenWaveApplications::instanceAddress(pid), {{"command", "quit"}}, &app,
                    [&](const QString &message) {
                        if (!message.isEmpty()) { error(message); return; }
                        log("QUIT files");
                        QTimer::singleShot(1500, &app, [&] {
                            if (instancePid("ocean-files")) { error("Files did not stop"); return; }
                            OpenWaveApplications::launch("browser", {}, &app, [&](const QString &message) {
                                if (!message.isEmpty()) { error(message); return; }
                                log("LAUNCH browser");
                                QTimer::singleShot(4000, &app, [&] {
                                    const qint64 browser = instancePid("ocean-browser");
                                    if (!browser) { error("Browser did not start"); return; }
                                    OpenWaveApplications::request(OpenWaveApplications::instanceAddress(browser), {{"command", "quit"}}, &app,
                                        [&](const QString &message) {
                                            if (!message.isEmpty()) error(message);
                                            else { lifecycleDone = true; log("QUIT browser"); }
                                        });
                                });
                            });
                        });
                    });
            });
        });
    });
    QTimer::singleShot(2000, &app, [&] {
        log("TIME " + time.timezone());
        log("WALLPAPER " + wallpaper.source().toString());
        if (!time.serviceAvailable() || time.timezone().isEmpty()) error("Time service unavailable");
        prompt.message("SDK integration check", "Checking native prompt cancellation");
        QTimer::singleShot(1500, &prompt, &OpenWavePromptClient::cancel);
    });
    QTimer::singleShot(22000, &app, [&] {
        if (!promptDone || !portalDone || !lifecycleDone) error("Prompt/portal cancellation did not complete");
        log(failed ? "FAIL" : "PASS");
        app.exit(failed ? 1 : 0);
    });
    return app.exec();
}
