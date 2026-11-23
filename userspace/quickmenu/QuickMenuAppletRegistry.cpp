#include "QuickMenuAppletRegistry.h"

#include "DesktopEntry.h"

#include <OpenWave/application-data.h>
#include <OpenWave/preferences.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr qint64 MaxMetadataBytes = 64 * 1024;
constexpr qint64 MaxEntryBytes = 1024 * 1024;
constexpr int MaxNameLength = 64;
constexpr int MaxDescriptionLength = 256;
constexpr int MaxPathLength = 256;
constexpr int MaxPathDepth = 8;
constexpr int MaxOrder = 1000000;
constexpr int DebounceMilliseconds = 150;

const QString EntryFile = QStringLiteral("MainApplet.qml");
const QString MetadataFile = QStringLiteral("applet.desktop");
const QString OrderKey = QStringLiteral("quickmenu/applets/order");
const QString StatePrefix = QStringLiteral("quickmenu/applets/state/");
const QString StateGroup = QStringLiteral("quickmenu/applets");

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

bool readRegularFile(const QString &path, QByteArray *contents, QString &reason, qint64 limit)
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
        reason = QStringLiteral("%1 is larger than %2 bytes").arg(QFileInfo(path).fileName()).arg(limit);
        return false;
    }
    if (contents) {
        contents->clear();
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
            contents->append(buffer, static_cast<qsizetype>(count));
            if (contents->size() > limit) {
                ::close(descriptor);
                reason = QStringLiteral("%1 is larger than %2 bytes")
                             .arg(QFileInfo(path).fileName()).arg(limit);
                return false;
            }
        }
    }
    ::close(descriptor);
    return true;
}

bool resolveRelative(const QString &canonicalDirectory, const QString &relative,
                     const QStringList &suffixes, QString &absolute, QString &reason)
{
    if (relative.isEmpty() || relative.size() > MaxPathLength || relative.startsWith(QLatin1Char('/'))
        || relative.contains(QLatin1Char('\\')) || hasControlCharacters(relative)) {
        reason = QStringLiteral("\"%1\" is not a plain relative path").arg(relative);
        return false;
    }
    const QStringList components = relative.split(QLatin1Char('/'));
    if (components.size() > MaxPathDepth) {
        reason = QStringLiteral("\"%1\" is nested too deeply").arg(relative);
        return false;
    }
    for (const QString &component : components) {
        if (component.isEmpty() || component == QLatin1String(".") || component == QLatin1String("..")) {
            reason = QStringLiteral("\"%1\" escapes the applet directory").arg(relative);
            return false;
        }
    }
    bool suffixMatches = false;
    for (const QString &suffix : suffixes)
        suffixMatches = suffixMatches || components.last().endsWith(suffix, Qt::CaseInsensitive);
    if (!suffixMatches) {
        reason = QStringLiteral("\"%1\" has an unsupported file type").arg(relative);
        return false;
    }
    QString walked = canonicalDirectory;
    for (const QString &component : components) {
        walked += QLatin1Char('/') + component;
        if (!exists(walked)) {
            reason = QStringLiteral("\"%1\" does not exist").arg(relative);
            return false;
        }
        if (isLink(walked)) {
            reason = QStringLiteral("\"%1\" must not contain symbolic links").arg(relative);
            return false;
        }
    }
    if (!readRegularFile(walked, nullptr, reason, MaxEntryBytes))
        return false;
    const QString canonical = QFileInfo(walked).canonicalFilePath();
    if (canonical.isEmpty() || !isInside(canonicalDirectory, canonical)) {
        reason = QStringLiteral("\"%1\" resolves outside the applet directory").arg(relative);
        return false;
    }
    absolute = canonical;
    return true;
}

int kindRank(QuickMenuAppletRegistry::Kind kind)
{
    return static_cast<int>(kind);
}

bool commit(QSettings &settings)
{
    settings.sync();
    return settings.status() == QSettings::NoError;
}

}

