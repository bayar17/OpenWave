#include <sys/stat.h>
#include <sys/statvfs.h>
#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/ApplicationChannel.h>
#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
    const QString bundle = "/Applications/org.openwave.qa.fixture";
    QDir().mkpath(bundle + "/bin");
    QFile::remove(bundle + "/bin/fixture");
    check(QFile::copy("/usr/libexec/appd-qa", bundle + "/bin/fixture"), "install bundle fixture executable");
    QFile::setPermissions(bundle + "/bin/fixture", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    QJsonArray args{"--helper"};
    if (!arguments.isEmpty()) args.append(QString::fromUtf8(arguments));
    QJsonObject manifest{{"schemaVersion", 1}, {"id", "org.openwave.qa.fixture"}, {"name", "Discovered Fixture"},
        {"version", "1"}, {"type", "system"}, {"executable", "bin/fixture"}, {"runtime", "openwave-qt"},
        {"sandbox", QJsonObject{{"enabled", true}, {"profile", "standard"}, {"requires", QJsonArray{"selinux"}}}}, {"permissions", QJsonArray{}}, {"arguments", args}};
    check(write(bundle + "/manifest.json", QJsonDocument(manifest).toJson()), "write discovery bundle");

}
int helper(QCoreApplication &app)
{
    const bool stubborn = app.arguments().contains("--stubborn");
    if (stubborn) {
        signal(SIGTERM, SIG_IGN);
        pid_t child = fork();
        if (!child) { setsid(); signal(SIGTERM, SIG_IGN); for (;;) pause(); }
        write("/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/qa-child", QByteArray::number(child));
    }
    QJsonObject env{{"platform", qEnvironmentVariable("QT_QPA_PLATFORM")},
        {"eglfs", qEnvironmentVariable("QT_QPA_EGLFS_KMS_CONFIG")}, {"preload", qEnvironmentVariable("LD_PRELOAD")},
        {"noNewPrivileges", prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0)}, {"seccomp", prctl(PR_GET_SECCOMP)}, {"processGroup", int(getpgrp())},
        {"pid", int(getpid())}, {"uid", int(geteuid())},
        {"pictures", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)},
        {"picturesTarget", QFileInfo(QDir::homePath() + "/Pictures").symLinkTarget()},
        {"dataHome", qEnvironmentVariable("XDG_DATA_HOME")}};
    QFile direct("/Data/Users/root/Pictures/appd-private-photo");
    QFile linked(QDir::homePath() + "/Pictures/appd-private-photo");
    env.insert("directUserRead", direct.open(QIODevice::ReadOnly));
    env.insert("linkedUserRead", linked.open(QIODevice::ReadOnly));
    struct statvfs mounted;
    env.insert("runtimeReadOnly", !statvfs("/usr", &mounted) && bool(mounted.f_flag & ST_RDONLY));
    env.insert("otherAppVisible", QFileInfo::exists("/Data/Users/root/.DataLocation/Applications/com.openwave.ocean"));
    env.insert("serviceStateVisible", QFileInfo::exists("/var/lib/openwave/app-identities/users.ini"));
    write("/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/qa-environment", QJsonDocument(env).toJson(QJsonDocument::Compact));
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
    QProcess folders;
    folders.start("/usr/libexec/appd-folder-tests", {});
    check(folders.waitForFinished(10000) && folders.exitCode() == 0,
        "user-folder provisioning preserves data and rejects symlink traversal");
    QTextStream(stdout) << folders.readAllStandardOutput() << folders.readAllStandardError();
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
    check(info(client, "com.openwave.settings").value("icon").toString().endsWith("resources/icon.png"),
        "registry resolves bitmap icons larger than metadata limits");
    const QString settingsManifestPath = "/System/Applications/com.openwave.settings/manifest.json";
    QFile settingsManifestFile(settingsManifestPath); check(settingsManifestFile.open(QIODevice::ReadOnly), "read Settings policy fixture");
    const auto settingsManifestBytes = settingsManifestFile.readAll(); settingsManifestFile.close();
    auto settingsManifest = QJsonDocument::fromJson(settingsManifestBytes).object();
    auto required = settingsManifest.value("sandbox").toObject(); required["requires"] = QJsonArray{"landlock"}; settingsManifest["sandbox"] = required;
    check(write(settingsManifestPath, QJsonDocument(settingsManifest).toJson()), "install explicit trusted-app isolation requirement");
    delay(2400); rpc(client, "GetApplicationInfo", {"com.openwave.settings"}, &error);
    check(!error.isEmpty(), "trusted policy cannot silently bypass explicit isolation requirements");
    check(write(settingsManifestPath, settingsManifestBytes), "restore Settings manifest"); delay(2400);
    rpc(client, "RegisterShell", {}, &error);
    check(!error.isEmpty(), "reject forged shell registration");
    rpc(client, "Launch", {"../not-an-app", QStringList{}}, &error);
    check(!error.isEmpty(), "reject unregistered ID");
    rpc(client, "Launch", {"com.openwave.ocean", QStringList{"/tmp/not-accepted"}}, &error);
    check(!error.isEmpty(), "reject unsupported arguments");

    rpc(client, "Launch", {"com.openwave.files", QStringList{}});
    check(waitState(client, "com.openwave.files", "Running"), "launch Files through visual handoff");
    const qint64 files = info(client, "com.openwave.files").value("pid").toUInt(); delay(1500);
    check(processField(files, "PPid").toLongLong() == daemon, "Files is an appd child, independent of Ocean");
    rpc(client, "Launch", {"com.openwave.files", QStringList{}});
    for (int i = 0; i < 50 && activations == 0; ++i) delay(100);
    check(info(client, "com.openwave.files").value("pid").toUInt() == files && activations > 0, "single-instance reuse and activation signal");
    const QString legacy = "/Data/Users/root/.DataLocation/Applications/browser";
    const QString migrated = "/Data/Users/root/.DataLocation/Applications/com.openwave.ocean";
    QDir().mkpath(legacy + "/Browser"); QDir().mkpath(legacy + "/cache/images");
    check(write(legacy + "/bookmarks.json", "[{\"title\":\"Migration preserved\",\"url\":\"https://example.com/\"}]"), "seed legacy bookmarks");
    check(write(legacy + "/Browser/Settings.json", "{\"askOnDownload\":false}"), "seed legacy browser configuration");
    check(write(legacy + "/cache/marker", "cache-preserved"), "seed legacy cache");
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", "legacy-history");
        database.setDatabaseName(legacy + "/Browser/History.sqlite");
        check(database.open(), "create legacy history database");
        QSqlQuery query(database);
        check(query.exec("CREATE TABLE history (id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT NOT NULL, title TEXT, favicon TEXT, visited_at INTEGER NOT NULL)")
            && query.exec("INSERT INTO history(url,title,visited_at) VALUES('https://example.com/','Migration preserved',1792000000000)"), "seed legacy history visit");
        database.close();
    }
    QSqlDatabase::removeDatabase("legacy-history");
    QDir().mkpath("/var/lib/openwave/app-identities"); chmod("/var/lib/openwave/app-identities", 0700);
    check(write("/var/lib/openwave/app-identities/users.ini", "[Applications]\n0:browser=200081\n"), "seed legacy application UID mapping");
    chmod("/var/lib/openwave/app-identities/users.ini", 0600);
    rpc(client, "Launch", {"com.openwave.ocean", QStringList{}});
    check(waitState(client, "com.openwave.ocean", "Running"), "launch Browser"); delay(1700);
    const qint64 browser = info(client, "com.openwave.ocean").value("pid").toUInt();
    check(processField(browser, "Uid").section('\t', 0, 0).trimmed() == "200081", "application ID migration preserves isolated UID");
    check(info(client, "com.openwave.ocean").value("namespaceSandboxed").toBool(), "Browser uses Bubblewrap namespace isolation");
    check(!QFile::exists(legacy) && QFile::exists(migrated + "/Data/bookmarks.json")
        && QFile::exists(migrated + "/Config/Settings.json") && QFile::exists(migrated + "/Cache/marker"), "migrate private data into Config Cache Data without legacy links");
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", "migrated-history");
        database.setDatabaseName(migrated + "/Data/Browser/History.sqlite");
        check(database.open(), "open migrated history database"); QSqlQuery query(database);
        check(query.exec("SELECT title FROM history WHERE url='https://example.com/'") && query.next()
            && query.value(0).toString() == "Migration preserved", "history visit survives bundle migration"); database.close();
    }
    QSqlDatabase::removeDatabase("migrated-history");
    bool accepted = false;
    const int beforeHandoff = activations;
    OpenWaveApplications::launch("com.openwave.files", {"/Data/Users/root/Downloads"}, &app, [&](const QString &failure) {
        accepted = failure.isEmpty();
    });
    for (int i = 0; i < 150 && activations == beforeHandoff; ++i) delay(100);
    delay(800);
    check(accepted && activations > beforeHandoff && info(client, "com.openwave.files").value("pid").toUInt() == files && alive(browser), "Browser-to-Files SDK handoff preserves both processes");
    rpc(client, "Terminate", {"com.openwave.files"});
    check(waitState(client, "com.openwave.files", "Stopped"), "Files graceful shutdown");
    check(alive(browser), "terminating Files leaves Browser alive");
    rpc(client, "Terminate", {"com.openwave.ocean"});
    check(waitState(client, "com.openwave.ocean", "Stopped"), "Browser graceful shutdown");
    rpc(client, "Launch", {"com.openwave.ocean", QStringList{}});
    check(waitState(client, "com.openwave.ocean", "Running"), "Browser relaunch preserves managed Downloads link");
    rpc(client, "Terminate", {"com.openwave.ocean"});
    check(waitState(client, "com.openwave.ocean", "Stopped"), "relaunched Browser shuts down normally");

    QDir().mkpath("/Data/Users/root/Pictures");
    check(write("/Data/Users/root/Pictures/appd-private-photo", "private-user-photo"), "seed user-folder isolation probe");
    fixture("--stubborn"); delay(2400);
    const bool renamedBwrap = QFile::rename("/usr/bin/bwrap", "/usr/bin/bwrap.qa-disabled");
    check(renamedBwrap, "temporarily remove Bubblewrap for fail-closed test");
    if (renamedBwrap) {
        rpc(client, "Launch", {"org.openwave.qa.fixture", QStringList{}});
        QVariantMap refused;
        for (int i = 0; i < 70; ++i) {
            refused = info(client, "org.openwave.qa.fixture");
            if (!refused.value("error").toString().isEmpty()) break;
            delay(100);
        }
        check(refused.value("state") == "Stopped" && !refused.value("pid").toUInt()
            && refused.value("error").toString().contains("bwrap"), "missing Bubblewrap rejects launch without fallback");
        check(QFile::rename("/usr/bin/bwrap.qa-disabled", "/usr/bin/bwrap"), "restore Bubblewrap after fail-closed test");
    }
    check(info(client, "org.openwave.qa.fixture").value("name") == "Discovered Fixture" && registryEvents > 0, "discover a new bundle without shell rebuild");
    check(!info(client, "org.openwave.qa.fixture").value("trusted").toBool(), "third-party bundle does not gain system trust");
    const QString fixtureRoot = "/Applications/org.openwave.qa.fixture";
    QFile manifestFile(fixtureRoot + "/manifest.json"); check(manifestFile.open(QIODevice::ReadOnly), "read bundle manifest");
    const auto validManifest = QJsonDocument::fromJson(manifestFile.readAll()).object(); manifestFile.close();
    const auto rejected = [&](const QJsonObject &manifest, const QString &label) {
        check(write(fixtureRoot + "/manifest.json", QJsonDocument(manifest).toJson()), "install invalid manifest fixture");
        delay(2400); QString rejection;
        rpc(client, "GetApplicationInfo", {"org.openwave.qa.fixture"}, &rejection);
        check(!rejection.isEmpty(), label);
    };
    auto invalidManifest = validManifest; invalidManifest["executable"] = "../../usr/bin/false";
    rejected(invalidManifest, "reject executable path traversal");
    invalidManifest = validManifest; invalidManifest["sandbox"] = QJsonObject{{"enabled", true}, {"profile", "bubblewrap"}};
    rejected(invalidManifest, "reject unrecognized sandbox profiles");
    invalidManifest = validManifest; invalidManifest["id"] = "com.openwave.settings";
    rejected(invalidManifest, "reject mismatched or reserved application identity");
    invalidManifest = validManifest; invalidManifest["icon"] = "../../etc/passwd";
    rejected(invalidManifest, "reject resource path traversal");
    check(write(fixtureRoot + "/manifest.json", QJsonDocument(validManifest).toJson()), "restore bundle manifest");
    QFile::remove(fixtureRoot + "/bin/fixture");
    check(QFile::link("/usr/libexec/appd-qa", fixtureRoot + "/bin/fixture"), "install escaping executable symlink fixture");
    delay(2400); QString rejection; rpc(client, "GetApplicationInfo", {"org.openwave.qa.fixture"}, &rejection);
    check(!rejection.isEmpty(), "reject executable symlinks");
    fixture("--stubborn"); delay(2400);
    rpc(client, "Launch", {"org.openwave.qa.fixture", QStringList{}});
    check(waitState(client, "org.openwave.qa.fixture", "Running"), "launch newly discovered application"); delay(500);
    const qint64 fixturePid = info(client, "org.openwave.qa.fixture").value("pid").toUInt();
    const qint64 helperPid = pidfile("/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/qa-child");
    QFile envFile("/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/qa-environment"); check(envFile.open(QIODevice::ReadOnly), "read helper environment");
    const auto env = QJsonDocument::fromJson(envFile.readAll()).object();
    check(env.value("platform") == "wayland" && env.value("eglfs").toString().isEmpty()
        && env.value("preload").toString().isEmpty() && env.value("noNewPrivileges").toInt() == 1
        && env.value("processGroup").toInt() == fixturePid, "controlled Wayland environment and dedicated process group");
    check(env.value("uid").toInt() >= 200000, "regular application uses an isolated UID");
    check(env.value("seccomp").toInt() == 2, "application inherits a seccomp filter");
    QFile taskGroup(QString("/proc/%1/cgroup").arg(fixturePid));
    const bool groupOpened = taskGroup.open(QIODevice::ReadOnly);
    const auto groupPath = groupOpened ? QString::fromUtf8(taskGroup.readAll()).trimmed().section(':', 2) : QString();
    QFile taskLimit("/sys/fs/cgroup" + groupPath + "/pids.max");
    check(!groupPath.isEmpty() && taskLimit.open(QIODevice::ReadOnly) && taskLimit.readAll().trimmed() == "512",
        "application cgroup enforces the configured task limit");
    check(info(client, "org.openwave.qa.fixture").value("namespaceSandboxed").toBool(), "appd reports the active namespace sandbox");
    for (const QString &name : {QStringLiteral("mnt"), QStringLiteral("ipc"), QStringLiteral("uts"), QStringLiteral("pid"), QStringLiteral("net")}) {
        struct stat st, child; const bool same = name == "pid" || name == "net";
        check(!stat(QFile::encodeName("/proc/self/ns/" + name).constData(), &st)
            && !stat(QFile::encodeName(QString("/proc/%1/ns/%2").arg(fixturePid).arg(name)).constData(), &child)
            && (child.st_ino == st.st_ino) == same,
            name + (same ? ": preserve host identity/network" : ": isolate namespace"));
    }
    check(env.value("runtimeReadOnly").toBool(), "application runtime is mounted read-only");
    check(!env.value("otherAppVisible").toBool() && !env.value("serviceStateVisible").toBool(), "other app profiles and private service state are absent from the namespace");
    const QString privateHome = "/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture";
    check(env.value("pictures").toString() == privateHome + "/Pictures"
        && env.value("picturesTarget").toString() == "/Data/Users/root/Pictures", "Qt user folders resolve through per-app links");
    check(env.value("dataHome").toString() == privateHome + "/Data", "Qt data storage uses persistent application Data directory");
    check(!env.value("directUserRead").toBool() && !env.value("linkedUserRead").toBool(), "user-folder links cannot bypass file permissions");
    check(helperPid > 1 && processField(helperPid, "PPid").toLongLong() == fixturePid, "fixture creates helper outside its process group");
    const QString untouched = "/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/Data/unchanged";
    check(write(untouched, "preserve-ownership"), "seed existing private storage");
    chown(QFile::encodeName(untouched).constData(), env.value("uid").toInt(), env.value("uid").toInt());
    chmod(QFile::encodeName(untouched).constData(), 0600);
    struct stat untouchedBefore; check(!stat(QFile::encodeName(untouched).constData(), &untouchedBefore), "stat private storage before relaunch");
    QElapsedTimer deadline; deadline.start(); rpc(client, "Terminate", {"org.openwave.qa.fixture"});
    check(waitState(client, "org.openwave.qa.fixture", "Stopped", 5000) && deadline.elapsed() >= 2300, "force unresponsive process after configured deadline");
    for (int i = 0; i < 20 && (alive(fixturePid) || alive(helperPid)); ++i) delay(100);
    if (alive(fixturePid) || alive(helperPid)) QTextStream(stdout) << "APPD_QA helper remaining pid=" << helperPid
        << " state=" << processField(helperPid, "State") << " parent=" << processField(helperPid, "PPid") << Qt::endl;
    check(!alive(fixturePid) && !alive(helperPid), "cgroup removes and subreaper reaps escaped helper");

    fixture(""); delay(2400);
    rpc(client, "Launch", {"org.openwave.qa.fixture", QStringList{}});
    check(waitState(client, "org.openwave.qa.fixture", "Running"), "relaunch fixture for crash test"); delay(300);
    struct stat untouchedAfter;
    check(!stat(QFile::encodeName(untouched).constData(), &untouchedAfter)
        && untouchedBefore.st_ctim.tv_sec == untouchedAfter.st_ctim.tv_sec
        && untouchedBefore.st_ctim.tv_nsec == untouchedAfter.st_ctim.tv_nsec,
        "normal relaunch does not traverse and change existing profile ownership");
    const qint64 crashPid = info(client, "org.openwave.qa.fixture").value("pid").toUInt();
    if (crashPid > 1) kill(crashPid, SIGSEGV);
    check(waitState(client, "org.openwave.qa.fixture", "Crashed") && crashes > 0, "unexpected crash state and signal");
    delay(200);
    rpc(client, "Launch", {"com.openwave.settings", QStringList{}});
    check(waitState(client, "com.openwave.settings", "Running"), "launch Settings"); delay(1000);
    const qint64 settings = info(client, "com.openwave.settings").value("pid").toUInt();
    const qint64 ocean = pidfile("/run/openwave/ocean.pid");
    kill(ocean, SIGTERM);
    for (int i = 0; i < 100 && (pidfile("/run/openwave/ocean.pid") == ocean || alive(settings)); ++i) delay(100);
    check(!alive(settings) && pidfile("/run/openwave/ocean.pid") != ocean && pidfile("/run/openwave/appd.pid") == daemon,
        "Ocean restart reconciles applications without orphaning or duplicate ownership");
    delay(1500); rpc(client, "Launch", {"com.openwave.files", QStringList{}});
    check(waitState(client, "com.openwave.files", "Running") && info(client, "com.openwave.files").value("pid").toUInt() != files,
        "restart launches a fresh application in the new compositor session"); delay(1200);
    rpc(client, "Terminate", {"com.openwave.files"}); check(waitState(client, "com.openwave.files", "Stopped"), "post-restart termination");

    fixture("--stubborn"); delay(2400);
    rpc(client, "Launch", {"org.openwave.qa.fixture", QStringList{}}); check(waitState(client, "org.openwave.qa.fixture", "Running"), "fixture before appd crash"); delay(300);
    const qint64 orphan = pidfile("/Data/Users/root/.DataLocation/Applications/org.openwave.qa.fixture/qa-child");
    kill(daemon, SIGKILL);
    for (int i = 0; i < 100 && (pidfile("/run/openwave/appd.pid") == daemon || alive(orphan)); ++i) delay(100);
    check(pidfile("/run/openwave/appd.pid") != daemon && !alive(orphan), "appd restart cleans recorded cgroups after daemon crash");
    delay(1500); rpc(client, "Launch", {"com.openwave.files", QStringList{}});
    check(waitState(client, "com.openwave.files", "Running"), "SDK and Ocean reconnect after appd restart");
    rpc(client, "Terminate", {"com.openwave.files"}); check(waitState(client, "com.openwave.files", "Stopped"), "final cleanup");
    check(starts >= 8 && stops >= 6, "lifecycle notifications delivered");
    QDir("/Applications/org.openwave.qa.fixture").removeRecursively();
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
