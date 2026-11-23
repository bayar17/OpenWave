#include "SettingsModuleLoader.h"
#include "SettingsModuleRegistry.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

const QString GeneralId = QStringLiteral("com.openwave.settings.general");

}

class GeneralModuleTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_engine.reset(new QQmlEngine);
        m_engine->addImportPath(QStringLiteral(STUB_IMPORT_DIR));
        m_service = createMock(QStringLiteral("MockGeneral.qml"));
        m_prompts = createMock(QStringLiteral("MockPrompts.qml"));
        m_environment = createMock(QStringLiteral("MockEnvironment.qml"));
        QVERIFY(m_service && m_prompts && m_environment);
        m_engine->rootContext()->setContextProperty(QStringLiteral("General"), m_service);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), m_prompts);
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsEnvironment"), m_environment);
        SettingsModuleRegistry::shared()->setModuleRoot(QStringLiteral(MODULES_DIR));

        QQmlComponent component(m_engine.data());
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            SettingsModuleHost { width: 672; height: 900 }
        )", QUrl(QStringLiteral("qrc:/generalhost.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(m_host);
        m_window.reset(new QQuickWindow);
        m_window->resize(700, 960);
        m_host->setParentItem(m_window->contentItem());
        m_window->show();
    }

    void cleanup()
    {
        m_host.reset();
        m_window.reset();
        SettingsModuleRegistry::shared()->setModuleRoot(QString());
        delete m_service;
        delete m_prompts;
        delete m_environment;
        m_service = m_prompts = m_environment = nullptr;
        m_engine.reset();
    }

    void discoversGeneralModuleWithSearchEntries()
    {
        SettingsModuleRegistry *registry = SettingsModuleRegistry::shared();
        const QVariantMap info = registry->module(GeneralId);
        QVERIFY(!info.isEmpty());
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("General"));
        QCOMPARE(info.value(QStringLiteral("order")).toInt(), 30);
        QStringList ids;
        for (const QVariant &entry : registry->modules())
            ids << entry.toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(ids, (QStringList{QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("com.openwave.settings.bluetooth"), GeneralId}));
        QStringList keys;
        for (const QVariant &entry : info.value(QStringLiteral("searchEntries")).toList())
            keys << entry.toMap().value(QStringLiteral("key")).toString();
        QCOMPARE(keys, (QStringList{QStringLiteral("about"), QStringLiteral("name"), QStringLiteral("timezone"),
                                    QStringLiteral("storage"), QStringLiteral("locale"), QStringLiteral("power")}));
    }

    void loadsThroughRealLoaderAndListsFourCategories()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(loader()->item()->objectName(), QStringLiteral("generalModule"));
        QCOMPARE(pageProperty("category").toString(), QString());
        QStringList titles;
        for (const QVariant &entry : pageProperty("categories").toList())
            titles << entry.toMap().value(QStringLiteral("title")).toString();
        QCOMPARE(titles, (QStringList{QStringLiteral("About"), QStringLiteral("Storage"), QStringLiteral("Locale & Languages"), QStringLiteral("Power options")}));
        for (const char *key : {"about", "storage", "locale", "power"})
            QVERIFY(find(QStringLiteral("generalCategory_%1").arg(QLatin1String(key))));
        QVERIFY(!find(QStringLiteral("generalCategory_appearance")));
        QVERIFY(!find(QStringLiteral("settingsAccentPalette")));
    }

    void categoriesOpenTheirDetailPanesAndBackReturns()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QMetaObject::invokeMethod(find(QStringLiteral("generalCategory_about")), "clicked");
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("about"));
        QVERIFY(find(QStringLiteral("generalDeviceName"))->isVisible());
        QCOMPARE(find(QStringLiteral("generalDeviceName"))->property("detail").toString(), QStringLiteral("KB Phone"));
        QCOMPARE(find(QStringLiteral("generalSystem_kernel"))->property("detail").toString(), QStringLiteral("test-kernel"));
        QCOMPARE(find(QStringLiteral("generalSystem_os"))->property("detail").toString(), QStringLiteral("OpenWave"));
        QCOMPARE(find(QStringLiteral("generalSystem_processors"))->property("detail").toString(), QStringLiteral("4"));
        QVERIFY(!find(QStringLiteral("generalTimezone"))->isVisible());
        QCOMPARE(find(QStringLiteral("generalBack"))->property("heading").toString(), QString::fromUtf8("‹ About"));
        QVERIFY(call0("back").toBool());
        QCOMPARE(pageProperty("category").toString(), QString());
        QVERIFY(!call0("back").toBool());

        QMetaObject::invokeMethod(find(QStringLiteral("generalCategory_storage")), "clicked");
        QCOMPARE(find(QStringLiteral("generalStorageInfo"))->property("detail").toString(), QStringLiteral("1.0 GB available of 2.0 GB"));
        QVERIFY(call0("back").toBool());

        QMetaObject::invokeMethod(find(QStringLiteral("generalCategory_locale")), "clicked");
        QVERIFY(find(QStringLiteral("generalTimezone"))->isVisible());
        QCOMPARE(find(QStringLiteral("generalTimezone"))->property("detail").toString(), QStringLiteral("Europe/Istanbul"));
        QMetaObject::invokeMethod(find(QStringLiteral("generalBack")), "clicked");
        QCOMPARE(pageProperty("category").toString(), QString());

        QMetaObject::invokeMethod(find(QStringLiteral("generalCategory_power")), "clicked");
        QVERIFY(find(QStringLiteral("generalRestart"))->isVisible());
        QVERIFY(find(QStringLiteral("generalShutdown"))->isVisible());
        QVERIFY(!find(QStringLiteral("generalPowerNote"))->isVisible());
    }

    void unknownCategoriesAreIgnored()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("appearance"));
        QCOMPARE(pageProperty("category").toString(), QString());
        call1("openCategory", QStringLiteral("../../etc"));
        QCOMPARE(pageProperty("category").toString(), QString());
        call1("openCategory", QStringLiteral("storage"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("storage"));
    }

    void unavailableInformationFallsBack()
    {
        m_service->setProperty("systemInfo", QVariantMap{});
        m_service->setProperty("timeServiceAvailable", false);
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("about"));
        QCOMPARE(find(QStringLiteral("generalSystem_kernel"))->property("detail").toString(), QStringLiteral("Unavailable"));
        QCOMPARE(find(QStringLiteral("generalSystem_processors"))->property("detail").toString(), QStringLiteral("Unavailable"));
        call1("openCategory", QStringLiteral("storage"));
        QCOMPARE(find(QStringLiteral("generalStorageInfo"))->property("detail").toString(), QStringLiteral("Unavailable"));
        call1("openCategory", QStringLiteral("locale"));
        QCOMPARE(find(QStringLiteral("generalTimezone"))->property("detail").toString(), QStringLiteral("Time service unavailable"));
        QVERIFY(!find(QStringLiteral("generalTimezone"))->isEnabled());
    }

    void deviceNameUsesSharedPromptAndService()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("about"));
        QMetaObject::invokeMethod(find(QStringLiteral("generalDeviceName")), "clicked");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("input"));
        QCOMPARE(m_prompts->property("title").toString(), QStringLiteral("Device name"));
        QCOMPARE(promptOptions().value(QStringLiteral("initialText")).toString(), QStringLiteral("KB Phone"));
        QCOMPARE(promptOptions().value(QStringLiteral("maxLength")).toInt(), 64);
        respond(QStringLiteral("cancelled"), QStringLiteral("Other phone"));
        QCOMPARE(m_service->property("deviceName").toString(), QStringLiteral("KB Phone"));
        QCOMPARE(pageProperty("pendingAction").toString(), QString());

        call0("editName");
        respond(QStringLiteral("accepted"), QStringLiteral("My Phone"));
        QCOMPARE(m_service->property("deviceName").toString(), QStringLiteral("My Phone"));
        QCOMPARE(m_service->property("savedNames").toList().size(), 1);
        QCOMPARE(find(QStringLiteral("generalDeviceName"))->property("detail").toString(), QStringLiteral("My Phone"));
        QCOMPARE(pageProperty("pendingAction").toString(), QString());

        call0("editName");
        respond(QStringLiteral("renderer_lost"), QStringLiteral("Ignored"));
        QCOMPARE(m_service->property("savedNames").toList().size(), 1);
    }

    void timezonePickerChangesTimeZoneThroughService()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("locale"));
        QMetaObject::invokeMethod(find(QStringLiteral("generalTimezone")), "clicked");
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("selection"));
        QCOMPARE(m_prompts->property("title").toString(), QStringLiteral("Time zone"));
        QCOMPARE(promptOptions().value(QStringLiteral("initialText")).toString(), QStringLiteral("Europe/Istanbul"));
        QCOMPARE(promptOptions().value(QStringLiteral("choices")).toMap().value(QStringLiteral("America/New_York")).toString(), QStringLiteral("America/New York"));
        respond(QStringLiteral("cancelled"), QStringLiteral("Europe/London"));
        QCOMPARE(m_service->property("timezone").toString(), QStringLiteral("Europe/Istanbul"));

        call0("editTimezone");
        respond(QStringLiteral("accepted"), QStringLiteral("Unknown/Zone"));
        QCOMPARE(m_service->property("timezone").toString(), QStringLiteral("Europe/Istanbul"));

        call0("editTimezone");
        respond(QStringLiteral("accepted"), QStringLiteral("Europe/London"));
        QCOMPARE(m_service->property("timezone").toString(), QStringLiteral("Europe/London"));
        QCOMPARE(find(QStringLiteral("generalTimezone"))->property("detail").toString(), QStringLiteral("Europe/London"));
        QCOMPARE(pageProperty("pendingAction").toString(), QString());
    }

    void timezonePickerRefusesWhenUnavailableBusyOrLoading()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        m_service->setProperty("timeServiceAvailable", false);
        call0("editTimezone");
        QVERIFY(!m_prompts->property("busy").toBool());
        m_service->setProperty("timeServiceAvailable", true);
        m_service->setProperty("timeBusy", true);
        call0("editTimezone");
        QVERIFY(!m_prompts->property("busy").toBool());
        m_service->setProperty("timeBusy", false);
        m_service->setProperty("zones", QStringList{});
        call0("editTimezone");
        QVERIFY(!m_prompts->property("busy").toBool());
        QCOMPARE(m_service->property("refreshCount").toInt(), 1);
        QVERIFY(find(QStringLiteral("generalToastText"))->parentItem()->isVisible());
        QVERIFY(text(find(QStringLiteral("generalToastText"))).contains(QStringLiteral("loading")));
    }

    void powerActionsRequireConfirmationAndIgnoreStaleResults()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("power"));
        for (const QString &action : {QStringLiteral("restart"), QStringLiteral("shutdown")}) {
            QMetaObject::invokeMethod(find(action == QLatin1String("restart") ? QStringLiteral("generalRestart") : QStringLiteral("generalShutdown")), "clicked");
            QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("confirmation"));
            QCOMPARE(promptOptions().value(QStringLiteral("acceptLabel")).toString(), action == QLatin1String("restart") ? QStringLiteral("Restart") : QStringLiteral("Shut down"));
            QCOMPARE(promptOptions().value(QStringLiteral("cancelLabel")).toString(), QStringLiteral("Cancel"));
            QCOMPARE(m_service->property("powerActions").toList().size(), 0);
            respond(QStringLiteral("cancelled"), QString());
            QCOMPARE(m_service->property("powerActions").toList().size(), 0);
        }
        call1("requestPower", QStringLiteral("restart"));
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_service->property("powerActions").toStringList(), (QStringList{QStringLiteral("restart")}));
        call1("requestPower", QStringLiteral("shutdown"));
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_service->property("powerActions").toStringList(), (QStringList{QStringLiteral("restart"), QStringLiteral("shutdown")}));

        call1("requestPower", QStringLiteral("reboot"));
        QVERIFY(!m_prompts->property("busy").toBool());
        call1("requestPower", QStringLiteral("shutdown"));
        QVERIFY(m_prompts->property("busy").toBool());
        m_host->setProperty("moduleId", QString());
        QVERIFY(!m_prompts->property("busy").toBool());
        respond(QStringLiteral("accepted"), QString());
        QCOMPARE(m_service->property("powerActions").toList().size(), 2);
    }

    void powerControlsShowNotesWhenUnavailableOrBusy()
    {
        m_service->setProperty("powerActionsAvailable", false);
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("power"));
        QVERIFY(!find(QStringLiteral("generalRestart"))->isEnabled());
        QVERIFY(!find(QStringLiteral("generalShutdown"))->isEnabled());
        QVERIFY(find(QStringLiteral("generalPowerNote"))->isVisible());
        QCOMPARE(text(find(QStringLiteral("generalPowerNote"))), QStringLiteral("Power controls are unavailable in this session."));
        call1("requestPower", QStringLiteral("restart"));
        QVERIFY(!m_prompts->property("busy").toBool());
        m_service->setProperty("powerBusy", true);
        QCOMPARE(text(find(QStringLiteral("generalPowerNote"))), QString::fromUtf8("Closing applications…"));
        m_service->setProperty("powerActionsAvailable", true);
        m_service->setProperty("powerBusy", false);
        QTRY_VERIFY(find(QStringLiteral("generalRestart"))->isEnabled());
        QVERIFY(!find(QStringLiteral("generalPowerNote"))->isVisible());
    }

    void searchEntriesOpenTheRightCategoryAndPrompts()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openEntry", QStringLiteral("storage"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("storage"));
        call0("back");
        call1("openEntry", QStringLiteral("about"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("about"));
        QVERIFY(!m_prompts->property("busy").toBool());
        call0("back");
        call1("openEntry", QStringLiteral("name"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("about"));
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("input"));
        respond(QStringLiteral("cancelled"), QString());
        call0("back");
        call1("openEntry", QStringLiteral("timezone"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("locale"));
        QCOMPARE(m_prompts->property("kind").toString(), QStringLiteral("selection"));
        respond(QStringLiteral("cancelled"), QString());
        call0("back");
        call1("openEntry", QStringLiteral("power"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("power"));
        call1("openEntry", QStringLiteral("not-a-key"));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("power"));
    }

    void hostForwardsEntryAndBackRequests()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QMetaObject::invokeMethod(m_host.data(), "openEntry", Q_ARG(QVariant, QStringLiteral("storage")));
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("storage"));
        QVariant result;
        QMetaObject::invokeMethod(m_host.data(), "back", Q_RETURN_ARG(QVariant, result));
        QVERIFY(result.toBool());
        QCOMPARE(pageProperty("category").toString(), QString());
    }

    void backCancelsAPendingPromptBeforeLeavingTheCategory()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("about"));
        call0("editName");
        QVERIFY(m_prompts->property("busy").toBool());
        QVERIFY(call0("back").toBool());
        QVERIFY(!m_prompts->property("busy").toBool());
        QCOMPARE(pageProperty("category").toString(), QStringLiteral("about"));
        QVERIFY(call0("back").toBool());
        QCOMPARE(pageProperty("category").toString(), QString());
    }

    void servicesErrorsShowAsToast()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QVERIFY(!find(QStringLiteral("generalToastText"))->parentItem()->isVisible());
        QMetaObject::invokeMethod(m_service, "errorOccurred", Q_ARG(QString, QStringLiteral("Could not change the time zone.")));
        QVERIFY(find(QStringLiteral("generalToastText"))->parentItem()->isVisible());
        QCOMPARE(text(find(QStringLiteral("generalToastText"))), QStringLiteral("Could not change the time zone."));
    }

    void largeTextAndReducedMotionFollowTheEnvironment()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        const qreal normal = find(QStringLiteral("generalCategory_about"))->height();
        m_environment->setProperty("largeText", true);
        QVERIFY(find(QStringLiteral("generalCategory_about"))->height() >= normal);
        QVERIFY(pageProperty("largeText").toBool());
        m_environment->setProperty("reduceMotion", true);
        QVERIFY(pageProperty("reducedMotion").toBool());
        call1("openCategory", QStringLiteral("about"));
        QTRY_COMPARE(find(QStringLiteral("generalDetails"))->x(), 0.0);
    }

    void leavingTheTabResetsTheCategoryAndFreesTheModule()
    {
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QPointer<QQuickItem> page = loader()->item();
        call1("openCategory", QStringLiteral("power"));
        m_host->setProperty("moduleId", QString());
        QVERIFY(!loader()->loaded());
        QTRY_VERIFY(page.isNull());
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        QCOMPARE(pageProperty("category").toString(), QString());
    }

    void toleratesMissingServices()
    {
        m_engine->rootContext()->setContextProperty(QStringLiteral("General"), static_cast<QObject *>(nullptr));
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), static_cast<QObject *>(nullptr));
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsEnvironment"), static_cast<QObject *>(nullptr));
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call1("openCategory", QStringLiteral("about"));
        QVERIFY(!find(QStringLiteral("generalDeviceName"))->isEnabled());
        call1("openCategory", QStringLiteral("power"));
        QVERIFY(!find(QStringLiteral("generalRestart"))->isEnabled());
        call0("editName");
        call0("editTimezone");
        call1("requestPower", QStringLiteral("restart"));
        QVERIFY(loader()->loaded());
    }

    void promptServiceMissingShowsAnError()
    {
        m_engine->rootContext()->setContextProperty(QStringLiteral("Prompts"), static_cast<QObject *>(nullptr));
        QVERIFY2(loadPage(), qPrintable(loader()->errorString()));
        call0("editName");
        QVERIFY(find(QStringLiteral("generalToastText"))->parentItem()->isVisible());
        QVERIFY(text(find(QStringLiteral("generalToastText"))).contains(QStringLiteral("prompt")));
    }