QuickMenuAppletRegistry::QuickMenuAppletRegistry(const QList<Root> &roots,
                                                 const QString &preferencesPath, QObject *parent)
    : QAbstractListModel(parent)
    , m_roots(roots)
    , m_preferencesPath(preferencesPath)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DebounceMilliseconds);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        const bool rescan = m_pendingRescan;
        m_pendingRescan = false;
        if (rescan)
            reload();
        else
            reloadState();
        watchPaths();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        scheduleReload(false);
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        bool isRoot = false;
        for (const Root &root : std::as_const(m_roots))
            isRoot = isRoot || root.path == path;
        scheduleReload(isRoot);
    });
    reload();
    watchPaths();
}

QuickMenuAppletRegistry *QuickMenuAppletRegistry::shared()
{
    static QuickMenuAppletRegistry *instance = new QuickMenuAppletRegistry(
        defaultRoots(), openwavePreferencesPath(), QCoreApplication::instance());
    return instance;
}

QuickMenuAppletRegistry *QuickMenuAppletRegistry::create(QQmlEngine *, QJSEngine *)
{
    QuickMenuAppletRegistry *instance = shared();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

QList<QuickMenuAppletRegistry::Root> QuickMenuAppletRegistry::defaultRoots()
{
    QList<Root> roots{
        {Needed, QStringLiteral("/System/QuickMenu/Applets/Needed")},
        {System, QStringLiteral("/System/QuickMenu/Applets")}
    };
    const QString user = openwaveUserDataPath();
    if (!user.isEmpty())
        roots.append({User, QDir(user).filePath(QStringLiteral(".DataLocation/QuickMenu/Applets"))});
    return roots;
}

bool QuickMenuAppletRegistry::isValidAppletId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,63}$"));
    return pattern.match(id).hasMatch();
}

QString QuickMenuAppletRegistry::kindName(Kind kind)
{
    switch (kind) {
    case Needed: return QStringLiteral("needed");
    case System: return QStringLiteral("system");
    case User: return QStringLiteral("user");
    }
    return QStringLiteral("user");
}

int QuickMenuAppletRegistry::count() const
{
    return static_cast<int>(m_entries.size());
}

QVariantList QuickMenuAppletRegistry::panelApplets() const
{
    QVariantList result;
    for (const Entry &entry : m_entries) {
        if (entry.enabled && entry.loadError.isEmpty())
            result.append(toMap(entry));
    }
    return result;
}

QVariantList QuickMenuAppletRegistry::residentApplets() const
{
    QVariantList result;
    for (const Entry &entry : m_entries) {
        if (entry.kind == Needed && !entry.enabled && entry.loadError.isEmpty())
            result.append(toMap(entry));
    }
    return result;
}

QVariantList QuickMenuAppletRegistry::rejections() const
{
    return m_rejections;
}

QStringList QuickMenuAppletRegistry::orderedIds() const
{
    QStringList ids;
    for (const Entry &entry : m_entries)
        ids.append(entry.id);
    return ids;
}

QUrl QuickMenuAppletRegistry::entryUrl(const QString &id)
{
    const Entry *known = nullptr;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.id == id) {
            known = &entry;
            break;
        }
    }
    if (!known)
        return QUrl();
    const QFileInfo rootInfo(known->rootPath);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    QString reason;
    Entry fresh;
    if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
        reason = QStringLiteral("applet root is no longer available");
    } else if (isLink(known->directory)) {
        reason = QStringLiteral("applet directory is now a symbolic link");
    } else if (!load(canonicalRoot, known->directory, known->kind, fresh, reason)) {
    } else if (fresh.id != known->id || fresh.entryUrl != known->entryUrl
               || fresh.apiVersion != known->apiVersion) {
        reason = QStringLiteral("applet changed since discovery");
    } else {
        return fresh.entryUrl;
    }
    reportLoadFailure(id, reason);
    return QUrl();
}

QVariantMap QuickMenuAppletRegistry::applet(const QString &id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return toMap(entry);
    }
    return QVariantMap();
}

void QuickMenuAppletRegistry::reportLoadFailure(const QString &id, const QString &reason)
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id != id)
            continue;
        m_entries[row].loadError = reason.isEmpty() ? QStringLiteral("failed to load") : reason;
        const QModelIndex index = createIndex(row, 0);
        emit dataChanged(index, index);
        emit changed();
        emit loadFailed(id, m_entries.at(row).loadError);
        return;
    }
}

QUrl QuickMenuAppletRegistry::settingsUrl(const QString &id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return entry.settingsUrl;
    }
    return QUrl();
}

