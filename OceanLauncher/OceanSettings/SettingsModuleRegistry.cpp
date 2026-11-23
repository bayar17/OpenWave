#include "SettingsModuleRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr qint64 MaxMetadataBytes = 64 * 1024;
constexpr qint64 MaxEntryBytes = 2 * 1024 * 1024;
constexpr int MaxIdLength = 128;
constexpr int MaxNameLength = 64;
constexpr int MaxDescriptionLength = 256;
constexpr int MaxIconLength = 128;
constexpr int MaxCategoryLength = 64;
constexpr int MaxEntryDepth = 8;
constexpr int MaxOrder = 1000000;
constexpr int MaxSearchEntries = 16;
constexpr int MaxSearchTitleLength = 64;
constexpr int MaxSearchDetailLength = 128;

bool hasControlCharacters(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f)
            return true;
    }
    return false;
}

bool isInside(const QString &canonicalRoot, const QString &canonicalPath)
{
    const QString prefix = canonicalRoot.endsWith(QLatin1Char('/'))
        ? canonicalRoot : canonicalRoot + QLatin1Char('/');
    return canonicalPath.startsWith(prefix) && canonicalPath.size() > prefix.size();
}

bool isLink(const QString &path)
{
    struct stat status;
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0)
        return false;
    return S_ISLNK(status.st_mode);
}

bool exists(const QString &path)
{
    struct stat status;
    return ::lstat(QFile::encodeName(path).constData(), &status) == 0;
}

bool readRegularFile(const QString &path, QByteArray &contents, QString &reason,
                     qint64 limit = MaxMetadataBytes)
{
    const int descriptor = ::open(QFile::encodeName(path).constData(),
                                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        reason = QStringLiteral("cannot open %1 without following links").arg(QFileInfo(path).fileName());
        return false;
    }
    struct stat status;
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        ::close(descriptor);
        reason = QStringLiteral("%1 is not a regular file").arg(QFileInfo(path).fileName());
        return false;
    }
    if (status.st_size > limit) {
        ::close(descriptor);
        reason = QStringLiteral("%1 is larger than %2 bytes")
                     .arg(QFileInfo(path).fileName()).arg(limit);
        return false;
    }
    contents.clear();
    char buffer[4096];
    for (;;) {
        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
        if (count < 0) {
            ::close(descriptor);
            reason = QStringLiteral("cannot read %1").arg(QFileInfo(path).fileName());
            return false;
        }
        if (count == 0)
            break;
        contents.append(buffer, static_cast<qsizetype>(count));
        if (contents.size() > limit) {
            ::close(descriptor);
            reason = QStringLiteral("%1 is larger than %2 bytes")
                         .arg(QFileInfo(path).fileName()).arg(limit);
            return false;
        }
    }
    ::close(descriptor);
    return true;
}

bool regularFileExists(const QString &path)
{
    const int descriptor = ::open(QFile::encodeName(path).constData(),
                                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0)
        return false;
    struct stat status;
    const bool regular = ::fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode);
    ::close(descriptor);
    return regular;
}

bool readString(const QJsonObject &object, const QString &key, bool required, int maxLength,
                QString &value, QString &reason)
{
    const QJsonValue field = object.value(key);
    if (field.isUndefined()) {
        if (required) {
            reason = QStringLiteral("missing required field \"%1\"").arg(key);
            return false;
        }
        value.clear();
        return true;
    }
    if (!field.isString()) {
        reason = QStringLiteral("field \"%1\" must be a string").arg(key);
        return false;
    }
    value = field.toString().trimmed();
    if (required && value.isEmpty()) {
        reason = QStringLiteral("field \"%1\" must not be empty").arg(key);
        return false;
    }
    if (value.size() > maxLength) {
        reason = QStringLiteral("field \"%1\" is longer than %2 characters").arg(key).arg(maxLength);
        return false;
    }
    if (hasControlCharacters(value)) {
        reason = QStringLiteral("field \"%1\" contains control characters").arg(key);
        return false;
    }
    return true;
}

