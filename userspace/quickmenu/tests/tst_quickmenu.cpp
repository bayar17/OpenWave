#include "QuickMenuAppearance.h"
#include "QuickMenuAppletRegistry.h"
#include "QuickMenuSystem.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

bool writeApplet(const QString &root, const QString &id, const QMap<QString, QString> &fields = {},
                 bool withMetadata = true)
{
    const QString base = root + QLatin1Char('/') + id;
    if (!writeFile(base + QStringLiteral("/MainApplet.qml"), "import QtQuick\nItem {}\n"))
        return false;
    QString text = QStringLiteral("[Desktop Entry]\nType=X-OpenWave-Applet\n");
    for (auto field = fields.constBegin(); field != fields.constEnd(); ++field)
        text += field.key() + QLatin1Char('=') + field.value() + QLatin1Char('\n');
    return !withMetadata || writeFile(base + QStringLiteral("/applet.desktop"), text.toUtf8());
}

QStringList ids(const QuickMenuAppletRegistry &registry)
{
    QStringList result;
    for (int row = 0; row < registry.rowCount(); ++row)
        result << registry.data(registry.index(row), QuickMenuAppletRegistry::IdRole).toString();
    return result;
}

QStringList panelIds(const QuickMenuAppletRegistry &registry)
{
    QStringList result;
    for (const QVariant &item : registry.panelApplets())
        result << item.toMap().value(QStringLiteral("id")).toString();
    return result;
}

QVariant role(const QuickMenuAppletRegistry &registry, const QString &id, int roleId)
{
    for (int row = 0; row < registry.rowCount(); ++row) {
        if (registry.data(registry.index(row), QuickMenuAppletRegistry::IdRole).toString() == id)
            return registry.data(registry.index(row), roleId);
    }
    return QVariant();
}

QString firstRejection(const QuickMenuAppletRegistry &registry)
{
    const QVariantList rejected = registry.rejections();
    return rejected.isEmpty() ? QString() : rejected.first().toMap().value(QStringLiteral("reason")).toString();
}

QMap<QString, QString> metadata(const QString &name, int order)
{
    return {{QStringLiteral("Name"), name}, {QStringLiteral("X-OpenWave-Applet-Order"), QString::number(order)}};
}

class MockBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wifiAvailable MEMBER wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled MEMBER wifiEnabled NOTIFY changed)
    Q_PROPERTY(bool wifiConnected MEMBER wifiConnected NOTIFY changed)
    Q_PROPERTY(int wifiStrength MEMBER wifiStrength NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable MEMBER bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothEnabled MEMBER bluetoothEnabled NOTIFY changed)
    Q_PROPERTY(bool airplaneMode MEMBER airplaneMode NOTIFY changed)
    Q_PROPERTY(bool cellularAvailable MEMBER cellularAvailable NOTIFY changed)
    Q_PROPERTY(int cellularStrength MEMBER cellularStrength NOTIFY changed)
    Q_PROPERTY(QString modemLabel MEMBER modemLabel NOTIFY changed)
    Q_PROPERTY(int volume MEMBER volume NOTIFY changed)
    Q_PROPERTY(int brightness MEMBER brightness NOTIFY changed)

public:
    bool wifiAvailable = true;
    bool wifiEnabled = false;
    bool wifiConnected = false;
    int wifiStrength = 0;
    bool bluetoothAvailable = true;
    bool bluetoothEnabled = false;
    bool airplaneMode = false;
    bool cellularAvailable = false;
    int cellularStrength = 0;
    QString modemLabel = QStringLiteral("NO MODEM");
    int volume = 40;
    int brightness = 60;
    QStringList calls;

    Q_INVOKABLE void setWifiEnabled(bool enabled)
    {
        calls << QStringLiteral("wifi:%1").arg(enabled);
        wifiEnabled = enabled;
        emit changed();
    }
    Q_INVOKABLE void setBluetoothEnabled(bool enabled)
    {
        calls << QStringLiteral("bluetooth:%1").arg(enabled);
        bluetoothEnabled = enabled;
        emit changed();
    }
    Q_INVOKABLE void setAirplaneMode(bool enabled)
    {
        calls << QStringLiteral("airplane:%1").arg(enabled);
        airplaneMode = enabled;
        emit changed();
    }
    Q_INVOKABLE void setVolume(int value)
    {
        calls << QStringLiteral("volume:%1").arg(value);
        volume = value;
        emit changed();
    }
    Q_INVOKABLE void setBrightness(int value)
    {
        calls << QStringLiteral("brightness:%1").arg(value);
        brightness = value;
        emit changed();
    }

signals:
    void changed();
    void operationFailed(const QString &message);
};

class BareBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable CONSTANT)

public:
    bool wifiAvailable() const { return true; }
};

}

class QuickMenuTests : public QObject
{
    Q_OBJECT

private slots:
    void registryDiscoversEveryRootWithItsKind();
    void registryIgnoresDirectoriesWithoutAnEntryPoint();
    void registryDefaultsEnableSystemAppletsAndDisableUserApplets();
    void registryRejectsDuplicateIdsAcrossRoots();
    void registryRejectsUnsafeAndInvalidApplets();
    void registryReadsOptionalMetadata();
    void registryRejectsIncompatibleApiVersions();
    void registryHonoursSettingsOnlyForSystemApplets();
    void registryOrdersByKindThenMetadataThenSavedOrder();
    void registryMovesAndPersistsOrder();
    void registryEnablesAndPersistsState();
    void registryResetRestoresDefaults();
    void registryKeepsRequiredAppletsResidentWhenHidden();
    void registryRevalidatesBeforeLoading();
    void registryFlagsFailedAppletsUntilRescan();
    void registryPicksUpNewAppletsAndExternalPreferenceChanges();
    void registryToleratesMissingRoots();
    void registryRoutesStateWritesThroughTheWriter();
    void registryAcceptsTheShippedApplets();

    void appearanceLoadsPresetsInOrder();
    void appearanceReadsDesktopEntrySyntax();
    void appearanceUsesLocalizedNames();
    void appearancePersistsTheSelection();
    void appearanceFavoritesComeFirstAndPersist();
    void appearanceFavoritesGoThroughTheWriter();
    void appearanceRejectsInvalidPresets();
    void appearanceGuaranteesTextContrast();
    void appearanceFollowsExternalSelection();
    void appearancePreviewColorsMatchTheAppliedColors();
    void appearanceUsesTheWallpaperAsThumbnailWithoutAPreview();
    void appearanceFlagsOnlyLaterDirectoriesAsUserAppearances();
    void appearanceRefreshPicksUpChangesWithoutWatchers();
    void appearanceDefaultsReadTheSystemFolderAndTheBrokerStore();
    void appearanceReadsTheDaemonsCurrentMarker();
    void appearanceSelectionWorksWhenPreferencesAreNotWritable();
    void appearanceShipsEmpty();
    void appearanceAcceptsTheExampleDesktopFiles();

    void systemReportsUnavailableWithoutBackend();
    void systemForwardsStateAndRequests();
    void systemRefusesUnavailableHardware();
    void systemNeverPretendsToControlMobileData();
    void systemRejectsBackendsWithoutTheMethod();
    void systemReadsBattery();
    void systemEmitsLockRequest();
};

