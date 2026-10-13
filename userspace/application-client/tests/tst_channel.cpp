#include "../ApplicationChannel.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QLocalSocket>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

class ChannelTests : public QObject
{
    Q_OBJECT
private slots:
    void jsonDeliveryAndErrors()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        QJsonObject received;
        ApplicationChannel launcher(OpenWaveApplications::launcherAddress(),
            [&received](const QJsonObject &message) {
                received = message;
                return message.value("appId") == "files" ? QString() : QStringLiteral("Not installed");
            });
        bool done = false;
        QString error;
        OpenWaveApplications::request(OpenWaveApplications::launcherAddress(),
            {{"command", "launch"}, {"appId", "files"},
             {"arguments", QJsonArray{"/Data/Users/test/Downloads/a file.png"}}}, this,
            [&](const QString &failure) { error = failure; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(received.value("command").toString(), QStringLiteral("launch"));
        QCOMPARE(received.value("arguments").toArray().first().toString(),
                 QStringLiteral("/Data/Users/test/Downloads/a file.png"));
        QVERIFY(!(QFileInfo(OpenWaveApplications::launcherAddress()).permissions()
                  & (QFile::ReadOther | QFile::WriteOther)));
        done = false;
        OpenWaveApplications::request(OpenWaveApplications::launcherAddress(),
            {{"command", "launch"}, {"appId", "unknown"}}, this,
            [&](const QString &failure) { error = failure; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(error, QStringLiteral("Not installed"));
    }
    void instanceIsolationAndCleanup()
    {
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        int filesQuit = 0, browserQuit = 0;
        const QString files = OpenWaveApplications::instanceAddress(101);
        {
            ApplicationChannel filesEndpoint(files, [&](const QJsonObject &) { ++filesQuit; return QString(); });
            ApplicationChannel browserEndpoint(OpenWaveApplications::instanceAddress(102),
                [&](const QJsonObject &) { ++browserQuit; return QString(); });
            bool done = false;
            OpenWaveApplications::request(files, {{"command", "quit"}}, this,
                [&](const QString &error) { QVERIFY2(error.isEmpty(), qPrintable(error)); done = true; });
            QTRY_VERIFY(done);
            QCOMPARE(filesQuit, 1);
            QCOMPARE(browserQuit, 0);
        }
        QVERIFY(!QFileInfo::exists(files));
        bool done = false;
        int callbacks = 0;
        OpenWaveApplications::request(files, {{"command", "quit"}}, this,
            [&](const QString &error) { QVERIFY(!error.isEmpty()); ++callbacks; done = true; });
        QTRY_VERIFY(done);
        QTest::qWait(30);
        QCOMPARE(callbacks, 1);
    }
    void malformedAndOversizedRequests()
    {
        QTemporaryDir runtime;
        const QString address = runtime.filePath("endpoint");
        int calls = 0;
        ApplicationChannel endpoint(address, [&](const QJsonObject &) { ++calls; return QString(); });
        QLocalSocket socket;
        socket.connectToServer(address);
        QVERIFY(socket.waitForConnected());
        socket.write("not json\n");
        QTRY_VERIFY(socket.canReadLine());
        QVERIFY(socket.readLine().contains("Invalid application request"));
        QCOMPARE(calls, 0);
        QLocalSocket oversized;
        oversized.connectToServer(address);
        QVERIFY(oversized.waitForConnected());
        oversized.write(QByteArray(17000, 'x'));
        QTRY_COMPARE(oversized.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(calls, 0);
    }
    void gracefulProcessShutdown()
    {
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        QProcess files, browser;
        const QString executable = QCoreApplication::applicationFilePath();
        files.start(executable, {"--instance"});
        browser.start(executable, {"--instance"});
        QVERIFY(files.waitForStarted());
        QVERIFY(browser.waitForStarted());
        const QString endpoint = OpenWaveApplications::instanceAddress(files.processId());
        QTRY_VERIFY(QFileInfo::exists(endpoint));
        bool done = false;
        OpenWaveApplications::request(endpoint, {{"command", "quit"}}, this,
            [&](const QString &error) { QVERIFY2(error.isEmpty(), qPrintable(error)); done = true; });
        QTRY_VERIFY(done);
        QTRY_COMPARE(files.state(), QProcess::NotRunning);
        QCOMPARE(files.exitStatus(), QProcess::NormalExit);
        QCOMPARE(files.exitCode(), 0);
        QCOMPARE(browser.state(), QProcess::Running);
        QVERIFY(!QFileInfo::exists(endpoint));
        OpenWaveApplications::request(OpenWaveApplications::instanceAddress(browser.processId()),
            {{"command", "quit"}}, this, [](const QString &) {});
        QTRY_COMPARE(browser.state(), QProcess::NotRunning);
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--instance")) {
        ApplicationChannel endpoint(OpenWaveApplications::instanceAddress(app.applicationPid()),
            [&app](const QJsonObject &) {
                QTimer::singleShot(50, &app, &QCoreApplication::quit);
                return QString();
            });
        return app.exec();
    }
    ChannelTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_channel.moc"
