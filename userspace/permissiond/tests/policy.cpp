#include "../PolicyStore.h"
#include "../PermissionRegistry.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>

class PolicyTests : public QObject {
    Q_OBJECT
private slots:
    void registryWithoutProvidersNeverAuthorizesHardware() {
        PermissionRegistry registry;
        QCOMPARE(registry.definition("camera").label, "Camera");
        QVERIFY(registry.add({"sensors.future", "Future sensor", "A future protected sensor"}));
        QVERIFY(!registry.add({"../camera", "Invalid", ""}));
        QVERIFY(!registry.add({"camera\n", "Invalid", ""}));
        QVERIFY(!PolicyStore::validPermission(QString("files.read") + QChar(0)));
        QVERIFY(!registry.add({"camera", "Replacement", ""}));
        QVERIFY(!PolicyStore::permissions().contains("sensors.future"));
    }
    void applicationLifetimeAndUninstall() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "reader"; p.permission = "files.read"; p.state = "granted";
        p.duration = "application"; p.session = "session"; p.instance = "launch-a";
        p.applicationDigest = QString(64, 'a'); p.requestedAt = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(store.set(p, &error));
        QCOMPARE(store.get(0, "reader", "files.read", "session", "launch-a").state, "granted");
        QCOMPARE(store.get(0, "reader", "files.read", "session", "launch-b").state, "not-determined");
        PolicyStore restored(directory.path()); QVERIFY(restored.load(&error));
        QVERIFY(restored.expireInstance(0, "reader", "launch-b", &error));
        QCOMPARE(restored.get(0, "reader", "files.read", "session", "launch-a").state, "granted");
        QVERIFY(restored.expireInstance(0, "reader", "launch-a", &error));
        QCOMPARE(restored.get(0, "reader", "files.read", "session", "launch-a").state, "not-determined");
        QCOMPARE(restored.list(0, "reader").first().requestedAt, p.requestedAt);
        QVERIFY(restored.removeApplication(1, "reader", &error)); QVERIFY(!restored.list(0).isEmpty());
        QVERIFY(restored.removeApplication(0, "reader", &error));
        PolicyStore removed(directory.path()); QVERIFY(removed.load(&error)); QVERIFY(removed.list(0).isEmpty());
    }
    void persistenceAndPrivacy() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
        PolicyStore store(directory.path()); QVERIFY2(store.load(&error), qPrintable(error));
        QCOMPARE(store.get(1000, "reader", "files.read", "session-a").state, "not-determined");
        PermissionPolicy p; p.user = 1000; p.application = "reader"; p.permission = "files.read"; p.state = "granted";
        QVERIFY2(store.set(p, &error), qPrintable(error));
        PolicyStore reloaded(directory.path()); QVERIFY2(reloaded.load(&error), qPrintable(error));
        QCOMPARE(reloaded.get(1000, "reader", "files.read", "session-b").state, "granted");
        QCOMPARE(reloaded.get(1001, "reader", "files.read", "session-b").state, "not-determined");
        QCOMPARE(reloaded.get(1000, "other-app", "files.read", "session-b").state, "not-determined");
        struct stat st; QVERIFY(!stat(QFile::encodeName(directory.path() + "/policies.json").constData(), &st));
        QCOMPARE(st.st_mode & 0777, 0600u);
        QVERIFY(!stat(QFile::encodeName(directory.path()).constData(), &st)); QCOMPARE(st.st_mode & 0777, 0700u);
    }
    void denialRestrictedAndRevocation() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "com.openwave.ocean"; p.permission = "files.write";
        for (const QString &state : {"denied", "restricted", "granted", "not-determined"}) {
            p.state = state; QVERIFY(store.set(p, &error)); QCOMPARE(store.get(0, "com.openwave.ocean", "files.write", "a").state, state);
            PolicyStore restored(directory.path()); QVERIFY(restored.load(&error)); QCOMPARE(restored.get(0, "com.openwave.ocean", "files.write", "a").state, state);
        }
        QCOMPARE(store.get(0, "com.openwave.ocean", "camera", "a").state, "restricted");
        p.permission = "camera"; QVERIFY(!store.set(p, &error));
        p.permission = "files.read"; p.state = "fake-grant"; QVERIFY(!store.set(p, &error));
    }
    void sessionAndOneTimeExpiry() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "com.openwave.ocean"; p.permission = "files.read"; p.state = "granted";
        p.duration = "session"; p.session = "a"; QVERIFY(store.set(p, &error));
        PolicyStore restored(directory.path()); QVERIFY(restored.load(&error));
        QCOMPARE(restored.get(0, "com.openwave.ocean", "files.read", "a").state, "granted");
        QCOMPARE(restored.get(0, "com.openwave.ocean", "files.read", "b").state, "not-determined");
        p.duration = "once"; p.expires = QDateTime::currentMSecsSinceEpoch() + 10000; QVERIFY(store.set(p, &error));
        QCOMPARE(store.get(0, "com.openwave.ocean", "files.read", "a").state, "granted");
        PolicyStore restarted(directory.path()); QVERIFY(restarted.load(&error)); QCOMPARE(restarted.get(0, "com.openwave.ocean", "files.read", "a").state, "not-determined");
        p.expires = QDateTime::currentMSecsSinceEpoch() - 1; QVERIFY(store.set(p, &error));
        QCOMPARE(store.get(0, "com.openwave.ocean", "files.read", "a").state, "not-determined");
    }
    void corruptDatabaseFailsClosed() {
        for (const QByteArray &bytes : {QByteArray("garbage"), QByteArray("{\"version\":2,\"policies\":[]}"),
             QByteArray("{\"version\":1,\"policies\":[{\"application\":\"../spoof\"}]}"), QByteArray("{}")}) {
            QTemporaryDir directory; QFile file(directory.filePath("policies.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes);
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner); file.close();
            PolicyStore store(directory.path()); QString error; QVERIFY(!store.load(&error)); QVERIFY(!error.isEmpty());
            QCOMPARE(store.get(0, "com.openwave.ocean", "files.read", "a").state, "not-determined");
        }
    }
    void symlinksAndUnsafeModesRejected() {
        QTemporaryDir directory, outside;
        QFile target(outside.filePath("policy.json")); QVERIFY(target.open(QIODevice::WriteOnly)); target.write("secret"); target.close();
        QVERIFY(QFile::link(target.fileName(), directory.filePath("policies.json")));
        PolicyStore store(directory.path()); QString error; QVERIFY(!store.load(&error));
        QFile::remove(directory.filePath("policies.json"));
        QFile file(directory.filePath("policies.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"version\":1,\"policies\":[]}"); file.close();
        QVERIFY(!chmod(QFile::encodeName(file.fileName()).constData(), 0644)); QVERIFY(!store.load(&error));
    }
    void interruptedRevocationPersists() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "reader"; p.permission = "files.read"; p.state = "denied";
        p.revocationPending = true; QVERIFY(store.set(p, &error));
        PolicyStore restart(directory.path()); QVERIFY(restart.load(&error));
        auto pending = restart.get(0, "reader", "files.read", "a"); QVERIFY(pending.revocationPending); QCOMPARE(pending.state, "denied");
        pending.revocationPending = false; QVERIFY(restart.set(pending, &error));
        PolicyStore complete(directory.path()); QVERIFY(complete.load(&error)); QVERIFY(!complete.get(0, "reader", "files.read", "a").revocationPending);
    }
    void creationTimestampPreserved() {
        QTemporaryDir directory; PolicyStore store(directory.path()); QString error; QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "reader"; p.permission = "files.read"; p.state = "denied"; QVERIFY(store.set(p, &error));
        const auto first = store.get(0, "reader", "files.read", "a"); QTest::qWait(2);
        p.state = "granted"; QVERIFY(store.set(p, &error)); const auto next = store.get(0, "reader", "files.read", "a");
        QCOMPARE(next.created, first.created); QVERIFY(next.modified >= first.modified);
    }
    void requestedPermissionsSurviveOneTimeGrantAndRevocation() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "reader"; p.permission = "files.read"; p.requestedAt = QDateTime::currentMSecsSinceEpoch();
        const auto first = p.requestedAt; QVERIFY(store.set(p, &error));
        p.requestedAt = 0; p.state = "granted"; p.duration = "once"; p.expires = first + 10000; QVERIFY(store.set(p, &error));
        PolicyStore restarted(directory.path()); QVERIFY(restarted.load(&error));
        const auto restored = restarted.get(0, "reader", "files.read", "session");
        QCOMPARE(restored.state, "not-determined"); QCOMPARE(restored.requestedAt, first);
        p.duration = "permanent"; p.state = "denied"; QVERIFY(restarted.set(p, &error));
        QCOMPARE(restarted.get(0, "reader", "files.read", "session").requestedAt, first);
    }
    void legacyIdentityMigrationPreservesPolicy() {
        QTemporaryDir directory; QString error; PolicyStore old(directory.path()); QVERIFY(old.load(&error));
        PermissionPolicy policy; policy.application = "browser"; policy.permission = "files.read";
        policy.state = "denied"; policy.requestedAt = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(old.set(policy, &error));
        PolicyStore migrated(directory.path()); QVERIFY(migrated.load(&error));
        const auto restored = migrated.get(0, "com.openwave.ocean", "files.read", "session");
        QCOMPARE(restored.state, "denied"); QCOMPARE(restored.requestedAt, policy.requestedAt);
        QFile file(directory.filePath("policies.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll(); QVERIFY(bytes.contains("com.openwave.ocean")); QVERIFY(!bytes.contains("\"application\":\"browser\""));
    }
    void conflictingMigrationFailsWithoutOverwriting() {
        QTemporaryDir directory; QString error; PolicyStore old(directory.path()); QVERIFY(old.load(&error));
        PermissionPolicy policy; policy.application = "browser"; policy.permission = "files.read"; policy.state = "denied";
        QVERIFY(old.set(policy, &error)); policy.application = "com.openwave.ocean"; policy.state = "granted"; QVERIFY(old.set(policy, &error));
        QFile before(directory.filePath("policies.json")); QVERIFY(before.open(QIODevice::ReadOnly)); const auto bytes = before.readAll(); before.close();
        PolicyStore migrated(directory.path()); QVERIFY(!migrated.load(&error));
        QFile after(directory.filePath("policies.json")); QVERIFY(after.open(QIODevice::ReadOnly)); QCOMPARE(after.readAll(), bytes);
    }
    void unsupportedRequestsAreRecordedButNeverGranted() {
        QTemporaryDir directory; QString error; PolicyStore store(directory.path()); QVERIFY(store.load(&error));
        PermissionPolicy p; p.application = "reader"; p.permission = "camera"; p.state = "restricted"; p.requestedAt = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(store.set(p, &error));
        PolicyStore restored(directory.path()); QVERIFY(restored.load(&error));
        QCOMPARE(restored.get(0, "reader", "camera", "session").requestedAt, p.requestedAt);
        p.state = "granted"; QVERIFY(!store.set(p, &error));
        p.permission = "../camera"; p.state = "restricted"; QVERIFY(!store.set(p, &error));
    }
};
QTEST_GUILESS_MAIN(PolicyTests)
#include "policy.moc"