void QuickMenuTests::registryDiscoversEveryRootWithItsKind()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString needed = directory.filePath(QStringLiteral("system/Needed"));
    const QString optional = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writeApplet(needed, QStringLiteral("WiFi"), metadata(QStringLiteral("Wi-Fi"), 10)));
    QVERIFY(writeApplet(optional, QStringLiteral("Torch"), metadata(QStringLiteral("Torch"), 10)));
    QVERIFY(writeApplet(user, QStringLiteral("Mine"), metadata(QStringLiteral("Mine"), 10)));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::Needed, needed},
                                      {QuickMenuAppletRegistry::System, optional},
                                      {QuickMenuAppletRegistry::User, user}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(ids(registry), (QStringList{QStringLiteral("WiFi"), QStringLiteral("Torch"), QStringLiteral("Mine")}));
    QCOMPARE(role(registry, QStringLiteral("WiFi"), QuickMenuAppletRegistry::KindRole).toString(), QStringLiteral("needed"));
    QCOMPARE(role(registry, QStringLiteral("Torch"), QuickMenuAppletRegistry::KindRole).toString(), QStringLiteral("system"));
    QCOMPARE(role(registry, QStringLiteral("Mine"), QuickMenuAppletRegistry::KindRole).toString(), QStringLiteral("user"));
    QVERIFY(role(registry, QStringLiteral("WiFi"), QuickMenuAppletRegistry::RequiredRole).toBool());
    QVERIFY(!role(registry, QStringLiteral("Torch"), QuickMenuAppletRegistry::RequiredRole).toBool());
    QVERIFY(role(registry, QStringLiteral("WiFi"), QuickMenuAppletRegistry::EntryUrlRole).toUrl().isLocalFile());
}

void QuickMenuTests::registryIgnoresDirectoriesWithoutAnEntryPoint()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("Good")));
    QVERIFY(writeFile(root + QStringLiteral("/Empty/readme.txt"), "x"));
    QVERIFY(writeFile(root + QStringLiteral("/Other/Entry.qml"), "import QtQuick\nItem {}\n"));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/Needed")));
    QVERIFY(writeFile(root + QStringLiteral("/loose.qml"), "x"));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(ids(registry), QStringList{QStringLiteral("Good")});
    QVERIFY(registry.rejections().isEmpty());
}

void QuickMenuTests::registryDefaultsEnableSystemAppletsAndDisableUserApplets()
{
    QTemporaryDir directory;
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writeApplet(system, QStringLiteral("Torch")));
    QVERIFY(writeApplet(user, QStringLiteral("Mine")));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, system},
                                      {QuickMenuAppletRegistry::User, user}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(role(registry, QStringLiteral("Torch"), QuickMenuAppletRegistry::EnabledRole).toBool());
    QVERIFY(!role(registry, QStringLiteral("Mine"), QuickMenuAppletRegistry::EnabledRole).toBool());
    QCOMPARE(panelIds(registry), QStringList{QStringLiteral("Torch")});
}

void QuickMenuTests::registryRejectsDuplicateIdsAcrossRoots()
{
    QTemporaryDir directory;
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writeApplet(system, QStringLiteral("Torch"), metadata(QStringLiteral("System torch"), 1)));
    QVERIFY(writeApplet(user, QStringLiteral("Torch"), metadata(QStringLiteral("Impostor"), 1)));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, system},
                                      {QuickMenuAppletRegistry::User, user}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(registry.count(), 1);
    QCOMPARE(role(registry, QStringLiteral("Torch"), QuickMenuAppletRegistry::NameRole).toString(),
             QStringLiteral("System torch"));
    QCOMPARE(registry.rejections().size(), 1);
    QVERIFY(registry.rejections().first().toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("already provided")));
}

void QuickMenuTests::registryRejectsUnsafeAndInvalidApplets()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    const QString outside = directory.filePath(QStringLiteral("outside"));
    QVERIFY(writeApplet(root, QStringLiteral("Good")));
    QVERIFY(writeApplet(outside, QStringLiteral("Elsewhere")));
    QVERIFY(QFile::link(outside + QStringLiteral("/Elsewhere"), root + QStringLiteral("/Linked")));
    QVERIFY(writeApplet(root, QStringLiteral("LinkedEntry"), {}, false));
    QVERIFY(QFile::remove(root + QStringLiteral("/LinkedEntry/MainApplet.qml")));
    QVERIFY(QFile::link(outside + QStringLiteral("/Elsewhere/MainApplet.qml"),
                        root + QStringLiteral("/LinkedEntry/MainApplet.qml")));
    QVERIFY(writeApplet(root, QStringLiteral("9bad"), {}, false));
    QVERIFY(writeApplet(root, QStringLiteral("has-dash"), {}, false));
    QVERIFY(writeApplet(root, QStringLiteral("NoGroup"), {}, false));
    QVERIFY(writeFile(root + QStringLiteral("/NoGroup/applet.desktop"), "Name=Nothing\nType=X-OpenWave-Applet\n"));
    QVERIFY(writeApplet(root, QStringLiteral("WrongType"), {}, false));
    QVERIFY(writeFile(root + QStringLiteral("/WrongType/applet.desktop"), "[Desktop Entry]\nType=Application\nName=Wrong\n"));
    QVERIFY(writeApplet(root, QStringLiteral("Huge"), {}, false));
    QVERIFY(writeFile(root + QStringLiteral("/Huge/MainApplet.qml"), QByteArray(2 * 1024 * 1024, 'x')));
    QVERIFY(writeApplet(root, QStringLiteral("LongName"), {{QStringLiteral("Name"), QString(80, QLatin1Char('x'))}}));
    QVERIFY(writeApplet(root, QStringLiteral("EscapingIcon"), {{QStringLiteral("Icon"), QStringLiteral("../x.svg")}}));
    QVERIFY(writeApplet(root, QStringLiteral("AbsoluteIcon"), {{QStringLiteral("Icon"), QStringLiteral("/etc/passwd")}}));
    QVERIFY(writeApplet(root, QStringLiteral("MissingIcon"), {{QStringLiteral("Icon"), QStringLiteral("none.svg")}}));
    QVERIFY(writeApplet(root, QStringLiteral("WrongIconType"), {{QStringLiteral("Icon"), QStringLiteral("MainApplet.qml")}}));
    QVERIFY(writeApplet(root, QStringLiteral("BadOrder"), {{QStringLiteral("X-OpenWave-Applet-Order"), QStringLiteral("soon")}}));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(ids(registry), QStringList{QStringLiteral("Good")});
    QCOMPARE(registry.rejections().size(), 13);
}

void QuickMenuTests::registryReadsOptionalMetadata()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeFile(root + QStringLiteral("/Fancy/MainApplet.qml"), "import QtQuick\nItem {}\n"));
    QVERIFY(writeFile(root + QStringLiteral("/Fancy/assets/glyph.svg"), "<svg/>"));
    QVERIFY(writeFile(root + QStringLiteral("/Fancy/applet.desktop"),
                      "[Desktop Entry]\nType=X-OpenWave-Applet\nName=Fancy Thing\nComment=Does things\nIcon=assets/glyph.svg\n"
                      "X-OpenWave-Applet-Order=7\nX-OpenWave-Applet-ApiVersion=1\n"));
    QVERIFY(writeApplet(root, QStringLiteral("Plain"), {}, false));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(role(registry, QStringLiteral("Fancy"), QuickMenuAppletRegistry::NameRole).toString(), QStringLiteral("Fancy Thing"));
    QCOMPARE(role(registry, QStringLiteral("Fancy"), QuickMenuAppletRegistry::DescriptionRole).toString(), QStringLiteral("Does things"));
    QCOMPARE(role(registry, QStringLiteral("Fancy"), QuickMenuAppletRegistry::OrderRole).toInt(), 7);
    QVERIFY(role(registry, QStringLiteral("Fancy"), QuickMenuAppletRegistry::IconRole).toUrl().toLocalFile().endsWith(QStringLiteral("/Fancy/assets/glyph.svg")));
    QCOMPARE(role(registry, QStringLiteral("Plain"), QuickMenuAppletRegistry::NameRole).toString(), QStringLiteral("Plain"));
    QCOMPARE(role(registry, QStringLiteral("Plain"), QuickMenuAppletRegistry::OrderRole).toInt(), QuickMenuAppletRegistry::DefaultOrder);
    QVERIFY(role(registry, QStringLiteral("Plain"), QuickMenuAppletRegistry::IconRole).toUrl().isEmpty());
}

