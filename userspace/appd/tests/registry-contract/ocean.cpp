#include "../../../../OceanLauncher/OceanLauncher/AppController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static int readyTrue = 0;
struct ApplicationsClient::Transport {};
ApplicationsClient::ApplicationsClient(QObject *parent) : QObject(parent), m_transport(new Transport)
{ QTimer::singleShot(0,this,[this]{ emit serviceChanged(true); }); }
ApplicationsClient::~ApplicationsClient() = default;
void ApplicationsClient::call(const QString &method, const QVariantList &arguments, Completion done)
{
    if (method == "SetSessionReady" && arguments.value(0).toBool()) ++readyTrue;
    const QVariantList result = method == "SetSessionReady" ? QVariantList{true} : QVariantList{};
    QTimer::singleShot(0,this,[done,result]{ done(result,{}); });
}
void ApplicationsClient::listApplications(Completion done)
{
    const QVariantList rows{
        QVariantMap{{"appId","com.openwave.settings"},{"name","Settings"},{"launchable",true}},
        QVariantMap{{"appId","com.openwave.shell"},{"launchable",true}},
        QVariantMap{{"appId","org.example.component"},{"launchable",false}},
        QVariantMap{{"appId","org.example.unknown"}},
        QVariantMap{{"appId","org.example.malformed"},{"launchable","true"}}};
    QTimer::singleShot(0,this,[done,rows]{done(QVariantList{QVariant(rows)},{});});
}
void ApplicationsClient::getRunningApplications(Completion done)
{ QTimer::singleShot(0,this,[done]{done(QVariantList{QVariant(QVariantList{})},{});}); }
void ApplicationsClient::getApplicationInfo(const QString &, Completion done) { call("GetApplicationInfo",{},done); }
void ApplicationsClient::launch(const QString &,const QStringList &,Completion done) { call("Launch",{},done); }
void ApplicationsClient::activate(const QString &,Completion done) { call("Activate",{},done); }
void ApplicationsClient::terminate(const QString &,Completion done) { call("Terminate",{},done); }
static void check(bool okay,const char *why) { if (!okay) { std::fprintf(stderr,"%s\n",why); std::exit(1); } }
static void settle()
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed()<50) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv); QTemporaryDir temporary;
    check(temporary.isValid(),"temporary profile");
    qputenv("XDG_CONFIG_HOME",temporary.path().toUtf8());
    qputenv("XDG_RUNTIME_DIR",temporary.path().toUtf8());
    qunsetenv("OPENWAVE_SESSION_USER");
    AppController controller;
    settle();
    check(controller.installedApps() == QStringList{"com.openwave.settings"},"Ocean must consume only explicit, valid launchable records");
    controller.setSessionReady(true); settle();
    check(readyTrue==0,"compositor creation alone must not announce readiness");
    controller.setGraphicsReady(true); settle();
    check(readyTrue==0 && controller.sessionState()=="failed","missing socket must fail closed");
    controller.setGraphicsReady(false); settle();
    QLocalServer socket; check(socket.listen(temporary.path()+"/wayland-0"),"create owned compositor socket");
    controller.setGraphicsReady(true); settle();
    check(readyTrue==1 && controller.sessionState()=="session-ready","owned socket, compositor and rendered frame must complete readiness");
    for (int i=0;i<10;++i) controller.setGraphicsReady(true);
    settle(); check(readyTrue==1,"each rendered frame must not repeat the readiness RPC");
    controller.setSessionReady(false); settle();
    check(controller.sessionState()=="initializing","compositor invalidation must revoke readiness");
    std::puts("PASS actual Ocean controller: metadata consumption, graphics/socket gates, handshake and invalidation");
}