bool QuickMenuAppletRegistry::setEnabled(const QString &id, bool enabled)
{
    bool known = false;
    for (const Entry &entry : std::as_const(m_entries))
        known = known || entry.id == id;
    if (!known)
        return false;
    if (!store(StatePrefix + id, enabled))
        return false;
    reloadState();
    return true;
}

bool QuickMenuAppletRegistry::move(const QString &id, int toIndex)
{
    QStringList ids = orderedIds();
    const int from = static_cast<int>(ids.indexOf(id));
    if (from < 0)
        return false;
    ids.removeAt(from);
    ids.insert(qBound(0, toIndex, static_cast<int>(ids.size())), id);
    if (!store(OrderKey, ids))
        return false;
    reloadState();
    return true;
}

bool QuickMenuAppletRegistry::resetState()
{
    if (!store(StateGroup, QVariant()))
        return false;
    reloadState();
    return true;
}

void QuickMenuAppletRegistry::setRoots(const QList<Root> &roots)
{
    m_roots = roots;
    reload();
    watchPaths();
}

void QuickMenuAppletRegistry::setPreferenceWriter(PreferenceWriter writer)
{
    m_writer = std::move(writer);
}

bool QuickMenuAppletRegistry::store(const QString &key, const QVariant &value)
{
    if (m_writer)
        return m_writer(key, value);
    QSettings settings(m_preferencesPath, QSettings::IniFormat);
    if (value.isValid())
        settings.setValue(key, value);
    else
        settings.remove(key);
    return commit(settings);
}

void QuickMenuAppletRegistry::setPreferencesPath(const QString &path)
{
    m_preferencesPath = path;
    reloadState();
    watchPaths();
}

void QuickMenuAppletRegistry::reload()
{
    QVariantList rejected;
    QList<Entry> entries = scan(rejected);
    applyState(entries);
    const bool rejectionsChanged = m_rejections != rejected;
    m_rejections = rejected;
    for (const QVariant &item : std::as_const(rejected)) {
        const QVariantMap map = item.toMap();
        emit appletRejected(map.value(QStringLiteral("path")).toString(),
                            map.value(QStringLiteral("reason")).toString());
    }
    if (!publish(std::move(entries), false) && rejectionsChanged)
        emit changed();
}

void QuickMenuAppletRegistry::reloadState()
{
    QList<Entry> entries = m_entries;
    applyState(entries);
    publish(std::move(entries), true);
}

int QuickMenuAppletRegistry::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant QuickMenuAppletRegistry::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= count())
        return QVariant();
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case IdRole: return entry.id;
    case DescriptionRole: return entry.description;
    case IconRole: return entry.icon.isEmpty() ? QUrl() : QUrl::fromLocalFile(entry.icon);
    case KindRole: return kindName(entry.kind);
    case RequiredRole: return entry.kind == Needed;
    case EnabledRole: return entry.enabled;
    case OrderRole: return entry.order;
    case ApiVersionRole: return entry.apiVersion;
    case EntryUrlRole: return entry.entryUrl;
    case SettingsUrlRole: return entry.settingsUrl;
    case LoadErrorRole: return entry.loadError;
    default: return QVariant();
    }
}

QHash<int, QByteArray> QuickMenuAppletRegistry::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {IconRole, "icon"},
        {KindRole, "kind"},
        {RequiredRole, "required"},
        {EnabledRole, "enabled"},
        {OrderRole, "order"},
        {ApiVersionRole, "apiVersion"},
        {EntryUrlRole, "entryUrl"},
        {SettingsUrlRole, "settingsUrl"},
        {LoadErrorRole, "loadError"}
    };
}

QVariantMap QuickMenuAppletRegistry::toMap(const Entry &entry)
{
    return {
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("description"), entry.description},
        {QStringLiteral("icon"), entry.icon.isEmpty() ? QUrl() : QUrl::fromLocalFile(entry.icon)},
        {QStringLiteral("kind"), kindName(entry.kind)},
        {QStringLiteral("required"), entry.kind == Needed},
        {QStringLiteral("enabled"), entry.enabled},
        {QStringLiteral("order"), entry.order},
        {QStringLiteral("apiVersion"), entry.apiVersion},
        {QStringLiteral("entryUrl"), entry.entryUrl},
        {QStringLiteral("settingsUrl"), entry.settingsUrl},
        {QStringLiteral("loadError"), entry.loadError}
    };
}