bool readInteger(const QJsonObject &object, const QString &key, bool required, int fallback,
                 int &value, QString &reason)
{
    const QJsonValue field = object.value(key);
    if (field.isUndefined()) {
        if (required) {
            reason = QStringLiteral("missing required field \"%1\"").arg(key);
            return false;
        }
        value = fallback;
        return true;
    }
    if (!field.isDouble()) {
        reason = QStringLiteral("field \"%1\" must be an integer").arg(key);
        return false;
    }
    const double number = field.toDouble();
    if (!std::isfinite(number) || number != std::floor(number) || std::fabs(number) > MaxOrder) {
        reason = QStringLiteral("field \"%1\" must be an integer within range").arg(key);
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

bool readSearchEntries(const QJsonObject &object, QVariantList &entries, QString &reason)
{
    entries.clear();
    const QJsonValue field = object.value(QStringLiteral("search"));
    if (field.isUndefined())
        return true;
    if (!field.isArray()) {
        reason = QStringLiteral("field \"search\" must be an array");
        return false;
    }
    const QJsonArray array = field.toArray();
    if (array.size() > MaxSearchEntries) {
        reason = QStringLiteral("field \"search\" has more than %1 entries").arg(MaxSearchEntries);
        return false;
    }
    static const QRegularExpression keyPattern(QStringLiteral("^[a-z][a-z0-9-]{0,31}$"));
    QSet<QString> keys;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            reason = QStringLiteral("every search entry must be an object");
            return false;
        }
        const QJsonObject entry = value.toObject();
        QString key;
        QString title;
        QString detail;
        if (!readString(entry, QStringLiteral("key"), true, 32, key, reason)
            || !readString(entry, QStringLiteral("title"), true, MaxSearchTitleLength, title, reason)
            || !readString(entry, QStringLiteral("detail"), false, MaxSearchDetailLength, detail, reason))
            return false;
        if (!keyPattern.match(key).hasMatch()) {
            reason = QStringLiteral("invalid search entry key \"%1\"").arg(key);
            return false;
        }
        if (keys.contains(key)) {
            reason = QStringLiteral("duplicate search entry key \"%1\"").arg(key);
            return false;
        }
        keys.insert(key);
        entries.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("title"), title},
                                   {QStringLiteral("detail"), detail}});
    }
    return true;
}

bool validEntryPath(const QString &entry, QStringList &components, QString &reason)
{
    if (entry.isEmpty() || entry.startsWith(QLatin1Char('/')) || entry.contains(QLatin1Char('\\'))
        || entry.contains(QLatin1Char('\0')) || hasControlCharacters(entry)) {
        reason = QStringLiteral("entry path is not a plain relative path");
        return false;
    }
    components = entry.split(QLatin1Char('/'));
    if (components.size() > MaxEntryDepth) {
        reason = QStringLiteral("entry path is nested too deeply");
        return false;
    }
    for (const QString &component : std::as_const(components)) {
        if (component.isEmpty() || component == QLatin1String(".") || component == QLatin1String("..")) {
            reason = QStringLiteral("entry path escapes the module directory");
            return false;
        }
    }
    if (!components.last().endsWith(QLatin1String(".qml")) || components.last().size() <= 4) {
        reason = QStringLiteral("entry must be a .qml file");
        return false;
    }
    return true;
}

}

SettingsModuleRegistry::SettingsModuleRegistry(const QString &moduleRoot, QObject *parent)
    : QAbstractListModel(parent)
    , m_moduleRoot(moduleRoot)
{
    m_entries = scan(m_moduleRoot);
}

SettingsModuleRegistry *SettingsModuleRegistry::shared()
{
    static SettingsModuleRegistry *instance = new SettingsModuleRegistry(defaultModuleRoot(), QCoreApplication::instance());
    return instance;
}

SettingsModuleRegistry *SettingsModuleRegistry::create(QQmlEngine *, QJSEngine *)
{
    SettingsModuleRegistry *instance = shared();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

QString SettingsModuleRegistry::defaultModuleRoot()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/../resources/Modules"));
}

bool SettingsModuleRegistry::isValidModuleId(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*){2,}$"));
    return id.size() <= MaxIdLength && pattern.match(id).hasMatch();
}

QAbstractItemModel *SettingsModuleRegistry::model()
{
    return this;
}

int SettingsModuleRegistry::count() const
{
    return static_cast<int>(m_entries.size());
}

QString SettingsModuleRegistry::moduleRoot() const
{
    return m_moduleRoot;
}

void SettingsModuleRegistry::setModuleRoot(const QString &moduleRoot)
{
    if (m_moduleRoot == moduleRoot)
        return;
    m_moduleRoot = moduleRoot;
    emit moduleRootChanged();
    reload();
}

void SettingsModuleRegistry::reload()
{
    const int before = count();
    QList<Entry> entries = scan(m_moduleRoot);
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    if (count() != before)
        emit countChanged();
    emit reloaded();
}

