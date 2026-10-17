#include "FilesBackend.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <utility>
#include <cerrno>
#include <fcntl.h>
#include <dirent.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

namespace {
QString currentUserName()
{
#ifdef Q_OS_UNIX
    if (const passwd *user = getpwuid(geteuid()))
        return QString::fromLocal8Bit(user->pw_name);
#endif
    return QStringLiteral("root");
}

QString sizeLabel(qint64 bytes)
{
    return QLocale::c().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

qint64 folderSize(const QString &path)
{
    qint64 bytes = 0;
    QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        bytes += iterator.fileInfo().size();
    }
    return bytes;
}

int openUserDirectory(const QString &root, const QString &path)
{
    int directory = ::open(QFile::encodeName(root).constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0 || path == root) return directory;
    if (!path.startsWith(root + QLatin1Char('/'))) {
        ::close(directory); return -1;
    }
    for (const QString &part : path.mid(root.size() + 1).split('/')) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) {
            ::close(directory); return -1;
        }
        const int next = ::openat(directory, QFile::encodeName(part).constData(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ::close(directory);
        if (next < 0) return -1;
        directory = next;
    }
    return directory;
}

bool removeEntry(int parent, const QByteArray &name, const struct stat &expected, int depth = 0)
{
    struct stat current;
    if (::fstatat(parent, name.constData(), &current, AT_SYMLINK_NOFOLLOW)
        || current.st_dev != expected.st_dev || current.st_ino != expected.st_ino)
        return false;
    // Unlink symlinks themselves, including broken links; never follow them.
    if (!S_ISDIR(current.st_mode)) return ::unlinkat(parent, name.constData(), 0) == 0;
    if (depth >= 256) return false;
    const int child = ::openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat opened;
    if (child < 0) return false;
    if (::fstat(child, &opened) || opened.st_dev != current.st_dev || opened.st_ino != current.st_ino) {
        ::close(child); return false;
    }
    DIR *stream = ::fdopendir(child);
    if (!stream) { ::close(child); return false; }
    bool ok = true;
    for (;;) {
        errno = 0;
        dirent *entry = ::readdir(stream);
        if (!entry) { ok = errno == 0; break; }
        const QByteArray childName(entry->d_name);
        if (childName == "." || childName == "..") continue;
        struct stat info;
        // Refuse mounted subtrees rather than recursively deleting another volume.
        if (::fstatat(child, childName.constData(), &info, AT_SYMLINK_NOFOLLOW)
            || info.st_dev != current.st_dev || !removeEntry(child, childName, info, depth + 1)) {
            ok = false; break;
        }
    }
    ::closedir(stream);
    if (!ok || ::fstatat(parent, name.constData(), &current, AT_SYMLINK_NOFOLLOW)
        || current.st_dev != expected.st_dev || current.st_ino != expected.st_ino)
        return false;
    return ::unlinkat(parent, name.constData(), AT_REMOVEDIR) == 0;
}

bool copyEntry(int source, int destination, const QByteArray &name, const struct stat &expected, int depth = 0)
{
    if (depth >= 256) return false;
    if (S_ISLNK(expected.st_mode)) {
        char target[4096];
        const ssize_t length = ::readlinkat(source, name.constData(), target, sizeof(target) - 1);
        if (length < 0 || length >= ssize_t(sizeof(target) - 1)) return false;
        target[length] = 0;
        return ::symlinkat(target, destination, name.constData()) == 0;
    }
    if (!S_ISREG(expected.st_mode) && !S_ISDIR(expected.st_mode)) return false;
    const int input = ::openat(source, name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW
                              | (S_ISDIR(expected.st_mode) ? O_DIRECTORY : 0));
    if (input < 0) return false;
    struct stat current;
    if (::fstat(input, &current) || current.st_dev != expected.st_dev || current.st_ino != expected.st_ino) {
        ::close(input); return false;
    }
    int output;
    if (S_ISDIR(current.st_mode)) {
        if (::mkdirat(destination, name.constData(), 0700)) { ::close(input); return false; }
        output = ::openat(destination, name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    } else {
        output = ::openat(destination, name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    }
    if (output < 0) { ::close(input); return false; }
    struct stat created{};
    bool ok = ::fstat(output, &created) == 0;
    if (ok && S_ISDIR(current.st_mode)) {
        DIR *stream = ::fdopendir(input);
        if (!stream) { ::close(input); ok = false; }
        else {
            for (;;) {
                errno = 0;
                dirent *entry = ::readdir(stream);
                if (!entry) { ok = errno == 0; break; }
                const QByteArray child(entry->d_name);
                if (child == "." || child == "..") continue;
                struct stat info;
                if (::fstatat(input, child.constData(), &info, AT_SYMLINK_NOFOLLOW)
                    || info.st_dev != current.st_dev || !copyEntry(input, output, child, info, depth + 1)) {
                    ok = false; break;
                }
            }
            ::closedir(stream);
        }
    } else {
        char buffer[65536];
        while (ok) {
            const ssize_t count = ::read(input, buffer, sizeof(buffer));
            if (count < 0) { if (errno == EINTR) continue; ok = false; break; }
            if (!count) break;
            ssize_t offset = 0;
            while (offset < count) {
                const ssize_t written = ::write(output, buffer + offset, count - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) { ok = false; break; }
                offset += written;
            }
        }
        ::close(input);
    }
    if (ok) ok = ::fchmod(output, current.st_mode & 0777) == 0;
    if (::close(output)) ok = false;
    if (!ok) removeEntry(destination, name, created);
    return ok;
}
}

FilesBackend::FilesBackend(QString userName, QString usersPath, QObject *parent)
    : QAbstractListModel(parent)
{
    if (userName.isEmpty())
        userName = currentUserName();
    if (!validName(userName)) {
        setError(QStringLiteral("The current username is invalid."));
        return;
    }
    m_userName = userName;

    const QString cleanUsersPath = QDir::cleanPath(usersPath);
    const QString requestedRoot = QDir(cleanUsersPath).filePath(userName);
    // A profile path must be a directory, never a redirect to a system path.
    for (const QString &path : {cleanUsersPath.section('/', 0, -2), cleanUsersPath, requestedRoot}) {
        const QFileInfo part(path);
        if (part.isSymbolicLink()) {
            setError(QStringLiteral("The user storage path is a symbolic link."));
            return;
        }
    }
    if (!QDir().mkpath(requestedRoot)) {
        setError(QStringLiteral("Could not create the user storage folder."));
        return;
    }
    m_rootPath = QFileInfo(requestedRoot).canonicalFilePath();
    if (m_rootPath.isEmpty()) {
        setError(QStringLiteral("Could not open the user storage folder."));
        return;
    }
    m_currentPath = m_rootPath;
    for (const char *folder : {"Downloads", "Musics", "Videos", "Documents"}) {
        const QString folderPath = QDir(m_rootPath).filePath(QString::fromLatin1(folder));
        if (QFileInfo(folderPath).isSymbolicLink() || !QDir().mkpath(folderPath))
            setError(QStringLiteral("Could not create one of the default folders."));
    }
    refresh();
}

FilesBackend::~FilesBackend()
{
    cancelDeletion();
    if (m_transferDirectory >= 0) ::close(m_transferDirectory);
}

int FilesBackend::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FilesBackend::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case FolderRole: return entry.folder;
    case DetailRole: return entry.detail;
    case SelectedRole: return entry.selected;
    case AudioRole: return entry.audio;
    default: return {};
    }
}