void QuickMenuTests::registryRejectsIncompatibleApiVersions()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("Future"), {{QStringLiteral("X-OpenWave-Applet-ApiVersion"), QString::number(QuickMenuAppletRegistry::SupportedApiVersion + 1)}}));
    QVERIFY(writeApplet(root, QStringLiteral("Zero"), {{QStringLiteral("X-OpenWave-Applet-ApiVersion"), QStringLiteral("0")}}));
    QVERIFY(writeApplet(root, QStringLiteral("Current"), {{QStringLiteral("X-OpenWave-Applet-ApiVersion"), QString::number(QuickMenuAppletRegistry::SupportedApiVersion)}}));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(ids(registry), QStringList{QStringLiteral("Current")});
    bool incompatible = false;
    for (const QVariant &item : registry.rejections())
        incompatible = incompatible || item.toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("incompatible"));
    QVERIFY(incompatible);
}

void QuickMenuTests::registryHonoursSettingsOnlyForSystemApplets()
{
    QTemporaryDir directory;
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    for (const QString &root : {system, user}) {
        const QString id = root == system ? QStringLiteral("Sys") : QStringLiteral("Usr");
        QVERIFY(writeFile(root + QLatin1Char('/') + id + QStringLiteral("/MainApplet.qml"), "import QtQuick\nItem {}\n"));
        QVERIFY(writeFile(root + QLatin1Char('/') + id + QStringLiteral("/Config.qml"), "import QtQuick\nItem {}\n"));
        QVERIFY(writeFile(root + QLatin1Char('/') + id + QStringLiteral("/applet.desktop"), "[Desktop Entry]\nType=X-OpenWave-Applet\nX-OpenWave-Applet-Settings=Config.qml\n"));
    }
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, system},
                                      {QuickMenuAppletRegistry::User, user}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(registry.settingsUrl(QStringLiteral("Sys")).isLocalFile());
    QVERIFY(registry.settingsUrl(QStringLiteral("Usr")).isEmpty());
    QVERIFY(role(registry, QStringLiteral("Sys"), QuickMenuAppletRegistry::SettingsUrlRole).toUrl().isLocalFile());
}

void QuickMenuTests::registryOrdersByKindThenMetadataThenSavedOrder()
{
    QTemporaryDir directory;
    const QString needed = directory.filePath(QStringLiteral("needed"));
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writeApplet(user, QStringLiteral("UserA"), metadata(QStringLiteral("A"), 1)));
    QVERIFY(writeApplet(system, QStringLiteral("SysB"), metadata(QStringLiteral("B"), 20)));
    QVERIFY(writeApplet(system, QStringLiteral("SysA"), metadata(QStringLiteral("A"), 10)));
    QVERIFY(writeApplet(needed, QStringLiteral("NeedZ"), metadata(QStringLiteral("Z"), 99)));
    const QList<QuickMenuAppletRegistry::Root> roots{{QuickMenuAppletRegistry::Needed, needed},
                                                      {QuickMenuAppletRegistry::System, system},
                                                      {QuickMenuAppletRegistry::User, user}};
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    {
        QuickMenuAppletRegistry registry(roots, prefs);
        QCOMPARE(ids(registry), (QStringList{QStringLiteral("NeedZ"), QStringLiteral("SysA"), QStringLiteral("SysB"), QStringLiteral("UserA")}));
    }
    {
        QSettings settings(prefs, QSettings::IniFormat);
        settings.setValue(QStringLiteral("quickmenu/applets/order"),
                          QStringList{QStringLiteral("SysB"), QStringLiteral("Unknown"), QStringLiteral("NeedZ")});
        settings.sync();
        QuickMenuAppletRegistry registry(roots, prefs);
        QCOMPARE(ids(registry), (QStringList{QStringLiteral("SysB"), QStringLiteral("NeedZ"), QStringLiteral("SysA"), QStringLiteral("UserA")}));
    }
}

void QuickMenuTests::registryMovesAndPersistsOrder()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    for (int index = 0; index < 4; ++index)
        QVERIFY(writeApplet(root, QStringLiteral("A%1").arg(index), metadata(QStringLiteral("A%1").arg(index), index)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}}, prefs);
    QSignalSpy changed(&registry, &QuickMenuAppletRegistry::changed);
    QVERIFY(registry.move(QStringLiteral("A3"), 0));
    QCOMPARE(ids(registry), (QStringList{QStringLiteral("A3"), QStringLiteral("A0"), QStringLiteral("A1"), QStringLiteral("A2")}));
    QVERIFY(changed.count() >= 1);
    QVERIFY(registry.move(QStringLiteral("A3"), 99));
    QCOMPARE(ids(registry).last(), QStringLiteral("A3"));
    QVERIFY(!registry.move(QStringLiteral("Nope"), 0));
    QuickMenuAppletRegistry reopened({{QuickMenuAppletRegistry::System, root}}, prefs);
    QCOMPARE(ids(reopened), ids(registry));
}

void QuickMenuTests::registryEnablesAndPersistsState()
{
    QTemporaryDir directory;
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writeApplet(system, QStringLiteral("Torch")));
    QVERIFY(writeApplet(user, QStringLiteral("Mine")));
    const QList<QuickMenuAppletRegistry::Root> roots{{QuickMenuAppletRegistry::System, system},
                                                      {QuickMenuAppletRegistry::User, user}};
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppletRegistry registry(roots, prefs);
    QVERIFY(registry.setEnabled(QStringLiteral("Mine"), true));
    QVERIFY(registry.setEnabled(QStringLiteral("Torch"), false));
    QVERIFY(!registry.setEnabled(QStringLiteral("Nope"), true));
    QCOMPARE(panelIds(registry), QStringList{QStringLiteral("Mine")});
    QuickMenuAppletRegistry reopened(roots, prefs);
    QCOMPARE(panelIds(reopened), QStringList{QStringLiteral("Mine")});
}

void QuickMenuTests::registryResetRestoresDefaults()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("A"), metadata(QStringLiteral("A"), 1)));
    QVERIFY(writeApplet(root, QStringLiteral("B"), metadata(QStringLiteral("B"), 2)));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(registry.move(QStringLiteral("B"), 0));
    QVERIFY(registry.setEnabled(QStringLiteral("A"), false));
    QVERIFY(registry.resetState());
    QCOMPARE(panelIds(registry), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
}