QList<QuickMenuAppletRegistry::Entry> QuickMenuAppletRegistry::scan(QVariantList &rejected)
{
    QList<Entry> accepted;
    QHash<QString, QString> owners;
    const auto reject = [&rejected](const QString &path, const QString &reason) {
        rejected.append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("reason"), reason}});
    };
    for (const Root &root : std::as_const(m_roots)) {
        const QFileInfo rootInfo(root.path);
        if (!rootInfo.exists() || !rootInfo.isDir())
            continue;
        const QString canonicalRoot = rootInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty() || !QDir(canonicalRoot).isReadable())
            continue;
        const QFileInfoList children = QDir(canonicalRoot).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
        for (const QFileInfo &child : children) {
            const QString path = child.absoluteFilePath();
            if (child.fileName().startsWith(QLatin1Char('.')))
                continue;
            if (isLink(path)) {
                reject(path, QStringLiteral("symbolic links are not allowed"));
                continue;
            }
            if (!child.isDir() || !exists(path + QLatin1Char('/') + EntryFile))
                continue;
            Entry entry;
            QString reason;
            if (!load(canonicalRoot, path, root.kind, entry, reason)) {
                reject(path, reason);
                continue;
            }
            entry.rootPath = root.path;
            const auto owner = owners.constFind(entry.id);
            if (owner != owners.constEnd()) {
                reject(path, QStringLiteral("applet id \"%1\" is already provided by %2")
                                 .arg(entry.id, *owner));
                continue;
            }
            owners.insert(entry.id, path);
            accepted.append(entry);
        }
    }
    return accepted;
}

bool QuickMenuAppletRegistry::load(const QString &canonicalRoot, const QString &directory, Kind kind,
                                   Entry &entry, QString &reason) const
{
    const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    if (canonicalDirectory.isEmpty() || !isInside(canonicalRoot, canonicalDirectory)) {
        reason = QStringLiteral("applet directory resolves outside its root");
        return false;
    }
    const QString id = QFileInfo(canonicalDirectory).fileName();
    if (!isValidAppletId(id)) {
        reason = QStringLiteral("invalid applet id \"%1\"").arg(id);
        return false;
    }

    QString entryPath;
    if (!resolveRelative(canonicalDirectory, EntryFile, {QStringLiteral(".qml")}, entryPath, reason))
        return false;

    entry.id = id;
    entry.name = id;
    entry.kind = kind;
    entry.order = DefaultOrder;
    entry.apiVersion = 1;
    entry.directory = canonicalDirectory;
    entry.entryUrl = QUrl::fromLocalFile(entryPath);

    const QString metadataPath = canonicalDirectory + QLatin1Char('/') + MetadataFile;
    if (!exists(metadataPath))
        return true;
    if (isLink(metadataPath)) {
        reason = QStringLiteral("applet.desktop must not be a symbolic link");
        return false;
    }
    QByteArray contents;
    if (!readRegularFile(metadataPath, &contents, reason, MaxMetadataBytes))
        return false;
    const DesktopEntry::Fields fields = DesktopEntry::parse(contents);
    if (fields.isEmpty()) {
        reason = QStringLiteral("applet.desktop has no [Desktop Entry] group");
        return false;
    }
    if (fields.value(QStringLiteral("Type")) != QLatin1String("X-OpenWave-Applet")) {
        reason = QStringLiteral("Type must be X-OpenWave-Applet");
        return false;
    }

    const QString prefix = QStringLiteral("X-OpenWave-Applet-");
    const QString name = DesktopEntry::localized(fields, QStringLiteral("Name")).trimmed();
    const QString description = DesktopEntry::localized(fields, QStringLiteral("Comment")).trimmed();
    const QString iconPath = fields.value(QStringLiteral("Icon")).trimmed();
    const QString settingsPath = fields.value(prefix + QStringLiteral("Settings")).trimmed();
    int apiVersion = 1;
    int order = DefaultOrder;
    if (name.size() > MaxNameLength || description.size() > MaxDescriptionLength
        || hasControlCharacters(name) || hasControlCharacters(description)) {
        reason = QStringLiteral("Name or Comment is too long or contains control characters");
        return false;
    }
    if (!DesktopEntry::readInteger(fields, prefix + QStringLiteral("ApiVersion"), std::numeric_limits<int>::min(),
                                   std::numeric_limits<int>::max(), apiVersion, reason)
        || !DesktopEntry::readInteger(fields, prefix + QStringLiteral("Order"), -MaxOrder, MaxOrder, order, reason))
        return false;
    if (apiVersion < 1) {
        reason = QStringLiteral("invalid API version %1").arg(apiVersion);
        return false;
    }
    if (apiVersion > SupportedApiVersion) {
        reason = QStringLiteral("incompatible API version %1 (supported: %2)")
                     .arg(apiVersion).arg(SupportedApiVersion);
        return false;
    }
    if (!name.isEmpty())
        entry.name = name;
    entry.description = description;
    entry.apiVersion = apiVersion;
    entry.order = order;
    if (!iconPath.isEmpty()) {
        QString absolute;
        if (!resolveRelative(canonicalDirectory, iconPath,
                             {QStringLiteral(".svg"), QStringLiteral(".png")}, absolute, reason))
            return false;
        entry.icon = absolute;
    }
    if (!settingsPath.isEmpty() && kind != User) {
        QString absolute;
        if (!resolveRelative(canonicalDirectory, settingsPath, {QStringLiteral(".qml")}, absolute, reason))
            return false;
        entry.settingsUrl = QUrl::fromLocalFile(absolute);
    }
    return true;
}

