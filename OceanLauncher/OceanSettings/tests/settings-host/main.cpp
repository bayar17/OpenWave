#include "SettingsModuleRegistry.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void provide(const QString &name, QObject *object)
    {
        if (m_engine)
            m_engine->rootContext()->setContextProperty(name, object);
    }

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        m_engine = engine;
        engine->addImportPath(QStringLiteral(STUB_IMPORT_DIR));
        QQmlComponent component(engine);
        component.setData("import QtQuick\nQtObject { property url source: \"\" }\n", QUrl());
        QObject *wallpaper = component.create();
        engine->rootContext()->setContextProperty(QStringLiteral("Wallpaper"), wallpaper);
        engine->rootContext()->setContextProperty(QStringLiteral("TestHooks"), this);
        SettingsModuleRegistry::shared()->setModuleRoot(QStringLiteral(MODULES_DIR));
    }

private:
    QQmlEngine *m_engine = nullptr;
};

QUICK_TEST_MAIN_WITH_SETUP(OceanSettingsHost, Setup)
#include "main.moc"