void QuickMenuTests::registryKeepsRequiredAppletsResidentWhenHidden()
{
    QTemporaryDir directory;
    const QString needed = directory.filePath(QStringLiteral("needed"));
    const QString system = directory.filePath(QStringLiteral("system"));
    QVERIFY(writeApplet(needed, QStringLiteral("WiFi")));
    QVERIFY(writeApplet(system, QStringLiteral("Torch")));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::Needed, needed},
                                      {QuickMenuAppletRegistry::System, system}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(registry.setEnabled(QStringLiteral("WiFi"), false));
    QVERIFY(registry.setEnabled(QStringLiteral("Torch"), false));
    QVERIFY(panelIds(registry).isEmpty());
    QCOMPARE(registry.residentApplets().size(), 1);
    QCOMPARE(registry.residentApplets().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("WiFi"));
    QVERIFY(registry.setEnabled(QStringLiteral("WiFi"), true));
    QVERIFY(registry.residentApplets().isEmpty());
}

void QuickMenuTests::registryRevalidatesBeforeLoading()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("Good")));
    QVERIFY(writeApplet(root, QStringLiteral("Vanishing")));
    QVERIFY(writeApplet(root, QStringLiteral("Swapped")));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(registry.entryUrl(QStringLiteral("Good")).isLocalFile());
    QVERIFY(registry.entryUrl(QStringLiteral("Unknown")).isEmpty());
    QVERIFY(QFile::remove(root + QStringLiteral("/Vanishing/MainApplet.qml")));
    QSignalSpy failed(&registry, &QuickMenuAppletRegistry::loadFailed);
    QVERIFY(registry.entryUrl(QStringLiteral("Vanishing")).isEmpty());
    QCOMPARE(failed.count(), 1);
    QVERIFY(QFile::remove(root + QStringLiteral("/Swapped/MainApplet.qml")));
    QVERIFY(QFile::link(directory.filePath(QStringLiteral("elsewhere.qml")), root + QStringLiteral("/Swapped/MainApplet.qml")));
    QVERIFY(registry.entryUrl(QStringLiteral("Swapped")).isEmpty());
    QCOMPARE(failed.count(), 2);
}

void QuickMenuTests::registryFlagsFailedAppletsUntilRescan()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("A")));
    QVERIFY(writeApplet(root, QStringLiteral("B")));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    registry.reportLoadFailure(QStringLiteral("A"), QStringLiteral("boom"));
    QCOMPARE(panelIds(registry), QStringList{QStringLiteral("B")});
    QCOMPARE(role(registry, QStringLiteral("A"), QuickMenuAppletRegistry::LoadErrorRole).toString(), QStringLiteral("boom"));
    QVERIFY(registry.setEnabled(QStringLiteral("B"), true));
    QCOMPARE(panelIds(registry), QStringList{QStringLiteral("B")});
    registry.reload();
    QCOMPARE(panelIds(registry), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
}

void QuickMenuTests::registryPicksUpNewAppletsAndExternalPreferenceChanges()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    QVERIFY(writeApplet(root, QStringLiteral("A")));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}}, prefs);
    QCOMPARE(registry.count(), 1);
    QVERIFY(writeApplet(root, QStringLiteral("B")));
    QTRY_COMPARE_WITH_TIMEOUT(registry.count(), 2, 5000);
    {
        QSettings settings(prefs, QSettings::IniFormat);
        settings.setValue(QStringLiteral("quickmenu/applets/state/B"), false);
        settings.sync();
    }
    QTRY_COMPARE_WITH_TIMEOUT(panelIds(registry), QStringList{QStringLiteral("A")}, 5000);
}

void QuickMenuTests::registryRoutesStateWritesThroughTheWriter()
{
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("root"));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QVERIFY(writeApplet(root, QStringLiteral("A"), metadata(QStringLiteral("A"), 1)));
    QVERIFY(writeApplet(root, QStringLiteral("B"), metadata(QStringLiteral("B"), 2)));
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::System, root}}, prefs);
    QList<QPair<QString, QVariant>> received;
    bool accept = true;
    registry.setPreferenceWriter([&](const QString &key, const QVariant &value) {
        received.append({key, value});
        if (!accept)
            return false;
        QSettings settings(prefs, QSettings::IniFormat);
        if (value.isValid())
            settings.setValue(key, value);
        else
            settings.remove(key);
        settings.sync();
        return true;
    });
    QVERIFY(registry.setEnabled(QStringLiteral("A"), false));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.last().first, QStringLiteral("quickmenu/applets/state/A"));
    QCOMPARE(received.last().second.metaType().id(), int(QMetaType::Bool));
    QVERIFY(!received.last().second.toBool());
    QCOMPARE(panelIds(registry), QStringList{QStringLiteral("B")});
    QVERIFY(registry.move(QStringLiteral("B"), 0));
    QCOMPARE(received.last().first, QStringLiteral("quickmenu/applets/order"));
    QCOMPARE(received.last().second.toStringList(), (QStringList{QStringLiteral("B"), QStringLiteral("A")}));
    QVERIFY(registry.resetState());
    QCOMPARE(received.last().first, QStringLiteral("quickmenu/applets"));
    QVERIFY(!received.last().second.isValid());
    QCOMPARE(panelIds(registry), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    accept = false;
    const int before = received.size();
    QVERIFY(!registry.setEnabled(QStringLiteral("B"), false));
    QVERIFY(!registry.move(QStringLiteral("B"), 0));
    QVERIFY(!registry.resetState());
    QCOMPARE(received.size(), before + 3);
    QCOMPARE(panelIds(registry), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QVERIFY(!registry.setEnabled(QStringLiteral("Nope"), true));
    QCOMPARE(received.size(), before + 3);
}

void QuickMenuTests::registryToleratesMissingRoots()
{
    QTemporaryDir directory;
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::Needed, directory.filePath(QStringLiteral("none"))},
                                      {QuickMenuAppletRegistry::User, QString()}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(registry.count(), 0);
    QVERIFY(registry.panelApplets().isEmpty());
    QVERIFY(!registry.setEnabled(QStringLiteral("X"), true));
}

void QuickMenuTests::registryAcceptsTheShippedApplets()
{
    QTemporaryDir directory;
    const QString base = QStringLiteral(QUICKMENU_SOURCE_DIR "/applets");
    QuickMenuAppletRegistry registry({{QuickMenuAppletRegistry::Needed, base + QStringLiteral("/Needed")},
                                      {QuickMenuAppletRegistry::System, base}},
                                     directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(ids(registry), (QStringList{QStringLiteral("WiFi"), QStringLiteral("Bluetooth"), QStringLiteral("MobileData"),
                                         QStringLiteral("FlightMode"), QStringLiteral("Lock")}));
    QVERIFY2(registry.rejections().isEmpty(), qPrintable(firstRejection(registry)));
    for (const QString &id : {QStringLiteral("WiFi"), QStringLiteral("Bluetooth"), QStringLiteral("MobileData"), QStringLiteral("FlightMode")})
        QVERIFY(role(registry, id, QuickMenuAppletRegistry::RequiredRole).toBool());
    QVERIFY(!role(registry, QStringLiteral("Lock"), QuickMenuAppletRegistry::RequiredRole).toBool());
    for (const QString &id : ids(registry))
        QVERIFY(role(registry, id, QuickMenuAppletRegistry::IconRole).toUrl().isLocalFile());
}

namespace {

bool writePreset(const QString &directory, const QString &file, const QMap<QString, QString> &fields,
                 const QString &type = QStringLiteral("X-OpenWave-Appearance"))
{
    QString text = QStringLiteral("[Desktop Entry]\nType=%1\n").arg(type);
    for (auto field = fields.constBegin(); field != fields.constEnd(); ++field)
        text += field.key() + QLatin1Char('=') + field.value() + QLatin1Char('\n');
    return writeFile(directory + QLatin1Char('/') + file, text.toUtf8());
}

QMap<QString, QString> preset(const QString &name, int order)
{
    return {{QStringLiteral("Name"), name}, {QStringLiteral("X-OpenWave-Appearance-Order"), QString::number(order)},
            {QStringLiteral("X-OpenWave-Appearance-Hue"), QStringLiteral("0.6")}};
}

}

void QuickMenuTests::appearanceLoadsPresetsInOrder()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 20)));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 30)));
    QVERIFY(writePreset(presets, QStringLiteral("gamma.desktop"), preset(QStringLiteral("Gamma"), 10)));
    QVERIFY(writeFile(presets + QStringLiteral("/notes.txt"), "ignored"));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    QStringList order;
    for (const QVariant &item : appearance.presets())
        order << item.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(order, (QStringList{QStringLiteral("gamma"), QStringLiteral("beta"), QStringLiteral("alpha")}));
    QCOMPARE(appearance.current(), QStringLiteral("gamma"));
    QCOMPARE(appearance.name(), QStringLiteral("Gamma"));
}