bool QuickMenuAppletRegistry::defaultEnabled(Kind kind) const
{
    return kind != User;
}

void QuickMenuAppletRegistry::applyState(QList<Entry> &entries) const
{
    QSettings settings(m_preferencesPath, QSettings::IniFormat);
    for (Entry &entry : entries) {
        const QString key = StatePrefix + entry.id;
        entry.enabled = settings.contains(key) ? settings.value(key).toBool() : defaultEnabled(entry.kind);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        if (kindRank(left.kind) != kindRank(right.kind))
            return kindRank(left.kind) < kindRank(right.kind);
        if (left.order != right.order)
            return left.order < right.order;
        const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return left.id < right.id;
    });
    const QStringList saved = settings.value(OrderKey).toStringList();
    if (saved.isEmpty())
        return;
    const auto position = [&saved](const Entry &entry) {
        const qsizetype index = saved.indexOf(entry.id);
        return index < 0 ? std::numeric_limits<qsizetype>::max() : index;
    };
    std::stable_sort(entries.begin(), entries.end(), [&position](const Entry &left, const Entry &right) {
        return position(left) < position(right);
    });
}

void QuickMenuAppletRegistry::watchPaths()
{
    QStringList files;
    QStringList directories;
    if (QFile::exists(m_preferencesPath))
        files.append(m_preferencesPath);
    const QString preferencesDirectory = QFileInfo(m_preferencesPath).absolutePath();
    if (QDir(preferencesDirectory).exists())
        directories.append(preferencesDirectory);
    for (const Root &root : std::as_const(m_roots)) {
        if (!root.path.isEmpty() && QDir(root.path).exists())
            directories.append(root.path);
    }
    for (const QString &file : std::as_const(files)) {
        if (!m_watcher.files().contains(file))
            m_watcher.addPath(file);
    }
    for (const QString &directory : std::as_const(directories)) {
        if (!m_watcher.directories().contains(directory))
            m_watcher.addPath(directory);
    }
}

void QuickMenuAppletRegistry::scheduleReload(bool rescan)
{
    m_pendingRescan = m_pendingRescan || rescan;
    m_debounce.start();
}

bool QuickMenuAppletRegistry::publish(QList<Entry> entries, bool keepFailures)
{
    if (keepFailures) {
        for (Entry &entry : entries) {
            for (const Entry &old : std::as_const(m_entries)) {
                if (old.id == entry.id && old.entryUrl == entry.entryUrl)
                    entry.loadError = old.loadError;
            }
        }
    }
    QVariantList before;
    QVariantList after;
    for (const Entry &entry : std::as_const(m_entries))
        before.append(toMap(entry));
    for (const Entry &entry : std::as_const(entries))
        after.append(toMap(entry));
    if (before == after)
        return false;
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit changed();
    return true;
}