QHash<int, QByteArray> FilesBackend::roleNames() const
{
    return {{NameRole, "name"}, {FolderRole, "folder"},
            {DetailRole, "detail"}, {SelectedRole, "selected"}, {AudioRole, "audio"}};
}

QString FilesBackend::breadcrumb() const
{
    if (m_currentPath.isEmpty() || m_currentPath == m_rootPath)
        return QStringLiteral("Data");
    const QString relative = QDir(m_rootPath).relativeFilePath(m_currentPath);
    return QStringLiteral("Data > ") + relative.split('/').join(QStringLiteral(" > "));
}

bool FilesBackend::validName(const QString &name) const
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.toUtf8().size() > 255 || name != name.trimmed())
        return false;
    for (const QChar character : name) {
        if (character == QLatin1Char('/') || character == QLatin1Char('\\')
            || character.isNull() || !character.isPrint())
            return false;
    }
    return true;
}

bool FilesBackend::withinRoot(const QString &canonicalPath) const
{
    return !m_rootPath.isEmpty() &&
        (canonicalPath == m_rootPath || canonicalPath.startsWith(m_rootPath + QLatin1Char('/')));
}

QString FilesBackend::itemPath(const QString &name) const
{
    return QDir(m_currentPath).filePath(name);
}

void FilesBackend::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void FilesBackend::clearError()
{
    setError({});
}