void QuickMenuTests::appearanceReadsDesktopEntrySyntax()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writeFile(presets + QStringLiteral("/winter.desktop"),
                      "# a comment\n"
                      "[Other Group]\nName=Wrong\n\n"
                      "[Desktop Entry]\n"
                      "Type=X-OpenWave-Appearance\n"
                      "Name=Winter\\sSnow\n"
                      "Name[xx]=Localized Winter\n"
                      "Name[xx_YY]=Regional Winter\n"
                      "Comment=Seasonal\n"
                      "X-OpenWave-Appearance-Hue = 0,55\n"
                      "X-OpenWave-Appearance-ScrimColor=#102030\n"
                      "X-OpenWave-Appearance-Preview=snow.png\n"
                      "X-OpenWave-Appearance-Wallpaper=snow-wallpaper.png\n"
                      "Hue=0.1\n"));
    QVERIFY(writeFile(presets + QStringLiteral("/snow.png"), "png"));
    QVERIFY(writeFile(presets + QStringLiteral("/snow-wallpaper.png"), "wallpaper"));
    QLocale::setDefault(QLocale(QStringLiteral("en_US")));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(appearance.presets().size(), 1);
    QCOMPARE(appearance.name(), QStringLiteral("Winter Snow"));
    QVERIFY(appearance.preview().toLocalFile().endsWith(QStringLiteral("/snow.png")));
    QCOMPARE(appearance.presets().first().toMap().value(QStringLiteral("preview")).toUrl(), appearance.preview());
    QVERIFY(appearance.wallpaper().toLocalFile().endsWith(QStringLiteral("/snow-wallpaper.png")));
    QCOMPARE(appearance.presets().first().toMap().value(QStringLiteral("wallpaper")).toUrl(), appearance.wallpaper());
}

void QuickMenuTests::appearanceUsesLocalizedNames()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QMap<QString, QString> fields = preset(QStringLiteral("Winter"), 1);
    fields.insert(QStringLiteral("Name[xx]"), QStringLiteral("Language Winter"));
    fields.insert(QStringLiteral("Name[xx_YY]"), QStringLiteral("Regional Winter"));
    QVERIFY(writePreset(presets, QStringLiteral("winter.desktop"), fields));
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QStringLiteral("xx_YY")));
    {
        QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
        if (QLocale().name() == QLatin1String("xx_YY"))
            QCOMPARE(appearance.name(), QStringLiteral("Regional Winter"));
    }
    QLocale::setDefault(QLocale(QStringLiteral("xx_ZZ")));
    {
        QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
        if (QLocale().name() == QLatin1String("xx_ZZ"))
            QCOMPARE(appearance.name(), QStringLiteral("Language Winter"));
    }
    QLocale::setDefault(QLocale(QStringLiteral("en_US")));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(appearance.name(), QStringLiteral("Winter"));
    QLocale::setDefault(previous);
}

void QuickMenuTests::appearancePersistsTheSelection()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppearance appearance({presets}, prefs);
    QSignalSpy changed(&appearance, &QuickMenuAppearance::changed);
    QVERIFY(appearance.select(QStringLiteral("beta")));
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
    QCOMPARE(changed.count(), 1);
    QSignalSpy listChanged(&appearance, &QuickMenuAppearance::presetsChanged);
    QVERIFY(appearance.select(QStringLiteral("alpha")));
    QCOMPARE(listChanged.count(), 0);
    QVERIFY(appearance.select(QStringLiteral("beta")));
    QVERIFY(!appearance.select(QStringLiteral("missing")));
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
    QuickMenuAppearance reopened({presets}, prefs);
    QCOMPARE(reopened.current(), QStringLiteral("beta"));
}

