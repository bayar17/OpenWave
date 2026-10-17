#include "../WebAppDefinition.h"
#include "../WebAppIcon.h"
#include <QImage>
#include <QBuffer>
#include <OpenWave/ApplicationsClient.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QSaveFile>
#include <QUrlQuery>
#include <csignal>
#include <unistd.h>

namespace {
int failures = 0;
void check(bool okay, const QString &label) {
    QTextStream(stdout) << "WEBAPP_QA " << (okay ? "PASS " : "FAIL ") << label << Qt::endl;
    failures += !okay;
}
void delay(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
QVariantList rpc(ApplicationsClient &client, const QString &method, const QVariantList &args, QString *error = nullptr) {
    QEventLoop loop; QVariantList reply; QString failure; bool done = false;
    client.call(method, args, [&](const QVariantList &values, const QString &message) {
        reply = values; failure = message; done = true; loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit); loop.exec();
    if (!done) _exit(90);
    if (error) *error = failure;
    else check(failure.isEmpty(), method + " " + failure);
    return reply;
}
QVariantMap info(ApplicationsClient &client, const QString &id) {
    QString error;
    return rpc(client, "GetApplicationInfo", {id}, &error).value(0).toMap();
}
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool waitFor(const std::function<bool()> &condition, int timeout = 15000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) delay(100);
    return condition();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    QVector<QUrlQuery> reports;
    if (!server.listen(QHostAddress::LocalHost, 18081)) return 1;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                socket->setProperty("handled", true);
                const QUrl url = QUrl::fromEncoded(request.split(' ').value(1));
                QByteArray body;
                QByteArray type = "text/html";
                if (url.path() == "/favicon.ico" || url.path() == "/large.png") {
                    QImage image(url.path() == "/large.png" ? 512 : 16, url.path() == "/large.png" ? 512 : 16, QImage::Format_ARGB32);
                    image.fill(url.path() == "/large.png" ? QColor("#1678ed") : QColor("red"));
                    QBuffer buffer(&body); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); type = "image/png";
                } else if (url.path() == "/site.webmanifest") {
                    body = R"({"icons":[{"src":"large.png","sizes":"512x512"}]})"; type = "application/manifest+json";
                } else
                if (url.path() == "/report") {
                    reports.append(QUrlQuery(url));
                    QTextStream(stdout) << "WEBAPP_QA_REPORT " << reports.size() << Qt::endl;
                    QTextStream(stdout) << "WEBAPP_QA_VIEWPORT " << url.toString() << Qt::endl;
                    body = "OK";
                } else body = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenWave Web App QA</title><link rel="icon" href="/favicon.ico"><link rel="manifest" href="/site.webmanifest">
</head><body style="margin:0;font:24px sans-serif;background:white;color:#202124">
<header style="height:80px;padding:16px;box-sizing:border-box">Website header</header>
<button style="margin:24px;font-size:24px" onclick="document.body.style.background='#18212b';document.body.style.color='white'">Change background</button>
<h1 style="margin:24px">Web app fixture</h1><p>WPE WebKit and persistent application storage.</p><script>
fetch('/report?stored='+encodeURIComponent(localStorage.getItem('ow-webapp-qa'))+'&cookie='+encodeURIComponent(document.cookie)+'&width='+innerWidth+'&height='+innerHeight)
.then(()=>{localStorage.setItem('ow-webapp-qa','saved');document.cookie='ow_webapp=present;max-age=3600;path=/';});
</script></body></html>)HTML";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket");
    ApplicationsClient client;
    check(waitFor([&] { return QFileInfo::exists("/run/ocean/wayland-0"); }, 60000), "Wayland session ready");
    delay(1000);
    QString error;
    rpc(client, "CreateWebApp", {"Forged", "http://127.0.0.1:18081/forged"}, &error);
    check(!error.isEmpty(), "unmanaged root process cannot impersonate Browser installer");
    rpc(client, "UninstallWebApp", {"com.openwave.settings"}, &error);
    check(!error.isEmpty(), "unmanaged caller cannot uninstall applications");
    rpc(client, "CreateWebAppWithIcon", {"Forged", "http://127.0.0.1:18081/forged", QByteArray("bad")}, &error);
    check(!error.isEmpty(), "icon installer rejects caller spoofing");
    rpc(client, "UpdateWebAppIcon", {QByteArray("bad")}, &error);
    check(!error.isEmpty(), "unmanaged process cannot replace a web app icon");
    const QString address = "http://127.0.0.1:18081/app";
    const QString id = OpenWaveWebApp::identifier(QUrl(address));
    rpc(client, "Launch", {"com.openwave.ocean", QStringList{}});
    QTextStream(stdout) << "WEBAPP_QA_BROWSER_READY" << Qt::endl;
    check(waitFor([&] { return !reports.isEmpty(); }, 120000), "Browser opened fixture");
    QTextStream(stdout) << "WEBAPP_QA_AWAITING_INSTALL" << Qt::endl;
    const bool installed = waitFor([&] { return info(client, id).value("name").toString() == "qa dashboard"; }, 120000);
    check(installed, "native Browser creation registered chosen name");
    if (installed) {
        const QString bundle = "/Applications/" + id;
        const auto saved = QJsonDocument::fromJson(read(bundle + "/manifest.json")).object();
        check(saved.value("webApp").toObject().value("url").toString() == address, "URL is the captured read-only address");
        check(saved.value("permissions").toArray().isEmpty(), "shortcut has no inherited permissions");
        check(!QFileInfo(bundle + "/bin/openwave-webapp").isSymLink()
            && read(bundle + "/bin/openwave-webapp") == read("/usr/libexec/openwave-webapp"), "dedicated verified WPE host binary");
        check(QFileInfo(bundle + "/manifest.json").ownerId() == 0, "installed metadata belongs to system");
        const auto iconPath = saved.value("icon").toString();
        check(iconPath == "resources/icon.png" && OpenWaveWebApp::validIcon(read(bundle + '/' + iconPath)), "appd stores validated website PNG");
        check(QImage::fromData(read(bundle + '/' + iconPath)).size() == QSize(512, 512), "manifest icon wins over tiny favicon");
        const int before = reports.size();
        rpc(client, "Launch", {id, QStringList{}});
        check(waitFor([&] { return reports.size() > before; }), "web app renders fixture");
        if (reports.size() > before) {
            check(reports.last().queryItemValue("stored") == "null"
                && reports.last().queryItemValue("cookie").isEmpty(), "web app does not inherit Browser cookies or local storage");
            check(reports.last().queryItemValue("width").toInt() == 500,
                "web app uses the readable mobile viewport width");
            check(reports.last().queryItemValue("height").toInt() > 750
                && reports.last().queryItemValue("height").toInt() < 900,
                "web app mobile viewport fits below the status bar");
        }
        const auto running = info(client, id);
        const auto pid = running.value("pid").toUInt();
        const auto context = read(QString("/proc/%1/attr/current").arg(pid));
        check(context.contains(":openwave_app_t:"), "web app uses untrusted application SELinux domain");
        QTextStream(stdout) << "WEBAPP_QA_APPEARANCE_READY" << Qt::endl;
        delay(14000);
        rpc(client, "Terminate", {id});
        check(waitFor([&] { return info(client, id).value("state").toString() == "Stopped"; }), "web app stops normally");
        const int previous = reports.size();
        rpc(client, "Launch", {id, QStringList{}});
        check(waitFor([&] { return reports.size() > previous; }), "web app reopens from its installed bundle");
        if (reports.size() > previous) {
            check(reports.last().queryItemValue("stored") == "saved", "local storage survives application restart");
            check(reports.last().queryItemValue("cookie").contains("ow_webapp=present"), "cookies survive application restart");
        }
        rpc(client, "Terminate", {id});
        check(waitFor([&] { return info(client, id).value("state").toString() == "Stopped"; }), "second web app process stops");
        auto legacy = saved;
        legacy.insert("icon", "resources/icon.svg");
        const auto legacyBytes = QJsonDocument(legacy).toJson();
        QSaveFile legacyManifest(bundle + "/manifest.json");
        check(QFile::copy("/usr/share/pixmaps/openwave-webapp.svg", bundle + "/resources/icon.svg")
            && legacyManifest.open(QIODevice::WriteOnly)
            && legacyManifest.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther)
            && legacyManifest.write(legacyBytes) == legacyBytes.size() && legacyManifest.commit(), "legacy generic icon fixture prepared");
        const auto legacyDigest = QString::fromLatin1(QCryptographicHash::hash(legacyBytes, QCryptographicHash::Sha256).toHex());
        check(waitFor([&] { return info(client, id).value("manifestDigest").toString() == legacyDigest; }), "registry observes legacy bundle");
        rpc(client, "Launch", {id, QStringList{}});
        check(waitFor([&] {
            const auto updated = QJsonDocument::fromJson(read(bundle + "/manifest.json")).object();
            return updated.value("icon").toString().startsWith("resources/icon-");
        }), "existing shortcut refreshes its own icon through appd");
        const auto upgraded = QJsonDocument::fromJson(read(bundle + "/manifest.json")).object();
        check(upgraded.value("name") == saved.value("name") && upgraded.value("webApp") == saved.value("webApp"), "icon refresh preserves name and URL");
        check(info(client, id).value("icon").toString().contains("/resources/icon-"), "running app registry publishes refreshed icon");
        rpc(client, "Terminate", {id});
        check(waitFor([&] { return info(client, id).value("state").toString() == "Stopped"; }), "upgraded shortcut stops normally");
        const auto oldPid = read("/run/openwave/appd.pid").trimmed().toInt();
        if (oldPid > 1) kill(oldPid, SIGTERM);
        check(waitFor([&] { return read("/run/openwave/appd.pid").trimmed().toInt() != oldPid
            && info(client, id).value("name").toString() == "qa dashboard"; }, 30000), "shortcut survives application service restart");
        check(info(client, id).value("canUninstall").toBool() && !info(client, "com.openwave.settings").value("canUninstall").toBool(), "only website apps advertise Uninstall");
        const QString userData = "/Data/Users/root/.DataLocation/Applications/" + id;
        const QString shared = "/Data/Users/root/Downloads/webapp-uninstall-keep.txt";
        QFile marker(shared); check(marker.open(QIODevice::WriteOnly) && marker.write("keep") == 4, "shared download fixture created"); marker.close();
        check(QFile::link("/Data/Users/root/Downloads", userData + "/Cache/shared-link"), "private cache contains a shared-folder link");
        rpc(client, "Launch", {id, QStringList{}});
        check(waitFor([&] { return info(client, id).value("pid").toUInt() > 1; }), "web app running before uninstall");
        rpc(client, "Launch", {"com.openwave.settings", QStringList{}});
        QTextStream(stdout) << "WEBAPP_QA_SETTINGS_READY" << Qt::endl;
        delay(14000);
        check(QFileInfo::exists(bundle + "/manifest.json") && QFileInfo::exists(userData + "/Data/WebKit/cookies/cookies.sqlite"), "Cancel preserves installed app and cookies");
        QTextStream(stdout) << "WEBAPP_QA_CANCEL_CHECKED" << Qt::endl;
        check(waitFor([&] { return !QFileInfo::exists(bundle) && !QFileInfo::exists(userData); }, 120000), "Settings uninstall removes bundle and all private storage");
        check(waitFor([&] { return info(client, id).isEmpty(); }), "uninstalled web app disappears from registry");
        check(QFileInfo::exists(shared) && read(shared) == "keep", "shared downloads survive linked-folder cleanup");
        check(!QFileInfo::exists("/Applications/.uninstall-" + id)
            && !QFileInfo::exists("/Data/Users/root/.DataLocation/Applications/.uninstall-" + id), "uninstall leaves no pending storage");
        check(info(client, "com.openwave.settings").value("pid").toUInt() > 1, "Settings remains running after uninstall");
    }
    QTextStream(stdout) << "WEBAPP_QA_EXIT=" << (failures ? 1 : 0) << Qt::endl;
    delay(1500);
    kill(1, SIGUSR2);
    return failures ? 1 : 0;
}
