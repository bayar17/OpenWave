#include <OpenWave/ApplicationServices.h>
#include <OpenWave/PermissionClient.h>
#include <OpenWave/ApplicationsClient.h>
#include <OpenWave/ApplicationChannel.h>
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/OpenWavePromptClient.h>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <cerrno>
#undef signals
#include <gio/gio.h>
#include <OpenWave/OpenWaveIdentity.h>
extern "C" unsigned int permission_audio_probe(void);

namespace {
int failures = 0;
QString storage(const QString &app) { return "/Data/Users/root/.DataLocation/Applications/" + app; }
void check(bool okay, const QString &label) {
    QTextStream(stdout) << "PERMISSION_QA " << (okay ? "PASS " : "FAIL ") << label << Qt::endl;
    if (!okay) ++failures;
}
void delay(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
bool write(const QString &name, const QByteArray &value) {
    QSaveFile file(name); if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner); return file.write(value) == value.size() && file.commit();
}
QByteArray read(const QString &name) { QFile file(name); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QVariantList apps(ApplicationsClient &client, const QString &method, const QVariantList &args = {}, QString *failure = nullptr) {
    QEventLoop loop; bool done = false; QVariantList result; QString error;
    client.call(method, args, [&](const auto &values, const auto &message) { result = values; error = message; done = true; loop.quit(); });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit); loop.exec();
    if (!done) { check(false, "appd timed out: " + method); _exit(95); }
    if (failure) *failure = error; else check(error.isEmpty(), method + " " + error);
    return result;
}
QVariantList permissions(OpenWave::PermissionClient &client, const QString &method, const QStringList &args, QString *failure = nullptr) {
    QEventLoop loop; bool done = false; QVariantList result; QString error;
    client.call(method, args, [&](const auto &values, const auto &message) { result = values; error = message; done = true; loop.quit(); });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit); loop.exec();
    if (!done) { check(false, "permissiond timed out: " + method); _exit(96); }
    if (failure) *failure = error; else check(error.isEmpty(), method + " " + error);
    return result;
}
GVariant *raw(GDBusConnection *bus, const char *service, const char *path, const char *interface, const char *method, GVariant *args) {
    return g_dbus_connection_call_sync(bus, service, path, interface, method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
}
qint64 pid(const QString &name) { return read(name).trimmed().toLongLong(); }
bool alive(qint64 process) { return process > 1 && QFile::exists(QString("/proc/%1/stat").arg(process)); }
QString uidOf(qint64 process) {
    for (const auto &line : read(QString("/proc/%1/status").arg(process)).split('\n')) if (line.startsWith("Uid:")) return QString::fromUtf8(line.mid(4)).simplified().section(' ', 0, 0);
    return {};
}
int fixture(QGuiApplication &app, const QString &appId)
{
    const QString directory = storage(appId);
    OpenWave::PermissionClient client, second;
    QStringList notifications;
    QObject::connect(&client, &OpenWave::PermissionClient::permissionChanged, &app,
        [&](const QString &, const QString &permission, const QString &state) { notifications.append(permission + ':' + state); });
    client.subscribePermissionChanges();
    OpenWavePortals portal;
    QString phase, token;
    int retained = -1;
    auto report = [&](const QJsonObject &result) {
        auto row = result; row.insert("phase", phase); row.insert("pid", int(getpid()));
        write(directory + "/qa-response.json", QJsonDocument(row).toJson(QJsonDocument::Compact));
    };
    bool processing = false;
    int pendingState = -1;
    bool pendingFinished = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (processing) return;
        const auto command = QJsonDocument::fromJson(read(directory + "/qa-command.json")).object();
        const QString next = command.value("phase").toString();
        if (next.isEmpty() || next == phase) return;
        phase = next; processing = true;
        const auto operation = command.value("operation").toString();
        if (operation == "identity") {
            qputenv("OPENWAVE_APP_ID", "com.openwave.settings");
            ApplicationsClient applications; QString error;
            auto fields = apps(applications, "GetOwnIdentity", {}, &error).value(0).toMap();
            auto *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
            auto *reply = raw(bus, "org.openwave.Applications", "/org/openwave/Applications", "org.openwave.Applications1", "ResolveCaller", g_variant_new("(s)", g_dbus_connection_get_unique_name(bus)));
            const bool noResolve = !reply; if (reply) g_variant_unref(reply);
            reply = raw(bus, "org.openwave.Permissions", "/org/openwave/Permissions", "org.openwave.Permissions1", "ValidateLease", g_variant_new("(ss)", "forged", g_dbus_connection_get_unique_name(bus)));
            const bool noBroker = !reply; if (reply) g_variant_unref(reply);
            auto administrative = permissions(client, "ListApplicationsPermissions", {}, &error); const bool noAdmin = !error.isEmpty();
            permissions(client, "RevokePermission", {"com.openwave.ocean", "files.read"}, &error); const bool noRevoke = !error.isEmpty();
            int direct = open("/Data/Users/root/Documents/permission-test.txt", O_RDONLY);
            const bool noDirect = direct < 0; if (direct >= 0) close(direct);
            int privateFile = open("/var/lib/openwave/permissions/0/policies.json", O_RDONLY);
            const bool noDatabase = privateFile < 0; if (privateFile >= 0) close(privateFile);
            auto *session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
            reply = session ? raw(session, "org.openwave.Prompt", "/org/openwave/Prompt", "org.openwave.Prompt1", "Respond", g_variant_new("(sss)", "forged", "accepted", "allow_permanent")) : nullptr;
            const bool noForgedPrompt = !reply; if (reply) g_variant_unref(reply); if (session) g_object_unref(session);
            reply = raw(bus, "org.openwave.Applications", "/org/openwave/Applications", "org.openwave.ApplicationServices1", "Subscribe",
                g_variant_new("(s)", "9d1240be-1175-4b22-bf0e-b62822832fad"));
            const bool noPrivateLifecycle = !reply; if (reply) g_variant_unref(reply);
            errno = 0; const long cloneResult = syscall(SYS_clone3, nullptr, 0);
            const bool clone3Filtered = cloneResult == -1 && errno == ENOSYS;
            g_object_unref(bus);
            report({{"uid", int(geteuid())}, {"identity", fields.value("appId").toString()}, {"digest", fields.value("manifestDigest").toString()}, {"noResolve", noResolve}, {"noBroker", noBroker},
                {"seccomp", prctl(PR_GET_SECCOMP)}, {"clone3Filtered", clone3Filtered}, {"noPrivateLifecycle", noPrivateLifecycle}, {"noAdmin", noAdmin}, {"noRevoke", noRevoke}, {"noDirect", noDirect}, {"noDatabase", noDatabase}, {"noForgedPrompt", noForgedPrompt}});
        } else if (operation == "begin") {
            pendingState = -1; pendingFinished = false;
            QFile::remove(directory + "/qa-pending-result");
            client.requestPermissionAsync("files.read", [&, directory](auto state, const QString &error) {
                pendingState = int(state); pendingFinished = true;
                const bool saved = write(directory + "/qa-pending-result", QJsonDocument(QJsonObject{{"state", int(state)}, {"error", error}}).toJson());
                QTextStream(stdout) << "PERMISSION_QA_ASYNC state=" << int(state) << " saved=" << saved << Qt::endl;
            });
            report({{"started", true}});
        } else if (operation == "pending") {
            report({{"finished", pendingFinished}, {"state", pendingState}});
        } else if (operation == "async-check") {
            QEventLoop loop; bool done = false; OpenWave::PermissionClient::Status state = OpenWave::PermissionClient::Denied;
            client.checkPermissionAsync("files.read", [&](auto result, const QString &) { state = result; done = true; loop.quit(); });
            QTimer::singleShot(5000, &loop, &QEventLoop::quit); loop.exec();
            report({{"done", done}, {"granted", state == OpenWave::PermissionClient::Granted}});
        } else if (operation == "audio") {
            report({{"audioFlags", int(permission_audio_probe())}});
        } else if (operation == "notifications") {
            delay(150); report({{"changes", QJsonArray::fromStringList(notifications)}});
        } else if (operation == "check" || operation == "request") {
            QString error; auto values = permissions(client, operation == "request" ? "RequestPermission" : "GetPermissionStatus", {command.value("permission").toString()}, &error);
            report({{"state", values.value(0).toString()}, {"error", error}});
        } else if (operation == "forge") {
            QFile::remove(directory + "/qa-visible-prompt");
            client.call("RequestPermission", {"files.read"}, [](const QVariantList &, const QString &) {});
            QElapsedTimer deadline; deadline.start();
            while (read(directory + "/qa-visible-prompt").isEmpty() && deadline.elapsed() < 5000) delay(100);
            auto *session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
            const auto promptId = read(directory + "/qa-visible-prompt").trimmed(); GError *error = nullptr;
            auto *reply = session ? g_dbus_connection_call_sync(session, "org.openwave.Prompt", "/org/openwave/Prompt", "org.openwave.Prompt1", "Respond",
                g_variant_new("(sss)", promptId.constData(), "accepted", "allow_permanent"), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error) : nullptr;
            const bool denied = !reply && error && QString::fromUtf8(error->message).contains("Unauthorized");
            if (reply) g_variant_unref(reply); g_clear_error(&error); if (session) g_object_unref(session);
            report({{"forgedPromptRejected", denied}, {"hasRealId", !promptId.isEmpty()}});
        } else if (operation == "concurrent") {
            int finished = 0; QStringList states;
            QEventLoop loop;
            auto completion = [&](const QVariantList &values, const QString &error) { states.append(error.isEmpty() ? values.value(0).toString() : "error"); if (++finished == 2) loop.quit(); };
            client.call("RequestPermission", {"files.read"}, completion); second.call("RequestPermission", {"files.read"}, completion);
            QTimer::singleShot(10000, &loop, &QEventLoop::quit); loop.exec();
            report({{"finished", finished}, {"states", QJsonArray::fromStringList(states)}});
        } else if (operation == "open" || operation == "directory" || operation == "create" || operation == "save") {
            QEventLoop loop; QString error; bool done = false; QByteArray contents;
            const auto completed = QObject::connect(&portal, &OpenWavePortals::completed, &loop, [&](const QVariantList &grants) {
                auto *grant = grants.isEmpty() ? nullptr : qobject_cast<PortalGrant *>(grants.first().value<QObject *>());
                if (grant) {
                    if (grant->directory()) token = grant->directoryToken();
                    else if (grant->writable()) { if (!grant->writeAll("brokered-write")) error = "write failed"; }
                    else { contents = grant->readAll(); if (retained >= 0) close(retained); retained = fcntl(grant->descriptor(), F_DUPFD_CLOEXEC, 3); }
                } else error = "no grant";
                done = true; loop.quit();
            });
            const auto failure = QObject::connect(&portal, &OpenWavePortals::errorOccurred, &loop, [&](const QString &message) { error = message; done = true; loop.quit(); });
            if (operation == "directory") portal.selectDirectory();
            else if (operation == "create") portal.createFile(token, "brokered-created.txt");
            else if (operation == "save") portal.saveFile({{"suggestedFilename", "portal-saved.txt"}});
            else portal.openFile();
            QTimer::singleShot(12000, &loop, &QEventLoop::quit); loop.exec();
            QObject::disconnect(completed); QObject::disconnect(failure);
            int direct = open("/Data/Users/root/Documents/permission-test.txt", O_RDONLY);
            const bool directDenied = direct < 0; if (direct >= 0) close(direct);
            report({{"done", done}, {"error", error}, {"contents", QString::fromUtf8(contents)}, {"token", token}, {"directDenied", directDenied}});
        } else if (operation == "child") {
            const pid_t child = fork();
            if (!child) { setsid(); for (;;) pause(); }
            report({{"child", int(child)}, {"hasDescriptor", retained >= 0}});
        } else if (operation == "quit") {
            report({{"quitting", true}}); QTimer::singleShot(50, &app, &QCoreApplication::quit);
        }
        processing = false;
    });
    timer.start(100);
    ApplicationChannel channel(OpenWaveApplications::instanceAddress(getpid()), [&](const QJsonObject &message) {
        if (message.value("command") == "quit") QTimer::singleShot(50, &app, &QCoreApplication::quit);
        return QString();
    });
    report({{"ready", true}});
    return app.exec();
}
QJsonObject command(const QString &app, const QString &operation, const QString &permission = {}) {
    static int serial = 0; const QString phase = QString::number(++serial);
    check(write(storage(app) + "/qa-command.json", QJsonDocument(QJsonObject{{"phase", phase}, {"operation", operation}, {"permission", permission}}).toJson()), "send " + operation);
    // The root coordinator preserves the application's DAC ownership when publishing commands.
    struct stat st;
    if (!stat(QFile::encodeName(storage(app)).constData(), &st)) chown(QFile::encodeName(storage(app) + "/qa-command.json").constData(), st.st_uid, st.st_gid);
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < 16000) {
        auto row = QJsonDocument::fromJson(read(storage(app) + "/qa-response.json")).object();
        if (row.value("phase").toString() == phase) { QTextStream(stdout) << "PERMISSION_QA_RESPONSE " << operation << " " << QJsonDocument(row).toJson(QJsonDocument::Compact) << Qt::endl; return row; }
        delay(100);
    }
    check(false, "fixture response timeout " + operation); return {};
}
int coordinator(QGuiApplication &app) {
    Q_UNUSED(app);
    delay(1200);
    ApplicationsClient applications; OpenWave::PermissionClient client;
    const QString a = "org.openwave.qa.permission-a", b = "org.openwave.qa.permission-b";
    auto launch = [&](const QString &id) {
        if (id.startsWith("org.openwave.qa.permission-")) { QFile::remove(storage(id) + "/qa-command.json"); QFile::remove(storage(id) + "/qa-response.json"); QFile::remove(storage(id) + "/qa-visible-prompt"); }
        apps(applications, "Launch", {id, QStringList{}});
        delay(1600);
        auto info = apps(applications, "GetApplicationInfo", {id}).value(0).toMap();
        const auto process = info.value("pid").toUInt(); check(process && alive(process), id + " launched"); return process;
    };
    auto first = launch(a); const auto other = launch(b);
    check(!uidOf(first).isEmpty() && uidOf(first) != "0" && uidOf(first) != uidOf(other), "regular applications have distinct unprivileged UIDs");
    const auto identity = command(a, "identity");
    check(identity.value("identity").toString() == a, "self-declared Settings identity is ignored");
    check(identity.value("digest").toString() == QString::fromLatin1(QCryptographicHash::hash(read("/Applications/" + a + "/manifest.json"), QCryptographicHash::Sha256).toHex()),
        "caller identity is bound to verified installation metadata");
    for (const auto &key : {"noResolve", "noBroker", "noAdmin", "noRevoke", "noDirect", "noDatabase", "noForgedPrompt", "noPrivateLifecycle"}) check(identity.value(key).toBool(), key);
    check(identity.value("seccomp").toInt() == 2 && identity.value("clone3Filtered").toBool(), "real seccomp filter is installed before application code");
    const auto declaredRows = permissions(client, "ListApplicationsPermissions", {}).value(0).toList();
    bool cameraDeclared = false;
    for (const auto &row : declaredRows) {
        const auto value = row.toMap();
        if (value.value("application") == a && value.value("permission") == "camera")
            cameraDeclared = value.value("declared").toBool() && !value.value("supported").toBool();
    }
    check(cameraDeclared, "verified installation declarations include unavailable capabilities");
    const auto audio = command(a, "audio").value("audioFlags").toInt();
    check((audio & 31) == 31 && !(audio & 1792), "isolated playback works while audio capture, monitor ports, and unsafe factories remain inaccessible");
    check(command(a, "check", "camera").value("state").toString() == "restricted", "unsupported resources are restricted");
    check(command(a, "request", "camera").value("state").toString() == "restricted", "unsupported requested permission remains restricted");
    auto requestedAt = [&](const QString &app, const QString &permission) {
        const auto rows = permissions(client, "ListApplicationsPermissions", {}).value(0).toList();
        for (const auto &value : rows) { const auto row = value.toMap(); if (row.value("application") == app && row.value("permission") == permission) return row.value("requestedAt").toLongLong(); }
        return qint64(0);
    };
    check(requestedAt(a, "camera") > 0 && requestedAt(b, "camera") == 0 && requestedAt(a, "files.read") == 0,
        "Settings sees actual requests without inventing requests for other apps or permissions");
    write("/tmp/permission-qa-choice", "hold");
    const auto forged = command(a, "forge");
    check(forged.value("hasRealId").toBool() && forged.value("forgedPromptRejected").toBool(), "application cannot forge a response to its real pending permission prompt");
    write("/tmp/permission-qa-choice", "deny"); delay(1300);
    permissions(client, "SetPermission", {a, "files.read", "not-determined", "permanent"});
    first = launch(a);
    QFile::remove("/tmp/permission-qa-prompts");
    const auto denial = command(a, "concurrent");
    check(denial.value("finished").toInt() == 2 && denial.value("states").toArray() == QJsonArray{"denied", "denied"}, "concurrent denial requests complete safely");
    check(!read("/tmp/permission-qa-prompts").trimmed().isEmpty() && read("/tmp/permission-qa-prompts").trimmed().split('\n').size() == 1, "concurrent requests share one native prompt");
    auto failed = command(a, "open"); check(!failed.value("error").toString().isEmpty(), "denied broker request delivers no resource");
    permissions(client, "SetPermission", {a, "files.read", "not-determined", "permanent"});
    first = launch(a);
    write("/tmp/permission-qa-choice", "allow_once");
    auto opened = command(a, "open");
    check(opened.value("contents").toString() == "selected-file" && opened.value("directDenied").toBool(), "one-time grant opens only the selected descriptor");
    check(command(a, "check", "files.read").value("state").toString() == "not-determined", "one-time grant is consumed");
    check(command(a, "notifications").value("changes").toArray().contains("files.read:not-determined"), "one-time consumption notifies the authenticated subscriber");
    permissions(client, "SetPermission", {a, "files.read", "granted", "permanent"});
    permissions(client, "SetPermission", {a, "files.directory", "granted", "session"});
    permissions(client, "SetPermission", {a, "files.write", "granted", "permanent"});
    check(requestedAt(a, "files.write") == 0, "an administrative grant is not an application request");
    auto directory = command(a, "directory"); check(!directory.value("token").toString().isEmpty(), "directory selection returns a broker token");
    auto created = command(a, "create");
    check(created.value("error").toString().isEmpty() && read("/Data/Users/root/Documents/brokered-created.txt") == "brokered-write", "selected-folder writes use a broker descriptor");
    auto saved = command(a, "save");
    check(saved.value("error").toString().isEmpty() && read("/Data/Users/root/Documents/portal-saved.txt") == "brokered-write", "SaveFile end-to-end write");
    check(command(b, "check", "files.read").value("state").toString() == "not-determined", "another application cannot inherit the grant");
    const auto daemon = pid("/run/openwave/permissiond.pid"); kill(daemon, SIGKILL);
    for (int i = 0; i < 60 && pid("/run/openwave/permissiond.pid") == daemon; ++i) delay(100);
    delay(1200);
    check(pid("/run/openwave/permissiond.pid") != daemon, "PID 1 restarts permissiond");
    check(command(a, "check", "files.read").value("state").toString() == "granted", "permanent decisions survive a daemon restart");
    check(command(a, "check", "files.directory").value("state").toString() == "granted", "session decisions survive a daemon restart in the same session");
    check(requestedAt(a, "camera") > 0 && requestedAt(a, "files.read") > 0 && requestedAt(a, "files.write") > 0,
        "requested permission metadata survives restart and one-time consumption");
    opened = command(a, "open"); check(opened.value("contents").toString() == "selected-file", "broker reconnects after permissiond restart");
    const auto child = command(a, "child"); check(child.value("hasDescriptor").toBool(), "application retained a real granted descriptor");
    permissions(client, "RevokePermission", {a, "files.read"});
    delay(400);
    check(!alive(first) && !alive(child.value("child").toInt()), "revocation kills the application and escaped helpers holding descriptors");
    launch(a);
    check(command(a, "check", "files.read").value("state").toString() == "not-determined", "revocation persists after relaunch");
    permissions(client, "SetPermission", {a, "files.read", "granted", "permanent"});
    opened = command(a, "open"); check(opened.value("contents").toString() == "selected-file", "grant before crash recovery test");
    const auto recoveryChild = command(a, "child").value("child").toInt();
    const auto recoveryPid = apps(applications, "GetApplicationInfo", {a}).value(0).toMap().value("pid").toUInt();
    const QString policyPath = "/var/lib/openwave/permissions/0/policies.json";
    auto database = QJsonDocument::fromJson(read(policyPath)).object(); auto policies = database.value("policies").toArray();
    for (int i = 0; i < policies.size(); ++i) {
        auto row = policies.at(i).toObject();
        if (row.value("application") == a && row.value("permission") == "files.read") {
            row["state"] = "denied"; row["duration"] = "permanent"; row["revocationPending"] = true;
            policies[i] = row;
        }
    }
    database["policies"] = policies;
    check(write(policyPath, QJsonDocument(database).toJson()), "simulate a persisted revocation interrupted before broker closure");
    const auto oldDaemon = pid("/run/openwave/permissiond.pid"); kill(oldDaemon, SIGKILL);
    for (int i = 0; i < 60 && pid("/run/openwave/permissiond.pid") == oldDaemon; ++i) delay(100);
    delay(1200);
    check(!alive(recoveryPid) && !alive(recoveryChild), "daemon restart completes an interrupted revocation");
    first = launch(a); check(command(a, "check", "files.read").value("state").toString() == "denied", "recovered denial remains persistent");

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 58617), "start local browser download fixture");
    int networkRequests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                socket->setProperty("answered", true); ++networkRequests;
                const bool download = request.startsWith("GET /download ");
                const QByteArray body = download ? QByteArray("browser-broker-download")
                    : QByteArray("<!doctype html><title>Permission Browser Fixture</title><script>setTimeout(function(){location.href='/download'},300)</script>");
                QByteArray response = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: ";
                response += download ? "application/octet-stream\r\nContent-Disposition: attachment; filename=qa-download.txt\r\n" : "text/html\r\n";
                response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response); socket->disconnectFromHost();
            });
        }
    });
    QDir().mkpath(storage("com.openwave.ocean") + "/Config");
    QDir().mkpath(storage("com.openwave.ocean") + "/Data");
    write(storage("com.openwave.ocean") + "/Config/Settings.json", "{\"askOnDownload\":false}");
    write(storage("com.openwave.ocean") + "/Data/tabs.json", "{\"tabs\":[{\"id\":\"qa-page\",\"url\":\"http://127.0.0.1:58617/\"}],\"activeId\":\"qa-page\"}");
    permissions(client, "SetPermission", {"com.openwave.ocean", "files.write", "granted", "permanent"});
    const auto browser = launch("com.openwave.ocean"); delay(2200); check(alive(browser) && uidOf(browser) != "0", "Ocean Browser launches under isolation");
    for (int i = 0; i < 100 && read("/Data/Users/root/Downloads/qa-download.txt") != "browser-broker-download"; ++i) delay(100);
    check(networkRequests >= 2 && read("/Data/Users/root/Downloads/qa-download.txt") == "browser-broker-download", "real WebKit navigation and download use the file broker under isolation");
    apps(applications, "Launch", {"com.openwave.files", QStringList{}}); delay(1400);
    const auto files = apps(applications, "GetApplicationInfo", {"com.openwave.files"}).value(0).toMap().value("pid").toUInt();
    check(alive(files) && uidOf(files) == "0", "Files remains a trusted system component");
    permissions(client, "SetPermission", {a, "files.read", "not-determined", "permanent"}); first = launch(a);
    write("/tmp/permission-qa-choice", "allow_application");
    check(command(a, "request", "files.read").value("state").toString() == "granted", "native prompt grants access for the application lifetime");
    check(command(a, "async-check").value("granted").toBool(), "native SDK completion reports the effective grant");
    apps(applications, "Terminate", {a}); delay(1000); first = launch(a);
    check(command(a, "check", "files.read").value("state").toString() == "not-determined", "termination invalidates instance-bound grants");
    write("/tmp/permission-qa-choice", "hold");
    check(command(a, "begin").value("started").toBool(), "start a pending authorization before termination");
    delay(500); apps(applications, "Terminate", {a}); delay(1000); first = launch(a);
    check(command(a, "check", "files.read").value("state").toString() == "not-determined", "application termination cancels authorization without persisting a grant");
    check(command(a, "begin").value("started").toBool(), "start authorization before promptd crash"); delay(600);
    const auto promptBefore = pid("/run/openwave/promptd.pid"); check(promptBefore > 1, "promptd is supervised");
    if (promptBefore > 1) kill(promptBefore, SIGKILL);
    for (int i = 0; i < 50 && read(storage(a) + "/qa-pending-result").isEmpty(); ++i) delay(100);
    const auto promptFailure = command(a, "pending");
    check(promptFailure.value("state").toInt(-1) == OpenWave::PermissionClient::Denied,
        "promptd crash completes pending authorization without granting access");
    delay(1200);
    check(command(a, "check", "files.read").value("state").toString() == "not-determined", "promptd failure does not persist a permission decision");
    const auto serviceBefore = pid("/run/openwave/permissiond.pid");
    const bool disabled = QFile::rename("/System/Services/permissiond", "/System/Services/permissiond.qa-disabled");
    check(disabled, "temporarily disable permissiond for fail-closed test");
    if (disabled) {
        if (serviceBefore > 1) kill(serviceBefore, SIGKILL); delay(800);
        check(!command(a, "open").value("error").toString().isEmpty(), "unavailable permissiond prevents actual brokered file access");
        check(QFile::rename("/System/Services/permissiond.qa-disabled", "/System/Services/permissiond"), "restore permissiond executable");
        delay(1600);
        check(command(a, "check", "files.read").value("state").toString() == "not-determined", "framework reconnects safely after service recovery");
    }
    apps(applications, "Terminate", {a}); delay(1000);
    const QString bundle = "/Applications/" + a;
    const bool removed = QDir().rename(bundle, bundle + ".qa-removed");
    check(removed, "remove verified application installation");
    if (removed) {
        delay(2600);
        const auto database = QJsonDocument::fromJson(read("/var/lib/openwave/permissions/0/policies.json")).object();
        bool retained = false;
        for (const auto &value : database.value("policies").toArray()) retained |= value.toObject().value("application").toString() == a;
        check(!retained, "ApplicationRemoved purges the application's persistent policies");
        check(QDir().rename(bundle + ".qa-removed", bundle), "restore application installation"); delay(2600);
        first = launch(a);
        check(command(a, "check", "files.read").value("state").toString() == "not-determined", "reinstalled app cannot inherit removed grants");
    }
    write("/tmp/permission-qa-choice", "deny");

    QJsonObject report{{"failures", failures}};
    write("/tmp/permission-qa-result.json", QJsonDocument(report).toJson());
    QTextStream(stdout) << "PERMISSION_QA_RESULT failures=" << failures << Qt::endl;
    return failures ? 1 : 0;
}
}
int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    QQuickWindow window; window.setWidth(768); window.setHeight(1366); window.setColor(Qt::white); window.show();
    if (application.arguments().contains("--fixture-a")) return fixture(application, "org.openwave.qa.permission-a");
    if (application.arguments().contains("--fixture-b")) return fixture(application, "org.openwave.qa.permission-b");
    QTimer::singleShot(500, &application, [&] { application.exit(coordinator(application)); });
    return application.exec();
}