QUrl SettingsModuleRegistry::entryUrl(const QString &id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return entry.entryUrl;
    }
    return QUrl();
}

QVariantMap SettingsModuleRegistry::toMap(const Entry &entry)
{
    return {
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("description"), entry.description},
        {QStringLiteral("icon"), entry.icon},
        {QStringLiteral("category"), entry.category},
        {QStringLiteral("order"), entry.order},
        {QStringLiteral("apiVersion"), entry.apiVersion},
        {QStringLiteral("entryUrl"), entry.entryUrl},
        {QStringLiteral("searchEntries"), entry.search}
    };
}

QVariantMap SettingsModuleRegistry::module(const QString &id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return toMap(entry);
    }
    return QVariantMap();
}

QVariantList SettingsModuleRegistry::modules() const
{
    QVariantList result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries)
        result.append(toMap(entry));
    return result;
}

bool SettingsModuleRegistry::revalidate(const QString &id, ValidatedModule &validated,
                                        QString &reason) const
{
    const Entry *known = nullptr;
    for (const Entry &entry : m_entries) {
        if (entry.id == id) {
            known = &entry;
            break;
        }
    }
    if (!known) {
        reason = QStringLiteral("unknown module \"%1\"").arg(id);
        return false;
    }
    const QFileInfo rootInfo(m_moduleRoot);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        reason = QStringLiteral("module root is no longer available");
        return false;
    }
    if (isLink(known->directory)) {
        reason = QStringLiteral("module directory is now a symbolic link");
        return false;
    }
    Entry fresh;
    QByteArray source;
    if (!load(canonicalRoot, known->directory, fresh, reason, &source))
        return false;
    if (fresh.id != known->id || fresh.apiVersion != known->apiVersion
        || fresh.entryUrl != known->entryUrl) {
        reason = QStringLiteral("module metadata changed since discovery");
        return false;
    }
    validated.directory = QFileInfo(known->directory).canonicalFilePath();
    validated.entryUrl = fresh.entryUrl;
    validated.entrySource = source;
    return true;
}

int SettingsModuleRegistry::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant SettingsModuleRegistry::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= count())
        return QVariant();
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case IdRole: return entry.id;
    case DescriptionRole: return entry.description;
    case IconRole: return entry.icon;
    case CategoryRole: return entry.category;
    case OrderRole: return entry.order;
    case ApiVersionRole: return entry.apiVersion;
    case EntryUrlRole: return entry.entryUrl;
    default: return QVariant();
    }
}

QHash<int, QByteArray> SettingsModuleRegistry::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {IconRole, "icon"},
        {CategoryRole, "category"},
        {OrderRole, "order"},
        {ApiVersionRole, "apiVersion"},
        {EntryUrlRole, "entryUrl"}
    };
}

QList<SettingsModuleRegistry::Entry> SettingsModuleRegistry::scan(const QString &root)
{
    QList<Entry> accepted;
    if (root.isEmpty()) {
        emit scanFailed(root, QStringLiteral("no module root is configured"));
        return accepted;
    }
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        emit scanFailed(root, QStringLiteral("module root does not exist or is not a directory"));
        return accepted;
    }
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        emit scanFailed(root, QStringLiteral("module root cannot be resolved"));
        return accepted;
    }
    const QDir rootDirectory(canonicalRoot);
    if (!rootDirectory.isReadable()) {
        emit scanFailed(root, QStringLiteral("module root is not readable"));
        return accepted;
    }
    const QFileInfoList children = rootDirectory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    QHash<QString, QString> owners;
    for (const QFileInfo &child : children) {
        const QString path = child.absoluteFilePath();
        if (child.fileName().startsWith(QLatin1Char('.')))
            continue;
        if (isLink(path)) {
            emit moduleRejected(path, QStringLiteral("symbolic links are not allowed"));
            continue;
        }
        if (!child.isDir())
            continue;
        Entry entry;
        QString reason;
        if (!load(canonicalRoot, path, entry, reason)) {
            emit moduleRejected(path, reason);
            continue;
        }
        const auto owner = owners.constFind(entry.id);
        if (owner != owners.constEnd()) {
            emit moduleRejected(path, QStringLiteral("duplicate module id \"%1\" already provided by %2")
                                          .arg(entry.id, *owner));
            continue;
        }
        owners.insert(entry.id, child.fileName());
        accepted.append(entry);
    }
    std::stable_sort(accepted.begin(), accepted.end(), [](const Entry &left, const Entry &right) {
        if (left.order != right.order)
            return left.order < right.order;
        const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return left.id < right.id;
    });
    return accepted;
}

