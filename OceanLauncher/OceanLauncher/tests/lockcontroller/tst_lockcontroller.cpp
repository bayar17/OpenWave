#include "../../LockController.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LockControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void lockStateSurvivesRestartAndDevelopmentUnlock()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString statePath = directory.filePath(QStringLiteral("lock-state"));
        {
            LockController controller(statePath);
            QCOMPARE(controller.state(), LockController::Unlocked);
            QVERIFY(controller.requestLock());
            QCOMPARE(controller.state(), LockController::Locked);
            QFile stateFile(statePath);
            QVERIFY(stateFile.open(QIODevice::ReadOnly));
            QCOMPARE(stateFile.readAll(), QByteArrayLiteral("locked\n"));
            const QFileDevice::Permissions permissions = QFileInfo(statePath).permissions();
            QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                                     | QFileDevice::ReadOther | QFileDevice::WriteOther)));
        }

        LockController restarted(statePath);
        QCOMPARE(restarted.state(), LockController::Locked);
        QVERIFY(restarted.locked());
        QVERIFY(restarted.requestDevelopmentSwipeUnlock());
        QCOMPARE(restarted.state(), LockController::UnlockRequested);
        QTRY_COMPARE(restarted.state(), LockController::Authenticating);
        QTRY_VERIFY(restarted.unlockAuthorized());
        QVERIFY(restarted.finishDevelopmentSwipeUnlock());
        QCOMPARE(restarted.state(), LockController::Unlocked);
        QVERIFY(!QFileInfo::exists(statePath));
    }

    void invalidStateFailsClosed()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString statePath = directory.filePath(QStringLiteral("lock-state"));
        QFile stateFile(statePath);
        QVERIFY(stateFile.open(QIODevice::WriteOnly));
        QCOMPARE(stateFile.write("invalid\n"), qint64(8));
        stateFile.close();

        LockController controller(statePath);
        QCOMPARE(controller.state(), LockController::Locked);
        QVERIFY(controller.requestDevelopmentSwipeUnlock());
        QTRY_VERIFY(controller.unlockAuthorized());
        controller.cancelUnlockRequest();
        QCOMPARE(controller.state(), LockController::Locked);
        QVERIFY(!controller.finishDevelopmentSwipeUnlock());
    }

    void failedPersistenceFailsClosedInCurrentSession()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LockController controller(directory.filePath(QStringLiteral("missing/lock-state")));
        QSignalSpy persistenceError(&controller, &LockController::persistenceError);
        QVERIFY(!controller.requestLock());
        QCOMPARE(controller.state(), LockController::Locked);
        QCOMPARE(persistenceError.count(), 1);
    }
};

QTEST_GUILESS_MAIN(LockControllerTests)
#include "tst_lockcontroller.moc"
