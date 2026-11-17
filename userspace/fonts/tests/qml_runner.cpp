#include <OpenWave/FontManager.h>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class FontSetup : public QObject
{
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        OpenWave::FontManager::initialize(qEnvironmentVariable("OPENWAVE_FONT_TEST_DIRECTORY"));
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QString::fromUtf8(OPENWAVE_FONT_IMPORT_PATH));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(openwave_fonts, FontSetup)
#include "qml_runner.moc"