bool SettingsModuleRegistry::load(const QString &canonicalRoot, const QString &directory,
                                  Entry &entry, QString &reason, QByteArray *entrySource) const
{
    const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    if (canonicalDirectory.isEmpty() || !isInside(canonicalRoot, canonicalDirectory)) {
        reason = QStringLiteral("module directory resolves outside the module root");
        return false;
    }

    const QString metadataPath = canonicalDirectory + QStringLiteral("/module.json");
    if (!exists(metadataPath)) {
        reason = QStringLiteral("module.json is missing");
        return false;
    }
    if (isLink(metadataPath)) {
        reason = QStringLiteral("module.json must not be a symbolic link");
        return false;
    }
    QByteArray contents;
    if (!readRegularFile(metadataPath, contents, reason))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        reason = QStringLiteral("module.json is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!document.isObject()) {
        reason = QStringLiteral("module.json must contain a JSON object");
        return false;
    }
    const QJsonObject object = document.object();

    QString id;
    if (!readString(object, QStringLiteral("id"), true, MaxIdLength, id, reason))
        return false;
    if (object.value(QStringLiteral("id")).toString() != id || !isValidModuleId(id)) {
        reason = QStringLiteral("invalid module id \"%1\"").arg(id);
        return false;
    }
    if (!readString(object, QStringLiteral("name"), true, MaxNameLength, entry.name, reason)
        || !readString(object, QStringLiteral("description"), false, MaxDescriptionLength,
                       entry.description, reason)
        || !readString(object, QStringLiteral("category"), false, MaxCategoryLength,
                       entry.category, reason)
        || !readString(object, QStringLiteral("icon"), false, MaxIconLength, entry.icon, reason))
        return false;
    static const QRegularExpression iconPattern(QStringLiteral("^[A-Za-z0-9._-]*$"));
    if (!iconPattern.match(entry.icon).hasMatch()) {
        reason = QStringLiteral("field \"icon\" must be an icon name");
        return false;
    }

    int apiVersion = 0;
    if (!readInteger(object, QStringLiteral("apiVersion"), true, 0, apiVersion, reason))
        return false;
    if (apiVersion < 1) {
        reason = QStringLiteral("invalid API version %1").arg(apiVersion);
        return false;
    }
    if (apiVersion != SupportedApiVersion) {
        reason = QStringLiteral("unsupported API version %1 (supported: %2)")
                     .arg(apiVersion).arg(SupportedApiVersion);
        return false;
    }
    int order = DefaultOrder;
    if (!readInteger(object, QStringLiteral("order"), false, DefaultOrder, order, reason))
        return false;

    QVariantList search;
    if (!readSearchEntries(object, search, reason))
        return false;

    QString entryName = QStringLiteral("MainModule.qml");
    if (object.contains(QStringLiteral("entry"))
        && !readString(object, QStringLiteral("entry"), true, 256, entryName, reason))
        return false;
    QStringList components;
    if (!validEntryPath(entryName, components, reason))
        return false;

    QString walked = canonicalDirectory;
    for (int index = 0; index < components.size(); ++index) {
        walked += QLatin1Char('/') + components.at(index);
        if (!exists(walked)) {
            reason = QStringLiteral("entry file %1 is missing").arg(entryName);
            return false;
        }
        if (isLink(walked)) {
            reason = QStringLiteral("entry path must not contain symbolic links");
            return false;
        }
    }
    if (entrySource) {
        if (!readRegularFile(walked, *entrySource, reason, MaxEntryBytes))
            return false;
    } else if (!regularFileExists(walked)) {
        reason = QStringLiteral("entry file %1 is not a regular file").arg(entryName);
        return false;
    }
    const QString canonicalEntry = QFileInfo(walked).canonicalFilePath();
    if (canonicalEntry.isEmpty() || !isInside(canonicalDirectory, canonicalEntry)) {
        reason = QStringLiteral("entry file resolves outside the module directory");
        return false;
    }

    entry.id = id;
    entry.order = order;
    entry.apiVersion = apiVersion;
    entry.entryUrl = QUrl::fromLocalFile(canonicalEntry);
    entry.search = search;
    entry.directory = canonicalDirectory;
    return true;
}