void FilesBackend::clearSelection()
{
    m_selected.clear();
    m_selectionMode = false;
}

void FilesBackend::refresh()
{
    QVector<Entry> entries;
    if (!withinRoot(m_currentPath)) {
        beginResetModel();
        m_entries.clear();
        endResetModel();
        setError(QStringLiteral("This location is outside your files."));
        return;
    }
    const QDir directory(m_currentPath);
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_showHidden)
        filters |= QDir::Hidden;
    for (const QFileInfo &info : directory.entryInfoList(filters, QDir::NoSort)) {
        if (info.isSymbolicLink() || !validName(info.fileName())
            || !info.fileName().contains(m_searchQuery, Qt::CaseInsensitive))
            continue;
        const QString canonical = info.canonicalFilePath();
        if (!withinRoot(canonical))
            continue;
        Entry entry;
        entry.name = info.fileName();
        entry.folder = info.isDir();
        entry.audio = info.isFile() && info.suffix().compare(QLatin1String("mp3"), Qt::CaseInsensitive) == 0;
        entry.bytes = info.isFile() ? info.size() : folderSize(info.filePath());
        entry.modified = info.lastModified();
        entry.detail = sizeLabel(entry.bytes);
        entry.selected = m_selected.contains(entry.name);
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [this](const Entry &a, const Entry &b) {
        if (a.folder != b.folder)
            return a.folder;
        if (m_sortMode == NewestSort && a.modified != b.modified)
            return a.modified > b.modified;
        if (m_sortMode == SizeSort && a.bytes != b.bytes)
            return a.bytes > b.bytes;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit viewChanged();
}

void FilesBackend::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query)
        return;
    m_searchQuery = query;
    refresh();
}

void FilesBackend::setShowHidden(bool enabled)
{
    if (m_showHidden == enabled)
        return;
    m_showHidden = enabled;
    refresh();
}

void FilesBackend::setSortMode(int mode)
{
    if (mode < NameSort || mode > SizeSort || m_sortMode == mode)
        return;
    m_sortMode = mode;
    refresh();
}

void FilesBackend::setSelectionMode(bool enabled)
{
    if (m_selectionMode == enabled)
        return;
    m_selectionMode = enabled;
    if (!enabled)
        m_selected.clear();
    refresh();
}

QString FilesBackend::selectedName() const
{
    return m_selected.size() == 1 ? *m_selected.cbegin() : QString();
}

bool FilesBackend::selectedFolder() const
{
    const QString name = selectedName();
    const QFileInfo info(itemPath(name));
    return !name.isEmpty() && !info.isSymbolicLink() && info.isDir();
}

void FilesBackend::activate(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const Entry entry = m_entries.at(row);
    if (m_selectionMode) {
        if (m_selected.contains(entry.name))
            m_selected.remove(entry.name);
        else
            m_selected.insert(entry.name);
        m_entries[row].selected = m_selected.contains(entry.name);
        emit dataChanged(index(row), index(row), {SelectedRole});
        emit viewChanged();
        return;
    }
    const QFileInfo info(itemPath(entry.name));
    if (info.isSymbolicLink() || !withinRoot(info.canonicalFilePath())) {
        setError(QStringLiteral("This item cannot be opened."));
        refresh();
        return;
    }
    if (info.isDir()) {
        m_currentPath = info.canonicalFilePath();
        m_searchQuery.clear();
        clearSelection();
        refresh();
    } else {
        m_previewName = entry.name;
        m_previewPath = info.canonicalFilePath();
        m_previewAudio = entry.audio;
        m_previewDetail = QStringLiteral("%1 · %2")
            .arg(sizeLabel(info.size()), info.suffix().isEmpty()
                ? QStringLiteral("File") : info.suffix().toUpper() + QStringLiteral(" file"));
        emit previewChanged();
    }
}