private:
    QObject *createMock(const QString &file)
    {
        QQmlComponent component(m_engine.data(), QUrl::fromLocalFile(QStringLiteral(MOCK_DIR "/") + file));
        if (!component.isReady()) {
            qWarning("%s", qPrintable(component.errorString()));
            return nullptr;
        }
        return component.create();
    }

    bool loadPage()
    {
        m_host->setProperty("moduleId", GeneralId);
        return loader()->loaded();
    }

    SettingsModuleLoader *loader() const
    {
        return m_host->property("loader").value<SettingsModuleLoader *>();
    }

    static QQuickItem *search(QQuickItem *parent, const QString &name)
    {
        if (!parent)
            return nullptr;
        if (parent->objectName() == name)
            return parent;
        for (QQuickItem *child : parent->childItems()) {
            if (QQuickItem *found = search(child, name))
                return found;
        }
        return nullptr;
    }

    QQuickItem *find(const QString &name) const { return search(m_host.data(), name); }
    static QString text(QQuickItem *item) { return item ? item->property("text").toString() : QString(); }
    QVariant pageProperty(const char *name) { return loader()->item()->property(name); }
    QVariantMap promptOptions() const { return m_prompts->property("options").toMap(); }

    QVariant call0(const char *method)
    {
        QVariant result;
        QMetaObject::invokeMethod(loader()->item(), method, Q_RETURN_ARG(QVariant, result));
        return result;
    }

    QVariant call1(const char *method, const QVariant &first)
    {
        QVariant result;
        QMetaObject::invokeMethod(loader()->item(), method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, first));
        return result;
    }

    void respond(const QString &result, const QString &value)
    {
        QMetaObject::invokeMethod(m_prompts, "respond", Q_ARG(QVariant, result), Q_ARG(QVariant, value));
    }

    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QQuickItem> m_host;
    QScopedPointer<QQuickWindow> m_window;
    QObject *m_service = nullptr;
    QObject *m_prompts = nullptr;
    QObject *m_environment = nullptr;
};

QTEST_MAIN(GeneralModuleTest)
#include "tst_generalmodule.moc"
