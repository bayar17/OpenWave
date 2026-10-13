#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/ApplicationChannel.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>
#include <QTimer>
#include <csignal>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
int failures = 0;
void check(bool okay, const QString &label)
{
    QTextStream(stdout) << "APPD_QA " << (okay ? "PASS " : "FAIL ") << label << Qt::endl;
    if (!okay) ++failures;
}
void delay(int ms)
{
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
QVariantList rpc(ApplicationsClient &client, const QString &method, const QVariantList &arguments, QString *error = nullptr)
{
    QEventLoop loop; QVariantList reply; QString failure; bool done = false;
    client.call(method, arguments, [&](const QVariantList &values, const QString &message) {
        reply = values; failure = message; done = true; loop.quit();
    });
    QTimer::singleShot(8000, &loop, &QEventLoop::quit); loop.exec();
    if (!done) { QTextStream(stderr) << "APPD_QA RPC timeout " << method << Qt::endl; _exit(90); }
    if (error) *error = failure;
    else if (!failure.isEmpty()) check(false, method + ": " + failure);
    return reply;
}
QVariantMap info(ApplicationsClient &client, const QString &id)
{
    return rpc(client, "GetApplicationInfo", {id}).value(0).toMap();
}
bool waitState(ApplicationsClient &client, const QString &id, const QString &state, int timeout = 10000)
{
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < timeout) {
        if (info(client, id).value("state").toString() == state) return true;
        delay(100);
    }
    return false;
}
qint64 pidfile(const QString &name)
{
    QFile file(name); return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toLongLong() : 0;
}
bool alive(qint64 pid)
{
    return pid > 1 && QFile::exists(QStringLiteral("/proc/%1/stat").arg(pid));
}
bool write(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size()
        && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
}
QString processField(qint64 pid, const QString &key)
{
    QFile file(QStringLiteral("/proc/%1/status").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    for (const auto &line : file.readAll().split('\n'))
        if (line.startsWith(key.toUtf8() + ':')) return QString::fromUtf8(line.mid(key.size() + 1)).trimmed();
    return {};
}
void fixture(const QByteArray &arguments)
{
    check(write("/usr/share/applications/appd-fixture.desktop",
        "[Desktop Entry]\nType=Application\nName=Discovered Fixture\nX-OpenWave-AppId=fixture\nExec=/usr/libexec/appd-qa --helper "
        + arguments + "\nIcon=missing-icon\nTerminal=false\n"), "write discovery fixture");
}
int helper(QCoreApplication &app)
{
    const bool stubborn = app.arguments().contains("--stubborn");
    if (stubborn) {
        signal(SIGTERM, SIG_IGN);
        pid_t child = fork();
        if (!child) { setsid(); signal(SIGTERM, SIG_IGN); for (;;) pause(); }
        write("/tmp/appd-fixture-child", QByteArray::number(child));
    }
    QJsonObject env{{"platform", qEnvironmentVariable("QT_QPA_PLATFORM")},
        {"eglfs", qEnvironmentVariable("QT_QPA_EGLFS_KMS_CONFIG")}, {"preload", qEnvironmentVariable("LD_PRELOAD")},
        {"noNewPrivileges", prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0)}, {"processGroup", int(getpgrp())},
        {"pid", int(getpid())}, {"uid", int(geteuid())}};
    write("/tmp/appd-fixture-environment", QJsonDocument(env).toJson(QJsonDocument::Compact));
    ApplicationChannel channel(OpenWaveApplications::instanceAddress(app.applicationPid()),
        [&](const QJsonObject &request) {
            if (request.value("command") == "quit" && !stubborn) QTimer::singleShot(50, &app, &QCoreApplication::quit);
            return QString();
        });
    return app.exec();
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--helper")) return helper(app);
    ApplicationsClient client;
    int starts = 0, stops = 0, crashes = 0, activations = 0, registryEvents = 0;
    QObject::connect(&client, &ApplicationsClient::applicationStarted, &app, [&](const QString &, quint32) { ++starts; });
    QObject::connect(&client, &ApplicationsClient::applicationStopped, &app, [&](const QString &, int) { ++stops; });
    QObject::connect(&client, &ApplicationsClient::applicationCrashed, &app, [&](const QString &, int) { ++crashes; });
    QObject::connect(&client, &ApplicationsClient::activationRequested, &app, [&](const QString &, quint32) { ++activations; });
    QObject::connect(&client, &ApplicationsClient::registryChanged, &app, [&] { ++registryEvents; });
    QString error;
    for (int i = 0; i < 60; ++i) {
        rpc(client, "ListApplications", {}, &error);
        if (error.isEmpty()) break;
        delay(250);
    }
    check(error.isEmpty(), "SDK authenticates appd");
    if (!error.isEmpty() || rpc(client, "ListApplications", {}).value(0).toList().size() < 3) {
        check(false, "built-in application registry is available"); return 1;
    }
    const qint64 daemon = pidfile("/run/openwave/appd.pid");
    check(daemon > 1, "init supervises appd");
    check(info(client, "settings").value("icon").toString().endsWith("ocean-settings.png"),
        "registry resolves bitmap icons larger than metadata limits");
    rpc(client, "RegisterShell", {}, &error);
    check(!error.isEmpty(), "reject forged shell registration");
    rpc(client, "Launch", {"../not-an-app", QStringList{}}, &error);
    check(!error.isEmpty(), "reject unregistered ID");
    rpc(client, "Launch", {"browser", QStringList{"/tmp/not-accepted"}}, &error);
    check(!error.isEmpty(), "reject unsupported arguments");

    rpc(client, "Launch", {"files", QStringList{}});
    check(waitState(client, "files", "Running"), "launch Files through visual handoff");
    const qint64 files = info(client, "files").value("pid").toUInt(); delay(1500);
    check(processField(files, "PPid").toLongLong() == daemon, "Files is an appd child, independent of Ocean");
    rpc(client, "Launch", {"files", QStringList{}}); delay(700);
    check(info(client, "files").value("pid").toUInt() == files && activations > 0, "single-instance reuse and activation signal");
    rpc(client, "Launch", {"browser", QStringList{}});
    check(waitState(client, "browser", "Running"), "launch Browser"); delay(1700);
    const qint64 browser = info(client, "browser").value("pid").toUInt();
    bool accepted = false;
    const int beforeHandoff = activations;
    OpenWaveApplications::launch("files", {"/Data/Users/root/Downloads"}, &app, [&](const QString &failure) {
        accepted = failure.isEmpty();
    });
    for (int i = 0; i < 150 && activations == beforeHandoff; ++i) delay(100);
    delay(800);
    check(accepted && activations > beforeHandoff && info(client, "files").value("pid").toUInt() == files && alive(browser), "Browser-to-Files SDK handoff preserves both processes");
    rpc(client, "Terminate", {"files"});
    check(waitState(client, "files", "Stopped"), "Files graceful shutdown");
    check(alive(browser), "terminating Files leaves Browser alive");
    rpc(client, "Terminate", {"browser"});
    check(waitState(client, "browser", "Stopped"), "Browser graceful shutdown");

    fixture("--stubborn"); delay(400);
    check(info(client, "fixture").value("name") == "Discovered Fixture" && registryEvents > 0, "discover a new desktop entry without shell rebuild");
    rpc(client, "Launch", {"fixture", QStringList{}});
    check(waitState(client, "fixture", "Running"), "launch newly discovered application"); delay(500);
    const qint64 fixturePid = info(client, "fixture").value("pid").toUInt();
    const qint64 helperPid = pidfile("/tmp/appd-fixture-child");
    QFile envFile("/tmp/appd-fixture-environment"); check(envFile.open(QIODevice::ReadOnly), "read helper environment");
    const auto env = QJsonDocument::fromJson(envFile.readAll()).object();
    check(env.value("platform") == "wayland" && env.value("eglfs").toString().isEmpty()
        && env.value("preload").toString().isEmpty() && env.value("noNewPrivileges").toInt() == 1
        && env.value("processGroup").toInt() == fixturePid, "controlled Wayland environment and dedicated process group");
    check(helperPid > 1 && processField(helperPid, "PPid").toLongLong() == fixturePid, "fixture creates helper outside its process group");
    QElapsedTimer deadline; deadline.start(); rpc(client, "Terminate", {"fixture"});
    check(waitState(client, "fixture", "Stopped", 5000) && deadline.elapsed() >= 2300, "force unresponsive process after configured deadline");
    for (int i = 0; i < 20 && (alive(fixturePid) || alive(helperPid)); ++i) delay(100);
    if (alive(fixturePid) || alive(helperPid)) QTextStream(stdout) << "APPD_QA helper remaining pid=" << helperPid
        << " state=" << processField(helperPid, "State") << " parent=" << processField(helperPid, "PPid") << Qt::endl;
    check(!alive(fixturePid) && !alive(helperPid), "cgroup removes and subreaper reaps escaped helper");

    fixture(""); delay(400);
    rpc(client, "Launch", {"fixture", QStringList{}});
    check(waitState(client, "fixture", "Running"), "relaunch fixture for crash test"); delay(300);
    const qint64 crashPid = info(client, "fixture").value("pid").toUInt();
    if (crashPid > 1) kill(crashPid, SIGSEGV);
    check(waitState(client, "fixture", "Crashed") && crashes > 0, "unexpected crash state and signal");
    delay(200);
    rpc(client, "Launch", {"settings", QStringList{}});
    check(waitState(client, "settings", "Running"), "launch Settings"); delay(1000);
    const qint64 settings = info(client, "settings").value("pid").toUInt();
    const qint64 ocean = pidfile("/run/openwave/ocean.pid");
    kill(ocean, SIGTERM);
    for (int i = 0; i < 100 && (pidfile("/run/openwave/ocean.pid") == ocean || alive(settings)); ++i) delay(100);
    check(!alive(settings) && pidfile("/run/openwave/ocean.pid") != ocean && pidfile("/run/openwave/appd.pid") == daemon,
        "Ocean restart reconciles applications without orphaning or duplicate ownership");
    delay(1500); rpc(client, "Launch", {"files", QStringList{}});
    check(waitState(client, "files", "Running") && info(client, "files").value("pid").toUInt() != files,
        "restart launches a fresh application in the new compositor session"); delay(1200);
    rpc(client, "Terminate", {"files"}); check(waitState(client, "files", "Stopped"), "post-restart termination");

    fixture("--stubborn"); delay(400);
    rpc(client, "Launch", {"fixture", QStringList{}}); check(waitState(client, "fixture", "Running"), "fixture before appd crash"); delay(300);
    const qint64 orphan = pidfile("/tmp/appd-fixture-child");
    kill(daemon, SIGKILL);
    for (int i = 0; i < 100 && (pidfile("/run/openwave/appd.pid") == daemon || alive(orphan)); ++i) delay(100);
    check(pidfile("/run/openwave/appd.pid") != daemon && !alive(orphan), "appd restart cleans recorded cgroups after daemon crash");
    delay(1500); rpc(client, "Launch", {"files", QStringList{}});
    check(waitState(client, "files", "Running"), "SDK and Ocean reconnect after appd restart");
    rpc(client, "Terminate", {"files"}); check(waitState(client, "files", "Stopped"), "final cleanup");
    check(starts >= 8 && stops >= 6, "lifecycle notifications delivered");
    QFile::remove("/usr/share/applications/appd-fixture.desktop");
    delay(500);
    for (const QString &test : {QStringLiteral("application-channel-tests"), QStringLiteral("application-layout-tests")}) {
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.start("/usr/libexec/" + test);
        check(process.waitForFinished(30000) && process.exitCode() == 0, test);
    }
    QTextStream(stdout) << "APPD_QA_RESULT failures=" << failures << Qt::endl;
    return failures ? 1 : 0;
}