QString FilesBackend::entryPath(int row) const
{
    if (row < 0 || row >= m_entries.size()) return {};
    const QFileInfo info(itemPath(m_entries.at(row).name));
    if (info.isSymbolicLink()) return {};
    const QString canonical = info.canonicalFilePath();
    return withinRoot(canonical) ? canonical : QString();
}

bool FilesBackend::goToDirectory(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymbolicLink() || !info.isDir()) return false;
    const QString canonical = info.canonicalFilePath();
    if (!withinRoot(canonical)) return false;
    m_currentPath = canonical;
    m_searchQuery.clear();
    clearSelection();
    refresh();
    return true;
}

bool FilesBackend::up()
{
    if (m_previewName.size()) {
        closePreview();
        return true;
    }
    if (m_selectionMode) {
        setSelectionMode(false);
        return true;
    }
    if (m_currentPath == m_rootPath)
        return false;
    const QString parent = QFileInfo(m_currentPath).dir().canonicalPath();
    if (!withinRoot(parent))
        return false;
    m_currentPath = parent;
    m_searchQuery.clear();
    refresh();
    return true;
}

void FilesBackend::goToRoot()
{
    if (m_rootPath.isEmpty())
        return;
    m_currentPath = m_rootPath;
    m_searchQuery.clear();
    clearSelection();
    refresh();
}

bool FilesBackend::createFile(const QString &name)
{
    const QString cleaned = name.trimmed();
    if (!validName(cleaned) || !withinRoot(QFileInfo(m_currentPath).canonicalFilePath())) {
        setError(QStringLiteral("Choose a valid file name."));
        return false;
    }
    QFile file(itemPath(cleaned));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setError(QStringLiteral("Could not create the file. Check its name and storage space."));
        return false;
    }
    file.close();
    clearError();
    refresh();
    return true;
}

bool FilesBackend::fileExists(const QString &name) const
{
    const QString cleaned = name.trimmed();
    if (!validName(cleaned) || !withinRoot(QFileInfo(m_currentPath).canonicalFilePath()))
        return false;
    const QFileInfo file(itemPath(cleaned));
    return file.isFile() && !file.isSymbolicLink() && withinRoot(file.canonicalFilePath());
}

bool FilesBackend::replaceFile(const QString &name, const QString &expectedDirectory)
{
    const QString cleaned = name.trimmed();
    if (m_currentPath != expectedDirectory || !fileExists(cleaned)) {
        setError(QStringLiteral("The file or folder changed. Please try again."));
        return false;
    }
    QSaveFile replacement(itemPath(cleaned));
    replacement.setDirectWriteFallback(false);
    if (!replacement.open(QIODevice::WriteOnly) || !fileExists(cleaned)
        || !replacement.commit()) {
        setError(QStringLiteral("Could not replace the file. The original was preserved."));
        return false;
    }
    clearError();
    refresh();
    return true;
}

bool FilesBackend::createFolder(const QString &name)
{
    const QString cleaned = name.trimmed();
    if (!validName(cleaned) || !withinRoot(QFileInfo(m_currentPath).canonicalFilePath())) {
        setError(QStringLiteral("Choose a valid folder name."));
        return false;
    }
    if (!QDir(m_currentPath).mkdir(cleaned)) {
        setError(QStringLiteral("Could not create the folder. Check its name and storage space."));
        return false;
    }
    clearError();
    refresh();
    return true;
}

