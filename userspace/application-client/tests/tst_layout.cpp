#include "../../../OceanLauncher/OceanLauncher/AppController.h"
#include <QSettings>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ApplicationLayoutTests : public QObject
{
    Q_OBJECT
    QTemporaryDir profile;

    bool registryReady(const AppController &controller)
    {
        const auto installed = controller.installedApps();
        return installed.contains("com.openwave.files") && installed.contains("com.openwave.settings")
            && installed.contains("com.openwave.ocean");
    }

    void verifyPlacement(const AppController &controller)
    {
        const QStringList placed = controller.favoriteApps() + controller.unpinnedApps();
        const QStringList installed = controller.installedApps();
        QCOMPARE(placed.size(), installed.size());
        QCOMPARE(QSet<QString>(placed.begin(), placed.end()),
                 QSet<QString>(installed.begin(), installed.end()));
    }
private slots:
    void initTestCase()
    {
        QVERIFY(profile.isValid());
        qputenv("XDG_CONFIG_HOME", profile.path().toUtf8());
        qputenv("XDG_RUNTIME_DIR", profile.path().toUtf8());
    }

    void moveAndPersist()
    {
        QStringList favorites, drawer;
        int installedCount = 0;
        {
            AppController controller;
            if (!QFile::exists("/run/openwave/appd.pid")) QSKIP("Requires appd in the target runtime.");
            QTRY_VERIFY(registryReady(controller));
            installedCount = controller.installedApps().size();
            QSignalSpy changes(&controller, &AppController::appLayoutChanged);
            verifyPlacement(controller);
            for (const auto &app : controller.installedApps()) {
                QVERIFY(controller.moveApp(app, false, controller.unpinnedApps().size()));
                verifyPlacement(controller);
            }
            QVERIFY(controller.favoriteApps().isEmpty());
            QCOMPARE(controller.unpinnedApps().size(), installedCount);
            QVERIFY(controller.moveApp("com.openwave.ocean", false, 0));
            QCOMPARE(controller.unpinnedApps().first(), QString("com.openwave.ocean"));
            QVERIFY(controller.moveApp("com.openwave.files", true, 0));
            QVERIFY(controller.moveApp("com.openwave.settings", true, 0));
            verifyPlacement(controller);
            QCOMPARE(controller.unpinnedApps().first(), QString("com.openwave.ocean"));
            QCOMPARE(controller.unpinnedApps().size(), installedCount - 2);
            QVERIFY(controller.moveApp("com.openwave.settings", true, 2));
            QCOMPARE(controller.favoriteApps(), (QStringList{"com.openwave.files", "com.openwave.settings"}));
            QVERIFY(!controller.moveApp("not-installed", false, 0));
            QVERIFY(changes.count() >= 6);
            favorites = controller.favoriteApps();
            drawer = controller.unpinnedApps();
        }
        AppController restored;
        QTRY_COMPARE(restored.installedApps().size(), installedCount);
        QCOMPARE(restored.favoriteApps(), favorites);
        QCOMPARE(restored.unpinnedApps(), drawer);
        verifyPlacement(restored);
    }

    void sanitizeSavedDuplicates()
    {
        QSettings settings(profile.path() + "/OpenWave/settings.ini", QSettings::IniFormat);
        settings.setValue("launcher/favorites", QStringList{"files", "files", "not-installed"});
        settings.setValue("launcher/unpinned", QStringList{"files", "browser", "browser"});
        settings.sync();
        AppController controller;
        if (!QFile::exists("/run/openwave/appd.pid")) QSKIP("Requires appd in the target runtime.");
        QTRY_VERIFY(registryReady(controller));
        QCOMPARE(controller.favoriteApps(), QStringList{"com.openwave.files"});
        QCOMPARE(controller.unpinnedApps().first(), QString("com.openwave.ocean"));
        QVERIFY(controller.unpinnedApps().contains("com.openwave.settings"));
        verifyPlacement(controller);
    }

    void restoreFavoriteCounts_data()
    {
        QTest::addColumn<QStringList>("favorites");
        QTest::newRow("none") << QStringList{};
        QTest::newRow("one") << QStringList{"com.openwave.files"};
        QTest::newRow("two") << (QStringList{"com.openwave.files", "com.openwave.ocean"});
        QTest::newRow("three") << (QStringList{"com.openwave.files", "com.openwave.ocean", "com.openwave.settings"});
    }

    void restoreFavoriteCounts()
    {
        QFETCH(QStringList, favorites);
        QSettings settings(profile.path() + "/OpenWave/settings.ini", QSettings::IniFormat);
        settings.setValue("launcher/favorites", favorites);
        settings.setValue("launcher/unpinned", QStringList{"com.openwave.settings", "com.openwave.ocean", "com.openwave.files"});
        settings.sync();
        AppController controller;
        if (!QFile::exists("/run/openwave/appd.pid")) QSKIP("Requires appd in the target runtime.");
        QTRY_VERIFY(registryReady(controller));
        QCOMPARE(controller.favoriteApps(), favorites);
        verifyPlacement(controller);
    }
};

QTEST_GUILESS_MAIN(ApplicationLayoutTests)
#include "tst_layout.moc"