void QuickMenuTests::appearanceFavoritesComeFirstAndPersist()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    QVERIFY(writePreset(presets, QStringLiteral("gamma.desktop"), preset(QStringLiteral("Gamma"), 3)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppearance appearance({presets}, prefs);
    const auto order = [&appearance] {
        QStringList ids;
        for (const QVariant &item : appearance.presets())
            ids.append(item.toMap().value(QStringLiteral("id")).toString());
        return ids;
    };
    const auto favoriteFlags = [&appearance] {
        QStringList ids;
        for (const QVariant &item : appearance.presets())
            if (item.toMap().value(QStringLiteral("favorite")).toBool())
                ids.append(item.toMap().value(QStringLiteral("id")).toString());
        return ids;
    };
    QCOMPARE(order(), (QStringList{QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));
    QVERIFY(favoriteFlags().isEmpty());
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
    QSignalSpy listChanged(&appearance, &QuickMenuAppearance::presetsChanged);
    QVERIFY(appearance.setFavorite(QStringLiteral("gamma"), true));
    QCOMPARE(listChanged.count(), 1);
    QCOMPARE(order(), (QStringList{QStringLiteral("gamma"), QStringLiteral("alpha"), QStringLiteral("beta")}));
    QCOMPARE(favoriteFlags(), QStringList{QStringLiteral("gamma")});
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
    QVERIFY(appearance.setFavorite(QStringLiteral("beta"), true));
    QCOMPARE(order().mid(0, 2), (QStringList{QStringLiteral("beta"), QStringLiteral("gamma")}));
    QVERIFY(appearance.setFavorite(QStringLiteral("beta"), true));
    QCOMPARE(appearance.favorites().size(), 2);
    QVERIFY(!appearance.setFavorite(QStringLiteral("missing"), true));
    QuickMenuAppearance reopened({presets}, prefs);
    QCOMPARE(reopened.favorites().size(), 2);
    QVERIFY(appearance.setFavorite(QStringLiteral("gamma"), false));
    QVERIFY(appearance.setFavorite(QStringLiteral("beta"), false));
    QCOMPARE(order(), (QStringList{QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));
    QVERIFY(appearance.favorites().isEmpty());
}

void QuickMenuTests::appearanceFavoritesGoThroughTheWriter()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppearance appearance({presets}, prefs);
    QList<QPair<QString, QVariant>> received;
    bool accept = true;
    appearance.setPreferenceWriter([&](const QString &key, const QVariant &value) {
        received.append({key, value});
        if (!accept)
            return false;
        QSettings settings(prefs, QSettings::IniFormat);
        settings.setValue(key, value);
        settings.sync();
        return true;
    });
    QVERIFY(appearance.setFavorite(QStringLiteral("beta"), true));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.last().first, QStringLiteral("appearance/favorites"));
    QCOMPARE(received.last().second.toStringList(), QStringList{QStringLiteral("beta")});
    QCOMPARE(appearance.favorites(), QStringList{QStringLiteral("beta")});
    accept = false;
    QVERIFY(!appearance.setFavorite(QStringLiteral("alpha"), true));
    QCOMPARE(appearance.favorites(), QStringList{QStringLiteral("beta")});
    QVERIFY(!appearance.setFavorite(QStringLiteral("missing"), true));
    QCOMPARE(received.size(), 2);
}

void QuickMenuTests::appearanceRejectsInvalidPresets()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("good.desktop"), preset(QStringLiteral("Good"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("Bad Id.desktop"), preset(QStringLiteral("Bad"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("noname.desktop"), preset(QString(), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("longname.desktop"), preset(QString(40, QLatin1Char('x')), 1)));
    QMap<QString, QString> badHue = preset(QStringLiteral("Hue"), 1);
    badHue.insert(QStringLiteral("X-OpenWave-Appearance-Hue"), QStringLiteral("4"));
    QVERIFY(writePreset(presets, QStringLiteral("badhue.desktop"), badHue));
    QMap<QString, QString> notNumber = preset(QStringLiteral("Number"), 1);
    notNumber.insert(QStringLiteral("X-OpenWave-Appearance-Saturation"), QStringLiteral("lots"));
    QVERIFY(writePreset(presets, QStringLiteral("notnumber.desktop"), notNumber));
    QMap<QString, QString> badColor = preset(QStringLiteral("Color"), 1);
    badColor.insert(QStringLiteral("X-OpenWave-Appearance-ScrimColor"), QStringLiteral("red"));
    QVERIFY(writePreset(presets, QStringLiteral("badcolor.desktop"), badColor));
    QMap<QString, QString> escaping = preset(QStringLiteral("Escape"), 1);
    escaping.insert(QStringLiteral("X-OpenWave-Appearance-Preview"), QStringLiteral("../outside.png"));
    QVERIFY(writePreset(presets, QStringLiteral("escape.desktop"), escaping));
    QMap<QString, QString> missing = preset(QStringLiteral("Missing"), 1);
    missing.insert(QStringLiteral("X-OpenWave-Appearance-Preview"), QStringLiteral("nothing.png"));
    QVERIFY(writePreset(presets, QStringLiteral("missing.desktop"), missing));
    QMap<QString, QString> wallpaperEscape = preset(QStringLiteral("WallpaperEscape"), 1);
    wallpaperEscape.insert(QStringLiteral("X-OpenWave-Appearance-Wallpaper"), QStringLiteral("../outside.png"));
    QVERIFY(writePreset(presets, QStringLiteral("wallpaperescape.desktop"), wallpaperEscape));
    QMap<QString, QString> wallpaperMissing = preset(QStringLiteral("WallpaperMissing"), 1);
    wallpaperMissing.insert(QStringLiteral("X-OpenWave-Appearance-Wallpaper"), QStringLiteral("gone.png"));
    QVERIFY(writePreset(presets, QStringLiteral("wallpapermissing.desktop"), wallpaperMissing));
    QVERIFY(writePreset(presets, QStringLiteral("application.desktop"), preset(QStringLiteral("App"), 1), QStringLiteral("Application")));
    QVERIFY(writeFile(presets + QStringLiteral("/nogroup.desktop"), "Name=Nothing\nType=X-OpenWave-Appearance\n"));
    QVERIFY(writeFile(presets + QStringLiteral("/empty.desktop"), ""));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(appearance.presets().size(), 1);
    QCOMPARE(appearance.rejections().size(), 13);
}

void QuickMenuTests::appearanceGuaranteesTextContrast()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QMap<QString, QString> faint = preset(QStringLiteral("Faint"), 1);
    faint.insert(QStringLiteral("X-OpenWave-Appearance-ScrimAlpha"), QStringLiteral("0"));
    faint.insert(QStringLiteral("X-OpenWave-Appearance-ScrimColor"), QStringLiteral("#303030"));
    faint.insert(QStringLiteral("X-OpenWave-Appearance-Lightness"), QStringLiteral("0.9"));
    faint.insert(QStringLiteral("X-OpenWave-Appearance-Hue"), QStringLiteral("0.15"));
    QVERIFY(writePreset(presets, QStringLiteral("faint.desktop"), faint));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    const QColor white(Qt::white);
    const QColor overWhite = appearance.scrim();
    const double alpha = overWhite.alphaF();
    const QColor flattened = QColor::fromRgbF(overWhite.redF() * alpha + (1 - alpha), overWhite.greenF() * alpha + (1 - alpha),
                                              overWhite.blueF() * alpha + (1 - alpha));
    QVERIFY(QuickMenuAppearance::contrastRatio(flattened, white) >= QuickMenuAppearance::minimumContrast() - 0.02);
    QVERIFY(QuickMenuAppearance::contrastRatio(appearance.activeColor(), white) >= QuickMenuAppearance::minimumGraphicContrast());
    QVERIFY(appearance.scrim().alphaF() > 0.3);
}

void QuickMenuTests::appearanceFollowsExternalSelection()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    QuickMenuAppearance appearance({presets}, prefs);
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
    {
        QSettings settings(prefs, QSettings::IniFormat);
        settings.setValue(QStringLiteral("appearance/current"), QStringLiteral("beta"));
        settings.sync();
    }
    QTRY_COMPARE_WITH_TIMEOUT(appearance.current(), QStringLiteral("beta"), 5000);
    QSignalSpy listChanged(&appearance, &QuickMenuAppearance::presetsChanged);
    QVERIFY(writePreset(presets, QStringLiteral("gamma.desktop"), preset(QStringLiteral("Gamma"), 3)));
    QTRY_COMPARE_WITH_TIMEOUT(appearance.presets().size(), 3, 5000);
    QVERIFY(listChanged.count() >= 1);
}

void QuickMenuTests::appearancePreviewColorsMatchTheAppliedColors()
{
    QTemporaryDir directory;
    QuickMenuAppearance appearance({QStringLiteral(QUICKMENU_SOURCE_DIR "/tests/fixtures/appearances")}, directory.filePath(QStringLiteral("prefs.ini")));
    const QVariantList presets = appearance.presets();
    QCOMPARE(presets.size(), 4);
    for (const QVariant &item : presets) {
        const QVariantMap preview = item.toMap();
        QVERIFY(appearance.select(preview.value(QStringLiteral("id")).toString()));
        QCOMPARE(preview.value(QStringLiteral("wallpaper")).toUrl(), appearance.wallpaper());
        QVERIFY(appearance.wallpaper().toLocalFile().contains(preview.value(QStringLiteral("id")).toString() + QStringLiteral("-wallpaper.")));
        QCOMPARE(preview.value(QStringLiteral("scrim")).value<QColor>(), appearance.scrim());
        QCOMPARE(preview.value(QStringLiteral("activeColor")).value<QColor>(), appearance.activeColor());
        QCOMPARE(preview.value(QStringLiteral("inactiveColor")).value<QColor>(), appearance.inactiveColor());
        QCOMPARE(preview.value(QStringLiteral("textColor")).value<QColor>(), appearance.textColor());
        QCOMPARE(preview.value(QStringLiteral("sliderTrack")).value<QColor>(), appearance.sliderTrack());
        QCOMPARE(preview.value(QStringLiteral("sliderFill")).value<QColor>(), appearance.sliderFill());
        QCOMPARE(preview.value(QStringLiteral("sliderHandle")).value<QColor>(), appearance.sliderHandle());
        QCOMPARE(preview.value(QStringLiteral("selectionColor")).value<QColor>(), appearance.selectionColor());
    }
}

void QuickMenuTests::appearanceUsesTheWallpaperAsThumbnailWithoutAPreview()
{
    QTemporaryDir directory;
    QuickMenuAppearance appearance({QStringLiteral(QUICKMENU_SOURCE_DIR "/tests/fixtures/appearances")}, directory.filePath(QStringLiteral("prefs.ini")));
    QMap<QString, QVariantMap> byId;
    for (const QVariant &item : appearance.presets())
        byId.insert(item.toMap().value(QStringLiteral("id")).toString(), item.toMap());
    QVERIFY(byId.value(QStringLiteral("space")).value(QStringLiteral("preview")).toUrl().isEmpty());
    QCOMPARE(byId.value(QStringLiteral("space")).value(QStringLiteral("thumbnail")).toUrl(),
             byId.value(QStringLiteral("space")).value(QStringLiteral("wallpaper")).toUrl());
    QVERIFY(byId.value(QStringLiteral("dusk")).value(QStringLiteral("thumbnail")).toUrl().toLocalFile().endsWith(QStringLiteral("/dusk.png")));
    const QString bare = directory.filePath(QStringLiteral("bare"));
    QVERIFY(writePreset(bare, QStringLiteral("plain.desktop"), preset(QStringLiteral("Plain"), 1)));
    QuickMenuAppearance plain({bare}, directory.filePath(QStringLiteral("prefs2.ini")));
    QVERIFY(plain.presets().first().toMap().value(QStringLiteral("thumbnail")).toUrl().isEmpty());
}

void QuickMenuTests::appearanceFlagsOnlyLaterDirectoriesAsUserAppearances()
{
    QTemporaryDir directory;
    const QString system = directory.filePath(QStringLiteral("system"));
    const QString user = directory.filePath(QStringLiteral("user"));
    QVERIFY(writePreset(system, QStringLiteral("shipped.desktop"), preset(QStringLiteral("Shipped"), 1)));
    QVERIFY(writePreset(user, QStringLiteral("mine.desktop"), preset(QStringLiteral("Mine"), 2)));
    QVERIFY(writePreset(user, QStringLiteral("shipped.desktop"), preset(QStringLiteral("Impostor"), 3)));
    QuickMenuAppearance appearance({system, user}, directory.filePath(QStringLiteral("prefs.ini")));
    QCOMPARE(appearance.presets().size(), 2);
    QVERIFY(!appearance.presets().at(0).toMap().value(QStringLiteral("user")).toBool());
    QCOMPARE(appearance.presets().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Shipped"));
    QVERIFY(appearance.presets().at(1).toMap().value(QStringLiteral("user")).toBool());
    QCOMPARE(appearance.rejections().size(), 1);
}

void QuickMenuTests::appearanceRefreshPicksUpChangesWithoutWatchers()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QuickMenuAppearance appearance({presets}, directory.filePath(QStringLiteral("prefs.ini")));
    QSignalSpy listChanged(&appearance, &QuickMenuAppearance::presetsChanged);
    QSignalSpy changed(&appearance, &QuickMenuAppearance::changed);
    appearance.refresh();
    QCOMPARE(listChanged.count(), 0);
    QCOMPARE(changed.count(), 0);
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    appearance.refresh();
    QCOMPARE(appearance.presets().size(), 2);
    QCOMPARE(listChanged.count(), 1);
    QVERIFY(QFile::remove(presets + QStringLiteral("/alpha.desktop")));
    appearance.refresh();
    QCOMPARE(appearance.presets().size(), 1);
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
}

void QuickMenuTests::appearanceDefaultsReadTheSystemFolderAndTheBrokerStore()
{
    qunsetenv("OPENWAVE_SESSION_USER");
    const QStringList directories = QuickMenuAppearance::defaultPresetDirectories();
    QCOMPARE(directories.size(), 2);
    QCOMPARE(directories.first(), QStringLiteral("/System/QuickMenu/Appearances"));
    QCOMPARE(directories.last(), QStringLiteral("/var/openwave/appearances/users/%1").arg(geteuid()));
    qputenv("OPENWAVE_SESSION_USER", "no-such-user-here");
    QCOMPARE(QuickMenuAppearance::defaultPresetDirectories(), QStringList{QStringLiteral("/System/QuickMenu/Appearances")});
    qunsetenv("OPENWAVE_SESSION_USER");
}

void QuickMenuTests::appearanceReadsTheDaemonsCurrentMarker()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    const QString prefs = directory.filePath(QStringLiteral("prefs.ini"));
    {
        QSettings settings(prefs, QSettings::IniFormat);
        settings.setValue(QStringLiteral("appearance/current"), QStringLiteral("alpha"));
    }
    const QString marker = directory.filePath(QStringLiteral("marker/current"));
    QVERIFY(writeFile(marker, "beta"));
    QuickMenuAppearance appearance({presets}, prefs);
    appearance.setMarkerPath(marker);
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
    QVERIFY(writeFile(marker, "gone"));
    appearance.refresh();
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
    QVERIFY(writeFile(marker, "../../etc/passwd"));
    appearance.refresh();
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
    QVERIFY(writeFile(marker, "beta\n"));
    appearance.refresh();
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
    QVERIFY(QFile::remove(marker));
    appearance.refresh();
    QCOMPARE(appearance.current(), QStringLiteral("alpha"));
}

