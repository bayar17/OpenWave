#include "SettingsBackend.h"
#include "SettingsEnvironment.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class SettingsEnvironmentTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesPreferencesReadOnly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettingsBackend backend(directory.filePath(QStringLiteral("settings.ini")));
        SettingsEnvironment environment(&backend);
        QCOMPARE(environment.deviceName(), backend.deviceName());
        QVERIFY(!environment.largeText());
        QVERIFY(!environment.reduceMotion());
        QCOMPARE(environment.metaObject()->indexOfMethod("setLargeText(bool)"), -1);
        QCOMPARE(environment.metaObject()->indexOfMethod("saveDeviceName(QString)"), -1);
        QVERIFY(!environment.metaObject()->property(environment.metaObject()->indexOfProperty("largeText")).isWritable());
    }

    void followsBackendChanges()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettingsBackend backend(directory.filePath(QStringLiteral("settings.ini")));
        SettingsEnvironment environment(&backend);
        QSignalSpy changed(&environment, &SettingsEnvironment::changed);
        QVERIFY(backend.setLargeText(true));
        QVERIFY(environment.largeText());
        QVERIFY(backend.setReduceMotion(true));
        QVERIFY(environment.reduceMotion());
        QVERIFY(backend.saveDeviceName(QStringLiteral("Test Phone")));
        QCOMPARE(environment.deviceName(), QStringLiteral("Test Phone"));
        QVERIFY(changed.count() >= 3);
    }

    void toleratesMissingBackend()
    {
        SettingsEnvironment environment(nullptr);
        QCOMPARE(environment.deviceName(), QString());
        QVERIFY(!environment.largeText());
        QVERIFY(!environment.reduceMotion());
    }
};

QTEST_MAIN(SettingsEnvironmentTest)
#include "tst_settingsenvironment.moc"
