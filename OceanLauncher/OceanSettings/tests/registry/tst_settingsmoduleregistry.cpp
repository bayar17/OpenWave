#include "SettingsModuleRegistry.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqml.h>
#include <QtTest>

namespace {

QJsonObject validMetadata(const QString &id, const QString &name, int order)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), QStringLiteral("Description of %1").arg(name)},
        {QStringLiteral("icon"), QStringLiteral("network-wireless")},
        {QStringLiteral("category"), QStringLiteral("Connectivity")},
        {QStringLiteral("order"), order},
        {QStringLiteral("apiVersion"), 1},
        {QStringLiteral("entry"), QStringLiteral("MainModule.qml")}
    };
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

bool writeModule(const QString &root, const QString &directory, const QJsonObject &metadata,
                 bool withEntry = true)
{
    const QString base = root + QLatin1Char('/') + directory;
    if (!writeFile(base + QStringLiteral("/module.json"), QJsonDocument(metadata).toJson()))
        return false;
    return !withEntry || writeFile(base + QStringLiteral("/MainModule.qml"),
                                   "import QtQuick\nItem {}\n");
}

QStringList ids(SettingsModuleRegistry &registry)
{
    QStringList result;
    for (int row = 0; row < registry.rowCount(); ++row)
        result << registry.data(registry.index(row), SettingsModuleRegistry::IdRole).toString();
    return result;
}

}

class SettingsModuleRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        QDir(m_directory.path()).removeRecursively();
        QVERIFY(QDir().mkpath(root()));
    }

    void discoversValidModules()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"),
                            validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        QVERIFY(writeModule(root(), QStringLiteral("Bluetooth"),
                            validMetadata(QStringLiteral("com.openwave.settings.bluetooth"), QStringLiteral("Bluetooth"), 20)));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 2);
        QCOMPARE(ids(registry), (QStringList{QStringLiteral("com.openwave.settings.wifi"),
                                             QStringLiteral("com.openwave.settings.bluetooth")}));
    }

    void rejectsInvalidJson()
    {
        QVERIFY(writeFile(root() + QStringLiteral("/Broken/module.json"), "{ not json"));
        QVERIFY(writeFile(root() + QStringLiteral("/Broken/MainModule.qml"), "Item {}"));
        QVERIFY(writeFile(root() + QStringLiteral("/Array/module.json"), "[1,2]"));
        QVERIFY(writeFile(root() + QStringLiteral("/Array/MainModule.qml"), "Item {}"));
        QVERIFY(writeFile(root() + QStringLiteral("/Empty/MainModule.qml"), "Item {}"));
        QVERIFY(writeFile(root() + QStringLiteral("/Huge/module.json"), QByteArray(70 * 1024, ' ')));
        QVERIFY(writeFile(root() + QStringLiteral("/Huge/MainModule.qml"), "Item {}"));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), 4);
    }

    void rejectsWrongFieldTypesAndMissingFields()
    {
        QJsonObject missingName = validMetadata(QStringLiteral("com.openwave.settings.a"), QStringLiteral("A"), 1);
        missingName.remove(QStringLiteral("name"));
        QVERIFY(writeModule(root(), QStringLiteral("a"), missingName));
        QJsonObject numericName = validMetadata(QStringLiteral("com.openwave.settings.b"), QStringLiteral("B"), 1);
        numericName[QStringLiteral("name")] = 5;
        QVERIFY(writeModule(root(), QStringLiteral("b"), numericName));
        QJsonObject fractionalOrder = validMetadata(QStringLiteral("com.openwave.settings.c"), QStringLiteral("C"), 1);
        fractionalOrder[QStringLiteral("order")] = 1.5;
        QVERIFY(writeModule(root(), QStringLiteral("c"), fractionalOrder));
        QJsonObject badIcon = validMetadata(QStringLiteral("com.openwave.settings.d"), QStringLiteral("D"), 1);
        badIcon[QStringLiteral("icon")] = QStringLiteral("../../etc/passwd");
        QVERIFY(writeModule(root(), QStringLiteral("d"), badIcon));
        QJsonObject controlName = validMetadata(QStringLiteral("com.openwave.settings.e"), QStringLiteral("E"), 1);
        controlName[QStringLiteral("name")] = QStringLiteral("Bad\nName");
        QVERIFY(writeModule(root(), QStringLiteral("e"), controlName));
        QJsonObject longName = validMetadata(QStringLiteral("com.openwave.settings.f"), QStringLiteral("F"), 1);
        longName[QStringLiteral("name")] = QString(200, QLatin1Char('x'));
        QVERIFY(writeModule(root(), QStringLiteral("f"), longName));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), 6);
    }

    void optionalFieldsHaveDefaults()
    {
        QJsonObject minimal{{QStringLiteral("id"), QStringLiteral("com.openwave.settings.min")},
                            {QStringLiteral("name"), QStringLiteral("Minimal")},
                            {QStringLiteral("apiVersion"), 1}};
        QVERIFY(writeModule(root(), QStringLiteral("min"), minimal));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 1);
        const QVariantMap info = registry.module(QStringLiteral("com.openwave.settings.min"));
        QCOMPARE(info.value(QStringLiteral("order")).toInt(), SettingsModuleRegistry::DefaultOrder);
        QCOMPARE(info.value(QStringLiteral("description")).toString(), QString());
        QVERIFY(info.value(QStringLiteral("entryUrl")).toUrl().path().endsWith(QStringLiteral("/min/MainModule.qml")));
    }

    void rejectsMissingEntryFile()
    {
        QVERIFY(writeModule(root(), QStringLiteral("NoEntry"),
                            validMetadata(QStringLiteral("com.openwave.settings.noentry"), QStringLiteral("None"), 1), false));
        QVERIFY(QDir().mkpath(root() + QStringLiteral("/DirEntry/MainModule.qml")));
        QVERIFY(writeFile(root() + QStringLiteral("/DirEntry/module.json"),
                          QJsonDocument(validMetadata(QStringLiteral("com.openwave.settings.direntry"), QStringLiteral("Dir"), 1)).toJson()));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), 2);
    }

    void rejectsInvalidModuleIds()
    {
        const QStringList bad = {
            QStringLiteral(""), QStringLiteral("wifi"), QStringLiteral("com.openwave"),
            QStringLiteral("Com.OpenWave.Settings.Wifi"), QStringLiteral("com..openwave.wifi"),
            QStringLiteral("com.openwave.settings.wifi/../x"), QStringLiteral("com.openwave.settings.wifi "),
            QStringLiteral("com.openwave.settings.wi fi"), QStringLiteral("../com.openwave.settings.wifi"),
            QStringLiteral("com.openwave.settings.") , QString(300, QLatin1Char('a')) + QStringLiteral(".b.c")};
        int index = 0;
        for (const QString &id : bad)
            QVERIFY(writeModule(root(), QStringLiteral("bad%1").arg(index++),
                                validMetadata(id, QStringLiteral("Bad"), 1)));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), bad.size());
        QVERIFY(SettingsModuleRegistry::isValidModuleId(QStringLiteral("com.openwave.settings.wifi")));
        QVERIFY(!SettingsModuleRegistry::isValidModuleId(QStringLiteral("a.b")));
    }

    void rejectsDuplicateIds()
    {
        const QString id = QStringLiteral("com.openwave.settings.same");
        QVERIFY(writeModule(root(), QStringLiteral("first"), validMetadata(id, QStringLiteral("First"), 1)));
        QVERIFY(writeModule(root(), QStringLiteral("second"), validMetadata(id, QStringLiteral("Second"), 2)));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 1);
        QCOMPARE(registry.module(id).value(QStringLiteral("name")).toString(), QStringLiteral("First"));
        QCOMPARE(rejected.count(), 1);
        QVERIFY(rejected.first().at(1).toString().contains(QStringLiteral("duplicate")));
    }

    void rejectsUnsupportedApiVersions()
    {
        const QList<QJsonValue> versions = {QJsonValue(2), QJsonValue(0), QJsonValue(-1), QJsonValue(99),
                                            QJsonValue(QStringLiteral("1")), QJsonValue(1.5), QJsonValue(true)};
        int index = 0;
        for (const QJsonValue &version : versions) {
            QJsonObject metadata = validMetadata(QStringLiteral("com.openwave.settings.v%1").arg(index),
                                                 QStringLiteral("V"), 1);
            metadata[QStringLiteral("apiVersion")] = version;
            QVERIFY(writeModule(root(), QStringLiteral("v%1").arg(index++), metadata));
        }
        QJsonObject missing = validMetadata(QStringLiteral("com.openwave.settings.vmissing"), QStringLiteral("V"), 1);
        missing.remove(QStringLiteral("apiVersion"));
        QVERIFY(writeModule(root(), QStringLiteral("vmissing"), missing));
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), versions.size() + 1);
    }

    void rejectsSymlinks()
    {
        const QString outside = m_directory.filePath(QStringLiteral("outside"));
        QVERIFY(writeModule(outside, QStringLiteral("evil"),
                            validMetadata(QStringLiteral("com.openwave.settings.evil"), QStringLiteral("Evil"), 1)));
        QVERIFY(QFile::link(outside + QStringLiteral("/evil"), root() + QStringLiteral("/linkeddir")));

        QVERIFY(writeModule(root(), QStringLiteral("linkedjson"),
                            validMetadata(QStringLiteral("com.openwave.settings.linkedjson"), QStringLiteral("LJ"), 1)));
        QVERIFY(QFile::remove(root() + QStringLiteral("/linkedjson/module.json")));
        QVERIFY(QFile::link(outside + QStringLiteral("/evil/module.json"), root() + QStringLiteral("/linkedjson/module.json")));

        QVERIFY(writeModule(root(), QStringLiteral("linkedentry"),
                            validMetadata(QStringLiteral("com.openwave.settings.linkedentry"), QStringLiteral("LE"), 1), false));
        QVERIFY(QFile::link(outside + QStringLiteral("/evil/MainModule.qml"), root() + QStringLiteral("/linkedentry/MainModule.qml")));

        QJsonObject nested = validMetadata(QStringLiteral("com.openwave.settings.linkedsub"), QStringLiteral("LS"), 1);
        nested[QStringLiteral("entry")] = QStringLiteral("ui/Main.qml");
        QVERIFY(writeModule(root(), QStringLiteral("linkedsub"), nested, false));
        QVERIFY(writeFile(outside + QStringLiteral("/ui/Main.qml"), "Item {}"));
        QVERIFY(QFile::link(outside + QStringLiteral("/ui"), root() + QStringLiteral("/linkedsub/ui")));

        QJsonObject inner = validMetadata(QStringLiteral("com.openwave.settings.innerlink"), QStringLiteral("IL"), 1);
        inner[QStringLiteral("entry")] = QStringLiteral("ui/Main.qml");
        QVERIFY(writeModule(root(), QStringLiteral("innerlink"), inner, false));
        QVERIFY(writeFile(root() + QStringLiteral("/innerlink/real/Main.qml"), "Item {}"));
        QVERIFY(QFile::link(QStringLiteral("real"), root() + QStringLiteral("/innerlink/ui")));

        QVERIFY(QFile::link(root() + QStringLiteral("/nonexistent-target"), root() + QStringLiteral("/dangling")));

        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), 6);
    }

    void rejectsPathTraversalInEntry()
    {
        QVERIFY(writeFile(m_directory.filePath(QStringLiteral("escape.qml")), "Item {}"));
        const QStringList entries = {
            QStringLiteral("../escape.qml"), QStringLiteral("../../escape.qml"),
            QStringLiteral("sub/../../escape.qml"), QStringLiteral("/etc/passwd.qml"),
            QStringLiteral("..\\escape.qml"), QStringLiteral("./MainModule.qml"),
            QStringLiteral("sub//Main.qml"), QStringLiteral("MainModule.txt"),
            QStringLiteral(".qml"), QStringLiteral("sub/"), QStringLiteral("")};
        int index = 0;
        for (const QString &entry : entries) {
            QJsonObject metadata = validMetadata(QStringLiteral("com.openwave.settings.t%1").arg(index),
                                                 QStringLiteral("T"), 1);
            metadata[QStringLiteral("entry")] = entry;
            QVERIFY(writeModule(root(), QStringLiteral("t%1").arg(index++), metadata));
        }
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), entries.size());
    }

    void acceptsNestedEntryInsideModule()
    {
        QJsonObject metadata = validMetadata(QStringLiteral("com.openwave.settings.nested"), QStringLiteral("Nested"), 1);
        metadata[QStringLiteral("entry")] = QStringLiteral("ui/Main.qml");
        QVERIFY(writeModule(root(), QStringLiteral("nested"), metadata, false));
        QVERIFY(writeFile(root() + QStringLiteral("/nested/ui/Main.qml"), "Item {}"));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 1);
        QVERIFY(registry.entryUrl(QStringLiteral("com.openwave.settings.nested")).path().endsWith(QStringLiteral("/nested/ui/Main.qml")));
    }

    void handlesMissingRoot()
    {
        SettingsModuleRegistry registry(QString{});
        QSignalSpy failed(&registry, &SettingsModuleRegistry::scanFailed);
        registry.setModuleRoot(m_directory.filePath(QStringLiteral("does/not/exist")));
        QCOMPARE(registry.count(), 0);
        QCOMPARE(failed.count(), 1);
        registry.reload();
        QCOMPARE(failed.count(), 2);
        QVERIFY(writeFile(m_directory.filePath(QStringLiteral("afile")), "x"));
        registry.setModuleRoot(m_directory.filePath(QStringLiteral("afile")));
        QCOMPARE(registry.count(), 0);
        QCOMPARE(failed.count(), 3);
    }

    void sortsByOrderThenName()
    {
        QVERIFY(writeModule(root(), QStringLiteral("z"), validMetadata(QStringLiteral("com.openwave.settings.zeta"), QStringLiteral("Zeta"), 5)));
        QVERIFY(writeModule(root(), QStringLiteral("a"), validMetadata(QStringLiteral("com.openwave.settings.alpha"), QStringLiteral("alpha"), 50)));
        QVERIFY(writeModule(root(), QStringLiteral("m"), validMetadata(QStringLiteral("com.openwave.settings.mid"), QStringLiteral("Mid"), 20)));
        QVERIFY(writeModule(root(), QStringLiteral("b"), validMetadata(QStringLiteral("com.openwave.settings.beta"), QStringLiteral("Beta"), 20)));
        QJsonObject unordered = validMetadata(QStringLiteral("com.openwave.settings.unordered"), QStringLiteral("Unordered"), 0);
        unordered.remove(QStringLiteral("order"));
        QVERIFY(writeModule(root(), QStringLiteral("u"), unordered));
        QVERIFY(writeModule(root(), QStringLiteral("n"), validMetadata(QStringLiteral("com.openwave.settings.negative"), QStringLiteral("Negative"), -5)));
        SettingsModuleRegistry registry(root());
        QCOMPARE(ids(registry), (QStringList{
            QStringLiteral("com.openwave.settings.negative"), QStringLiteral("com.openwave.settings.zeta"),
            QStringLiteral("com.openwave.settings.beta"), QStringLiteral("com.openwave.settings.mid"),
            QStringLiteral("com.openwave.settings.alpha"), QStringLiteral("com.openwave.settings.unordered")}));
    }

    void looksUpModulesById()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        SettingsModuleRegistry registry(root());
        const QVariantMap info = registry.module(QStringLiteral("com.openwave.settings.wifi"));
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("Wi-Fi"));
        QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral("Connectivity"));
        QCOMPARE(info.value(QStringLiteral("icon")).toString(), QStringLiteral("network-wireless"));
        QCOMPARE(info.value(QStringLiteral("order")).toInt(), 10);
        QCOMPARE(info.value(QStringLiteral("apiVersion")).toInt(), 1);
        const QUrl url = registry.entryUrl(QStringLiteral("com.openwave.settings.wifi"));
        QVERIFY(url.isLocalFile());
        QVERIFY(QFileInfo::exists(url.toLocalFile()));
        QVERIFY(url.path().endsWith(QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(registry.module(QStringLiteral("com.openwave.settings.nothing")).isEmpty());
        QVERIFY(registry.entryUrl(QStringLiteral("com.openwave.settings.nothing")).isEmpty());
        QVERIFY(registry.entryUrl(QString()).isEmpty());
    }

    void reloadPicksUpChanges()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 1);
        QSignalSpy reset(&registry, &QAbstractItemModel::modelReset);
        QSignalSpy countChanged(&registry, &SettingsModuleRegistry::countChanged);
        QSignalSpy reloaded(&registry, &SettingsModuleRegistry::reloaded);

        QVERIFY(writeModule(root(), QStringLiteral("Bluetooth"), validMetadata(QStringLiteral("com.openwave.settings.bluetooth"), QStringLiteral("Bluetooth"), 20)));
        QCOMPARE(registry.count(), 1);
        registry.reload();
        QCOMPARE(registry.count(), 2);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(countChanged.count(), 1);
        QCOMPARE(reloaded.count(), 1);

        QVERIFY(QDir(root() + QStringLiteral("/Wifi")).removeRecursively());
        registry.reload();
        QCOMPARE(ids(registry), (QStringList{QStringLiteral("com.openwave.settings.bluetooth")}));
        QVERIFY(registry.entryUrl(QStringLiteral("com.openwave.settings.wifi")).isEmpty());

        registry.reload();
        QCOMPARE(registry.count(), 1);
        QCOMPARE(countChanged.count(), 2);
    }

    void emptyRootGivesEmptyModel()
    {
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(registry.rowCount(), 0);
        QVERIFY(!registry.data(registry.index(0), SettingsModuleRegistry::IdRole).isValid());
        QVERIFY(!registry.data(QModelIndex(), SettingsModuleRegistry::IdRole).isValid());
        QVERIFY(registry.module(QStringLiteral("com.openwave.settings.wifi")).isEmpty());
        QVERIFY(QFile::link(QStringLiteral("x"), root() + QStringLiteral("/.hidden")));
        QVERIFY(writeFile(root() + QStringLiteral("/stray.txt"), "x"));
        registry.reload();
        QCOMPARE(registry.count(), 0);
    }

    void exposesModelRoles()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        SettingsModuleRegistry registry(root());
        const QHash<int, QByteArray> names = registry.roleNames();
        const QStringList expected = {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("description"),
                                      QStringLiteral("icon"), QStringLiteral("category"), QStringLiteral("order"),
                                      QStringLiteral("apiVersion"), QStringLiteral("entryUrl")};
        QStringList actual;
        for (const QByteArray &name : names)
            actual << QString::fromLatin1(name);
        actual.sort();
        QStringList sorted = expected;
        sorted.sort();
        QCOMPARE(actual, sorted);
        const QModelIndex index = registry.index(0);
        QCOMPARE(registry.data(index, SettingsModuleRegistry::NameRole).toString(), QStringLiteral("Wi-Fi"));
        QCOMPARE(registry.data(index, Qt::DisplayRole).toString(), QStringLiteral("Wi-Fi"));
        QCOMPARE(registry.data(index, SettingsModuleRegistry::OrderRole).toInt(), 10);
        QCOMPARE(registry.data(index, SettingsModuleRegistry::EntryUrlRole).toUrl(),
                 registry.entryUrl(QStringLiteral("com.openwave.settings.wifi")));
        QCOMPARE(registry.model(), static_cast<QAbstractItemModel *>(&registry));
    }

    void exposesOptionalSearchEntries()
    {
        QJsonObject metadata = validMetadata(QStringLiteral("com.openwave.settings.general"), QStringLiteral("General"), 30);
        metadata[QStringLiteral("search")] = QJsonArray{
            QJsonObject{{QStringLiteral("key"), QStringLiteral("name")}, {QStringLiteral("title"), QStringLiteral("Device name")}, {QStringLiteral("detail"), QStringLiteral("Name of this device")}},
            QJsonObject{{QStringLiteral("key"), QStringLiteral("time-zone")}, {QStringLiteral("title"), QStringLiteral("Time zone")}}};
        QVERIFY(writeModule(root(), QStringLiteral("general"), metadata));
        QVERIFY(writeModule(root(), QStringLiteral("plain"), validMetadata(QStringLiteral("com.openwave.settings.plain"), QStringLiteral("Plain"), 40)));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 2);
        const QVariantList search = registry.module(QStringLiteral("com.openwave.settings.general")).value(QStringLiteral("searchEntries")).toList();
        QCOMPARE(search.size(), 2);
        QCOMPARE(search.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("name"));
        QCOMPARE(search.at(0).toMap().value(QStringLiteral("detail")).toString(), QStringLiteral("Name of this device"));
        QCOMPARE(search.at(1).toMap().value(QStringLiteral("detail")).toString(), QString());
        QVERIFY(registry.module(QStringLiteral("com.openwave.settings.plain")).value(QStringLiteral("searchEntries")).toList().isEmpty());
    }

    void rejectsInvalidSearchEntries()
    {
        const auto entry = [](const QString &key, const QString &title) {
            return QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("title"), title}};
        };
        QJsonArray tooMany;
        for (int index = 0; index < 17; ++index)
            tooMany.append(entry(QStringLiteral("k%1").arg(index), QStringLiteral("T")));
        const QList<QJsonValue> bad = {
            QJsonValue(QStringLiteral("not an array")), QJsonValue(5),
            QJsonValue(QJsonArray{QJsonValue(QStringLiteral("string entry"))}),
            QJsonValue(QJsonArray{entry(QStringLiteral("Bad Key"), QStringLiteral("T"))}),
            QJsonValue(QJsonArray{entry(QStringLiteral("1abc"), QStringLiteral("T"))}),
            QJsonValue(QJsonArray{entry(QStringLiteral("ok"), QString())}),
            QJsonValue(QJsonArray{entry(QStringLiteral("ok"), QStringLiteral("Bad\nTitle"))}),
            QJsonValue(QJsonArray{entry(QStringLiteral("ok"), QString(200, QLatin1Char('x')))}),
            QJsonValue(QJsonArray{entry(QStringLiteral("dup"), QStringLiteral("A")), entry(QStringLiteral("dup"), QStringLiteral("B"))}),
            QJsonValue(QJsonArray{QJsonObject{{QStringLiteral("key"), 5}, {QStringLiteral("title"), QStringLiteral("T")}}}),
            QJsonValue(tooMany)};
        int index = 0;
        for (const QJsonValue &value : bad) {
            QJsonObject metadata = validMetadata(QStringLiteral("com.openwave.settings.s%1").arg(index), QStringLiteral("S"), 1);
            metadata[QStringLiteral("search")] = value;
            QVERIFY(writeModule(root(), QStringLiteral("s%1").arg(index++), metadata));
        }
        SettingsModuleRegistry registry(QString{});
        QSignalSpy rejected(&registry, &SettingsModuleRegistry::moduleRejected);
        registry.setModuleRoot(root());
        QCOMPARE(registry.count(), 0);
        QCOMPARE(rejected.count(), bad.size());
    }

    void listsModulesInSortedOrder()
    {
        QVERIFY(writeModule(root(), QStringLiteral("b"), validMetadata(QStringLiteral("com.openwave.settings.second"), QStringLiteral("Second"), 20)));
        QVERIFY(writeModule(root(), QStringLiteral("a"), validMetadata(QStringLiteral("com.openwave.settings.first"), QStringLiteral("First"), 10)));
        SettingsModuleRegistry registry(root());
        const QVariantList list = registry.modules();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("com.openwave.settings.first"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Second"));
        QVERIFY(list.at(0).toMap().value(QStringLiteral("entryUrl")).toUrl().isLocalFile());
        QVERIFY(!list.at(0).toMap().contains(QStringLiteral("directory")));
        SettingsModuleRegistry empty(m_directory.filePath(QStringLiteral("none")));
        QVERIFY(empty.modules().isEmpty());
    }

    void revalidateReturnsCurrentEntrySource()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        SettingsModuleRegistry registry(root());
        SettingsModuleRegistry::ValidatedModule validated;
        QString reason;
        QVERIFY2(registry.revalidate(QStringLiteral("com.openwave.settings.wifi"), validated, reason), qPrintable(reason));
        QCOMPARE(validated.entrySource, QByteArray("import QtQuick\nItem {}\n"));
        QCOMPARE(validated.entryUrl, registry.entryUrl(QStringLiteral("com.openwave.settings.wifi")));
        QVERIFY(validated.entryUrl.toLocalFile().startsWith(validated.directory + QLatin1Char('/')));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/MainModule.qml"), "Item { property int changed: 1 }"));
        QVERIFY(registry.revalidate(QStringLiteral("com.openwave.settings.wifi"), validated, reason));
        QCOMPARE(validated.entrySource, QByteArray("Item { property int changed: 1 }"));
    }

    void revalidateRejectsChangesAfterDiscovery()
    {
        const QString id = QStringLiteral("com.openwave.settings.wifi");
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(id, QStringLiteral("Wi-Fi"), 10)));
        SettingsModuleRegistry registry(root());
        SettingsModuleRegistry::ValidatedModule validated;
        QString reason;
        QVERIFY(!registry.revalidate(QStringLiteral("com.openwave.settings.unknown"), validated, reason));
        QVERIFY(reason.contains(QStringLiteral("unknown")));

        const QString outside = m_directory.filePath(QStringLiteral("outside"));
        QVERIFY(writeFile(outside + QStringLiteral("/Evil.qml"), "Item {}"));
        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(QFile::link(outside + QStringLiteral("/Evil.qml"), root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(!registry.revalidate(id, validated, reason));
        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi/MainModule.qml")));
        QVERIFY(!registry.revalidate(id, validated, reason));
        QVERIFY(writeFile(root() + QStringLiteral("/Wifi/MainModule.qml"), "Item {}"));
        QVERIFY(registry.revalidate(id, validated, reason));

        QVERIFY(writeModule(outside, QStringLiteral("evil"), validMetadata(id, QStringLiteral("Evil"), 1)));
        QVERIFY(QDir(root() + QStringLiteral("/Wifi")).removeRecursively());
        QVERIFY(QFile::link(outside + QStringLiteral("/evil"), root() + QStringLiteral("/Wifi")));
        QVERIFY(!registry.revalidate(id, validated, reason));
        QVERIFY(reason.contains(QStringLiteral("symbolic")));

        QVERIFY(QFile::remove(root() + QStringLiteral("/Wifi")));
        QVERIFY(QDir(root()).removeRecursively());
        QVERIFY(!registry.revalidate(id, validated, reason));
        QVERIFY(reason.contains(QStringLiteral("root")));
    }

    void doesNotInstantiateModuleQml()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Marker"), validMetadata(QStringLiteral("com.openwave.settings.marker"), QStringLiteral("Marker"), 1), false));
        const QString marker = m_directory.filePath(QStringLiteral("executed"));
        QVERIFY(writeFile(root() + QStringLiteral("/Marker/MainModule.qml"),
                          QStringLiteral("import QtQuick\nItem { Component.onCompleted: console.log(\"%1\") }\n").arg(marker).toUtf8()));
        SettingsModuleRegistry registry(root());
        QCOMPARE(registry.count(), 1);
        QVERIFY(!QFileInfo::exists(marker));
    }

    void usableFromQmlListView()
    {
        QVERIFY(writeModule(root(), QStringLiteral("Wifi"), validMetadata(QStringLiteral("com.openwave.settings.wifi"), QStringLiteral("Wi-Fi"), 10)));
        QVERIFY(writeModule(root(), QStringLiteral("Bluetooth"), validMetadata(QStringLiteral("com.openwave.settings.bluetooth"), QStringLiteral("Bluetooth"), 20)));
        SettingsModuleRegistry::shared()->setModuleRoot(root());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import OpenWave.Settings 1.0
            Item {
                id: root
                width: 400
                height: 400
                property alias list: view
                property int registryCount: SettingsModuleRegistry.count
                property string wifiUrl: SettingsModuleRegistry.entryUrl("com.openwave.settings.wifi")
                property var wifi: SettingsModuleRegistry.module("com.openwave.settings.wifi")
                ListView {
                    id: view
                    anchors.fill: parent
                    model: SettingsModuleRegistry.model
                    delegate: Item {
                        width: view.width
                        height: 40
                        property string rowId: id
                        property string rowName: name
                        property string rowCategory: category
                        property int rowOrder: order
                        property url rowEntry: entryUrl
                    }
                }
                function reloadRegistry() { SettingsModuleRegistry.reload() }
            }
        )", QUrl(QStringLiteral("qrc:/test.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));

        QCOMPARE(object->property("registryCount").toInt(), 2);
        QVERIFY(object->property("wifiUrl").toString().endsWith(QStringLiteral("/Wifi/MainModule.qml")));
        QCOMPARE(object->property("wifi").toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Wi-Fi"));

        auto *list = object->property("list").value<QQuickItem *>();
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 2);
        QQuickItem *first = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0));
        QVERIFY(first);
        QCOMPARE(first->property("rowId").toString(), QStringLiteral("com.openwave.settings.wifi"));
        QCOMPARE(first->property("rowName").toString(), QStringLiteral("Wi-Fi"));
        QCOMPARE(first->property("rowCategory").toString(), QStringLiteral("Connectivity"));
        QCOMPARE(first->property("rowOrder").toInt(), 10);
        QVERIFY(first->property("rowEntry").toUrl().path().endsWith(QStringLiteral("/Wifi/MainModule.qml")));

        QVERIFY(QDir(root() + QStringLiteral("/Bluetooth")).removeRecursively());
        QMetaObject::invokeMethod(object.data(), "reloadRegistry");
        QCOMPARE(object->property("registryCount").toInt(), 1);
        QCOMPARE(list->property("count").toInt(), 1);
        SettingsModuleRegistry::shared()->setModuleRoot(QString());
    }

private:
    QString root() const { return m_directory.filePath(QStringLiteral("Modules")); }

    QTemporaryDir m_directory;
};

QTEST_MAIN(SettingsModuleRegistryTest)
#include "tst_settingsmoduleregistry.moc"