void QuickMenuTests::appearanceSelectionWorksWhenPreferencesAreNotWritable()
{
    QTemporaryDir directory;
    const QString presets = directory.filePath(QStringLiteral("presets"));
    QVERIFY(writePreset(presets, QStringLiteral("alpha.desktop"), preset(QStringLiteral("Alpha"), 1)));
    QVERIFY(writePreset(presets, QStringLiteral("beta.desktop"), preset(QStringLiteral("Beta"), 2)));
    QuickMenuAppearance appearance({presets}, QStringLiteral("/proc/openwave-no-such-dir/prefs.ini"));
    appearance.setMarkerPath(QString());
    QSignalSpy changed(&appearance, &QuickMenuAppearance::changed);
    QVERIFY(appearance.select(QStringLiteral("beta")));
    QCOMPARE(appearance.current(), QStringLiteral("beta"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!appearance.select(QStringLiteral("missing")));
}

void QuickMenuTests::appearanceShipsEmpty()
{
    QTemporaryDir directory;
    QDir empty(directory.filePath(QStringLiteral("none")));
    QuickMenuAppearance appearance({empty.path()}, directory.filePath(QStringLiteral("prefs.ini")));
    QVERIFY(appearance.current().isEmpty());
    QVERIFY(appearance.presets().isEmpty());
    QVERIFY(appearance.activeColor().isValid());
    QVERIFY(appearance.scrim().alpha() > 0);
    QVERIFY(!appearance.select(QStringLiteral("space")));
    QVERIFY(!QFileInfo::exists(QStringLiteral(QUICKMENU_SOURCE_DIR "/appearances")));
}

void QuickMenuTests::appearanceAcceptsTheExampleDesktopFiles()
{
    QTemporaryDir directory;
    QuickMenuAppearance appearance({QStringLiteral(QUICKMENU_SOURCE_DIR "/tests/fixtures/appearances")}, directory.filePath(QStringLiteral("prefs.ini")));
    QStringList order;
    for (const QVariant &item : appearance.presets())
        order << item.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(order, (QStringList{QStringLiteral("space"), QStringLiteral("purple"), QStringLiteral("ocean"), QStringLiteral("dusk")}));
    QVERIFY(appearance.rejections().isEmpty());
    const QColor white(Qt::white);
    QSet<QString> accents;
    for (const QString &id : order) {
        QVERIFY(appearance.select(id));
        QVERIFY(QuickMenuAppearance::contrastRatio(appearance.activeColor(), white) >= QuickMenuAppearance::minimumGraphicContrast());
        const QColor scrim = appearance.scrim();
        const double alpha = scrim.alphaF();
        const QColor flattened = QColor::fromRgbF(scrim.redF() * alpha + (1 - alpha), scrim.greenF() * alpha + (1 - alpha),
                                                  scrim.blueF() * alpha + (1 - alpha));
        QVERIFY(QuickMenuAppearance::contrastRatio(flattened, white) >= QuickMenuAppearance::minimumContrast() - 0.02);
        accents.insert(appearance.accent().name());
    }
    QCOMPARE(accents.size(), 4);
}

void QuickMenuTests::systemReportsUnavailableWithoutBackend()
{
    QuickMenuSystem system(nullptr);
    QVERIFY(!system.connected());
    QVERIFY(!system.wifiAvailable());
    QVERIFY(!system.wifiEnabled());
    QVERIFY(!system.bluetoothAvailable());
    QVERIFY(!system.cellularAvailable());
    QCOMPARE(system.volume(), 0);
    QSignalSpy failed(&system, &QuickMenuSystem::operationFailed);
    system.setWifiEnabled(true);
    system.setBluetoothEnabled(true);
    system.setAirplaneMode(true);
    system.setVolume(10);
    system.setBrightness(10);
    QCOMPARE(failed.count(), 5);
}

void QuickMenuTests::systemForwardsStateAndRequests()
{
    MockBackend backend;
    backend.wifiEnabled = true;
    backend.wifiConnected = true;
    backend.wifiStrength = 75;
    backend.volume = 47;
    backend.brightness = 70;
    backend.cellularAvailable = true;
    backend.cellularStrength = 60;
    backend.modemLabel = QStringLiteral("Carrier - 123");
    QuickMenuSystem system(nullptr);
    system.setPowerSupplyRoot(QStringLiteral("/nonexistent"));
    system.attach(&backend);
    QVERIFY(system.connected());
    QVERIFY(system.wifiEnabled());
    QVERIFY(system.wifiConnected());
    QCOMPARE(system.wifiStrength(), 75);
    QCOMPARE(system.volume(), 47);
    QCOMPARE(system.brightness(), 70);
    QVERIFY(system.cellularAvailable());
    QCOMPARE(system.cellularStrength(), 60);
    QCOMPARE(system.modemLabel(), QStringLiteral("Carrier - 123"));

    QSignalSpy changed(&system, &QuickMenuSystem::changed);
    system.setVolume(90);
    system.setBrightness(0);
    system.setBluetoothEnabled(true);
    system.setAirplaneMode(true);
    system.setWifiEnabled(false);
    QCOMPARE(backend.calls, (QStringList{QStringLiteral("volume:90"), QStringLiteral("brightness:1"), QStringLiteral("bluetooth:1"),
                                         QStringLiteral("airplane:1"), QStringLiteral("wifi:0")}));
    QCOMPARE(system.volume(), 90);
    QVERIFY(system.bluetoothEnabled());
    QVERIFY(system.airplaneMode());
    QVERIFY(changed.count() >= 5);
    system.setVolume(500);
    QCOMPARE(backend.volume, 100);

    QSignalSpy failed(&system, &QuickMenuSystem::operationFailed);
    emit backend.operationFailed(QStringLiteral("backend failure"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("backend failure"));
}

void QuickMenuTests::systemRefusesUnavailableHardware()
{
    MockBackend backend;
    backend.wifiAvailable = false;
    backend.bluetoothAvailable = false;
    QuickMenuSystem system(nullptr);
    system.attach(&backend);
    QSignalSpy failed(&system, &QuickMenuSystem::operationFailed);
    system.setWifiEnabled(true);
    system.setBluetoothEnabled(true);
    QCOMPARE(failed.count(), 2);
    QVERIFY(backend.calls.isEmpty());
    QVERIFY(!system.wifiEnabled());
}

void QuickMenuTests::systemNeverPretendsToControlMobileData()
{
    MockBackend backend;
    backend.cellularAvailable = true;
    QuickMenuSystem system(nullptr);
    system.attach(&backend);
    QVERIFY(!system.mobileDataSupported());
    QSignalSpy failed(&system, &QuickMenuSystem::operationFailed);
    system.setMobileDataEnabled(true);
    QCOMPARE(failed.count(), 1);
    QVERIFY(backend.calls.isEmpty());
}

void QuickMenuTests::systemRejectsBackendsWithoutTheMethod()
{
    BareBackend backend;
    QuickMenuSystem system(nullptr);
    system.attach(&backend);
    QSignalSpy failed(&system, &QuickMenuSystem::operationFailed);
    system.setVolume(10);
    system.setAirplaneMode(true);
    QCOMPARE(failed.count(), 2);
}

void QuickMenuTests::systemReadsBattery()
{
    QTemporaryDir directory;
    QuickMenuSystem system(nullptr);
    system.setPowerSupplyRoot(directory.path());
    QVERIFY(!system.batteryAvailable());
    QVERIFY(writeFile(directory.filePath(QStringLiteral("AC/type")), "Mains\n"));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/type")), "Battery\n"));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/capacity")), "87\n"));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/status")), "Charging\n"));
    QSignalSpy changed(&system, &QuickMenuSystem::batteryChanged);
    system.refreshBattery();
    QVERIFY(system.batteryAvailable());
    QCOMPARE(system.batteryPercent(), 87);
    QVERIFY(system.batteryCharging());
    QCOMPARE(changed.count(), 1);
    system.refreshBattery();
    QCOMPARE(changed.count(), 1);
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/status")), "Discharging\n"));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/capacity")), "12\n"));
    system.refreshBattery();
    QCOMPARE(system.batteryPercent(), 12);
    QVERIFY(!system.batteryCharging());
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/capacity")), "250\n"));
    system.refreshBattery();
    QVERIFY(!system.batteryAvailable());
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/capacity")), "50\n"));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("BAT0/present")), "0\n"));
    system.refreshBattery();
    QVERIFY(!system.batteryAvailable());
}

void QuickMenuTests::systemEmitsLockRequest()
{
    QuickMenuSystem system(nullptr);
    QSignalSpy lock(&system, &QuickMenuSystem::lockRequested);
    system.lock();
    QCOMPARE(lock.count(), 1);
}

QTEST_MAIN(QuickMenuTests)
#include "tst_quickmenu.moc"