bool FilesBackend::renameItem(const QString &oldName, const QString &newName,
                              const QString &expectedDirectory)
{
    const QString cleaned = newName.trimmed();
    if (!validName(oldName) || !validName(cleaned)) {
        setError(QStringLiteral("Choose a valid name."));
        return false;
    }
    if (m_currentPath != expectedDirectory
        || !withinRoot(QFileInfo(m_currentPath).canonicalFilePath())) {
        setError(QStringLiteral("The folder changed. Please try again."));
        return false;
    }
    const int directory = ::open(QFile::encodeName(m_currentPath).constData(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat source;
    const QByteArray original = QFile::encodeName(oldName);
    const QByteArray destination = QFile::encodeName(cleaned);
    if (directory < 0 || ::fstatat(directory, original.constData(), &source, AT_SYMLINK_NOFOLLOW)
        || (!S_ISREG(source.st_mode) && !S_ISDIR(source.st_mode))) {
        if (directory >= 0) ::close(directory);
        setError(QStringLiteral("This item cannot be renamed. Please refresh and try again."));
        return false;
    }
    const bool unchanged = cleaned == oldName;
    const bool renamed = unchanged || ::syscall(SYS_renameat2, directory, original.constData(),
        directory, destination.constData(), RENAME_NOREPLACE) == 0;
    const int failure = errno;
    ::close(directory);
    if (!renamed) {
        setError(failure == EEXIST ? QStringLiteral("An item with that name already exists.")
                                 : QStringLiteral("Could not rename this item. The original was kept."));
        return false;
    }
    clearError();
    clearSelection();
    refresh();
    return true;
}

void FilesBackend::cancelDeletion()
{
    if (m_deletionDirectory >= 0) ::close(m_deletionDirectory);
    m_deletionDirectory = -1;
    m_deletionToken.clear();
    m_deletionPath.clear();
    m_deletionEntries.clear();
}

QString FilesBackend::prepareDeletion()
{
    cancelDeletion();
    if (!m_selectionMode || m_selected.isEmpty()
        || QFileInfo(m_currentPath).canonicalFilePath() != m_currentPath) {
        setError(QStringLiteral("Select the items you want to delete."));
        return {};
    }
    m_deletionDirectory = openUserDirectory(m_rootPath, m_currentPath);
    struct stat directory;
    if (m_deletionDirectory < 0 || ::fstat(m_deletionDirectory, &directory)) {
        cancelDeletion(); setError(QStringLiteral("The folder could not be opened.")); return {};
    }
    QStringList names = m_selected.values();
    names.sort();
    for (const QString &name : names) {
        struct stat entry;
        if (!validName(name) || ::fstatat(m_deletionDirectory, QFile::encodeName(name).constData(),
                                        &entry, AT_SYMLINK_NOFOLLOW)
            || entry.st_dev != directory.st_dev || (!S_ISREG(entry.st_mode) && !S_ISDIR(entry.st_mode))) {
            cancelDeletion();
            setError(QStringLiteral("The selected items changed. Refresh and try again."));
            return {};
        }
        m_deletionEntries.append({name, quint64(entry.st_dev), quint64(entry.st_ino), S_ISDIR(entry.st_mode)});
    }
    m_deletionPath = m_currentPath;
    m_deletionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clearError();
    return m_deletionToken;
}

bool FilesBackend::deleteSelection(const QString &token, const QString &expectedDirectory)
{
    bool valid = !token.isEmpty() && token == m_deletionToken && m_deletionDirectory >= 0
        && expectedDirectory == m_deletionPath && m_currentPath == m_deletionPath
        && m_selected.size() == m_deletionEntries.size();
    const int location = openUserDirectory(m_rootPath, m_currentPath);
    struct stat current, captured;
    valid = valid && location >= 0 && !::fstat(location, &current)
        && !::fstat(m_deletionDirectory, &captured)
        && current.st_dev == captured.st_dev && current.st_ino == captured.st_ino;
    if (location >= 0) ::close(location);
    // Validate the complete snapshot before removing anything from a batch.
    for (const DeleteEntry &entry : std::as_const(m_deletionEntries)) {
        struct stat info;
        valid = valid && m_selected.contains(entry.name)
            && !::fstatat(m_deletionDirectory, QFile::encodeName(entry.name).constData(), &info, AT_SYMLINK_NOFOLLOW)
            && quint64(info.st_dev) == entry.device && quint64(info.st_ino) == entry.inode
            && (entry.folder ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode));
    }
    if (!valid) {
        cancelDeletion();
        setError(QStringLiteral("The selected items or folder changed. Nothing was deleted."));
        return false;
    }
    bool ok = true;
    for (const DeleteEntry &entry : std::as_const(m_deletionEntries)) {
        struct stat info;
        if (::fstatat(m_deletionDirectory, QFile::encodeName(entry.name).constData(), &info, AT_SYMLINK_NOFOLLOW)
            || quint64(info.st_dev) != entry.device || quint64(info.st_ino) != entry.inode
            || !removeEntry(m_deletionDirectory, QFile::encodeName(entry.name), info)) {
            ok = false; break;
        }
    }
    cancelDeletion();
    clearSelection();
    refresh();
    if (ok) clearError();
    else setError(QStringLiteral("Some items could not be deleted. Check the remaining items and try again."));
    return ok;
}

void FilesBackend::cancelTransfer()
{
    if (m_transferDirectory >= 0) ::close(m_transferDirectory);
    m_transferDirectory = -1;
    m_transferMode.clear();
    m_transferPath.clear();
    m_transferEntries.clear();
    emit viewChanged();
}

bool FilesBackend::prepareTransfer(const QString &mode)
{
    if (mode != QLatin1String("copy") && mode != QLatin1String("move")) return false;
    if (prepareDeletion().isEmpty()) return false;
    cancelTransfer();
    m_transferDirectory = std::exchange(m_deletionDirectory, -1);
    m_transferPath = m_deletionPath;
    m_transferEntries = std::move(m_deletionEntries);
    m_transferMode = mode;
    cancelDeletion();
    clearSelection();
    refresh();
    return true;
}

bool FilesBackend::transferSelection(const QString &expectedDirectory)
{
    if (m_transferDirectory < 0 || m_transferEntries.isEmpty() || m_currentPath != expectedDirectory) {
        setError(QStringLiteral("Choose a destination folder first.")); return false;
    }
    const int source = openUserDirectory(m_rootPath, m_transferPath);
    const int destination = openUserDirectory(m_rootPath, m_currentPath);
    struct stat current, captured;
    bool valid = source >= 0 && destination >= 0 && !::fstat(source, &current)
        && !::fstat(m_transferDirectory, &captured)
        && current.st_dev == captured.st_dev && current.st_ino == captured.st_ino;
    if (source >= 0) ::close(source);
    for (const DeleteEntry &entry : std::as_const(m_transferEntries)) {
        struct stat info, existing;
        const QByteArray name = QFile::encodeName(entry.name);
        const QString original = QDir(m_transferPath).filePath(entry.name);
        valid = valid && !::fstatat(m_transferDirectory, name.constData(), &info, AT_SYMLINK_NOFOLLOW)
            && quint64(info.st_dev) == entry.device && quint64(info.st_ino) == entry.inode
            && (entry.folder ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode))
            && (!entry.folder || (m_currentPath != original && !m_currentPath.startsWith(original + QLatin1Char('/'))));
        if (destination >= 0 && (::fstatat(destination, name.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0
                                || errno != ENOENT)) {
            ::close(destination);
            setError(QStringLiteral("An item with that name already exists. Choose another folder."));
            return false;
        }
    }
    if (!valid) {
        if (destination >= 0) ::close(destination);
        setError(QStringLiteral("The source changed or the destination is inside a selected folder."));
        return false;
    }
    const bool moving = m_transferMode == QLatin1String("move");
    bool ok = true;
    QVector<DeleteEntry> remaining;
    for (const DeleteEntry &entry : std::as_const(m_transferEntries)) {
        const QByteArray name = QFile::encodeName(entry.name);
        struct stat info;
        const bool unchanged = !::fstatat(m_transferDirectory, name.constData(), &info, AT_SYMLINK_NOFOLLOW)
            && quint64(info.st_dev) == entry.device && quint64(info.st_ino) == entry.inode;
        const bool completed = ok && unchanged && (moving
            ? ::syscall(SYS_renameat2, m_transferDirectory, name.constData(), destination, name.constData(), RENAME_NOREPLACE) == 0
            : copyEntry(m_transferDirectory, destination, name, info));
        if (!completed) { ok = false; remaining.append(entry); }
    }
    ::close(destination);
    m_transferEntries = remaining;
    if (ok) { cancelTransfer(); clearError(); }
    else setError(QStringLiteral("Some items could not be transferred. Completed items are kept; retry the remaining items."));
    refresh();
    return ok;
}

void FilesBackend::closePreview()
{
    m_previewName.clear();
    m_previewDetail.clear();
    m_previewPath.clear();
    m_previewAudio = false;
    emit previewChanged();
}
