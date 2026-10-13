#include "../FilesBackend.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

static bool selectItem(FilesBackend &files, const QString &name)
{
    for (int row = 0; row < files.rowCount(); ++row) {
        if (files.data(files.index(row), FilesBackend::NameRole).toString() == name) {
            files.activate(row); return true;
        }
    }
    return false;
}

static bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

class FilesBackendTest : public QObject
{
    Q_OBJECT
private slots:
    void profileAndNavigation();
    void confinement();
    void replacementSafety();
    void selectionAndRename();
    void deletionCancellationAndStaleSelection();
    void recursiveDeletionDoesNotFollowLinks();
    void copyingAndMovingSelection();
    void transferRejectsStaleSourceAndCleansFailedCopy();
};

void FilesBackendTest::profileAndNavigation()
{
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    FilesBackend files(QStringLiteral("root"), storage.path() + QStringLiteral("/Data/Users"));
    QCOMPARE(files.userName(), QStringLiteral("root"));
    QCOMPARE(files.rowCount(), 4);
    QCOMPARE(files.breadcrumb(), QStringLiteral("Data"));
    for (const QString &name : {"Downloads", "Musics", "Videos", "Documents"})
        QVERIFY(QFileInfo::exists(files.rootPath() + QLatin1Char('/') + name));

    QFile hidden(files.rootPath() + QStringLiteral("/.private.txt"));
    QVERIFY(hidden.open(QIODevice::WriteOnly));
    hidden.close();
    const QString hiddenFolder = files.rootPath() + QStringLiteral("/.private-folder");
    QVERIFY(QDir().mkdir(hiddenFolder));
    files.refresh();
    QVERIFY(!files.showHidden());
    QCOMPARE(files.rowCount(), 4);
    files.setShowHidden(true);
    QCOMPARE(files.rowCount(), 6);
    files.setSearchQuery(QStringLiteral(".private-folder"));
    QCOMPARE(files.rowCount(), 1);
    QVERIFY(files.data(files.index(0), FilesBackend::FolderRole).toBool());
    files.activate(0);
    QCOMPARE(files.currentPath(), hiddenFolder);
    QVERIFY(files.up());
    files.setShowHidden(false);
    QCOMPARE(files.rowCount(), 4);
    files.setSearchQuery(QStringLiteral(".private"));
    QCOMPARE(files.rowCount(), 0);
    files.setSearchQuery({});
    QVERIFY(hidden.remove());
    QVERIFY(QDir().rmdir(hiddenFolder));
    files.refresh();

    QVERIFY(files.createFolder(QStringLiteral("Designs")));
    QVERIFY(files.createFile(QStringLiteral("notes.txt")));
    QCOMPARE(files.rowCount(), 6);
    files.setSearchQuery(QStringLiteral("design"));
    QCOMPARE(files.rowCount(), 1);
    QCOMPARE(files.data(files.index(0), FilesBackend::NameRole).toString(), QStringLiteral("Designs"));
    files.activate(0);
    QCOMPARE(files.breadcrumb(), QStringLiteral("Data > Designs"));
    QCOMPARE(files.searchQuery(), QString());
    QVERIFY(files.createFile(QStringLiteral("inside.txt")));
    QCOMPARE(files.rowCount(), 1);
    QVERIFY(files.createFile(QStringLiteral("track.mp3")));
    QCOMPARE(files.rowCount(), 2);
    const int audioRow = files.data(files.index(0), FilesBackend::AudioRole).toBool() ? 0 : 1;
    QVERIFY(files.data(files.index(audioRow), FilesBackend::AudioRole).toBool());
    files.activate(audioRow);
    QVERIFY(files.previewVisible());
    QVERIFY(files.previewAudio());
    QVERIFY(files.previewPath().startsWith(files.rootPath() + QLatin1Char('/')));
    QVERIFY(files.up());
    QVERIFY(!files.previewVisible());
    QVERIFY(files.previewPath().isEmpty());
    QVERIFY(files.up());
    QCOMPARE(files.breadcrumb(), QStringLiteral("Data"));
    QVERIFY(!files.up());
}

void FilesBackendTest::confinement()
{
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    const QString usersPath = storage.path() + QStringLiteral("/Data/Users");
    FilesBackend files(QStringLiteral("root"), usersPath);
    QVERIFY(!files.createFile(QStringLiteral("../escape.txt")));
    QVERIFY(!files.createFolder(QStringLiteral("/tmp/escape")));
    QVERIFY(!QFileInfo::exists(storage.path() + QStringLiteral("/Data/escape.txt")));

    QVERIFY(QFile::link(storage.path(), files.rootPath() + QStringLiteral("/escape-link")));
    files.refresh();
    for (int row = 0; row < files.rowCount(); ++row)
        QVERIFY(files.data(files.index(row), FilesBackend::NameRole).toString() != QStringLiteral("escape-link"));
    FilesBackend redirected(QStringLiteral("root"), storage.path() + QStringLiteral("/Data/UsersRedirect"));
    QVERIFY(redirected.error().isEmpty());
    QVERIFY(QFile::link(storage.path(), usersPath + QStringLiteral("/redirect")));
    FilesBackend bad(QStringLiteral("redirect"), usersPath);
    QVERIFY(!bad.error().isEmpty());
    QCOMPARE(bad.rowCount(), 0);
}

void FilesBackendTest::replacementSafety()
{
    QTemporaryDir storage;
    FilesBackend files(QStringLiteral("root"), storage.path() + QStringLiteral("/Users"));
    const QString directory = files.currentPath();
    const QString path = directory + QStringLiteral("/original.txt");
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("keep these bytes"), qint64(16));
    original.close();
    QVERIFY(files.fileExists(QStringLiteral("original.txt")));
    QVERIFY(!files.createFile(QStringLiteral("original.txt")));
    QVERIFY(!files.replaceFile(QStringLiteral("original.txt"), directory + QStringLiteral("/Documents")));
    QVERIFY(!files.replaceFile(QStringLiteral("../original.txt"), directory));
    QVERIFY(!files.replaceFile(QStringLiteral("Documents"), directory));
    QVERIFY(QFile::link(path, directory + QStringLiteral("/linked.txt")));
    QVERIFY(!files.fileExists(QStringLiteral("linked.txt")));
    QVERIFY(!files.replaceFile(QStringLiteral("linked.txt"), directory));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("keep these bytes"));
    original.close();
    QVERIFY(files.replaceFile(QStringLiteral("original.txt"), directory));
    QCOMPARE(QFileInfo(path).size(), qint64(0));
    QVERIFY(!files.replaceFile(QStringLiteral("missing.txt"), directory));
    QVERIFY(!QFileInfo::exists(directory + QStringLiteral("/missing.txt")));
}

void FilesBackendTest::selectionAndRename()
{
    QTemporaryDir storage;
    FilesBackend files(QStringLiteral("root"), storage.path() + QStringLiteral("/Users"));
    const QString directory = files.currentPath();
    QFile original(directory + QStringLiteral("/original.txt"));
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("preserve contents");
    original.close();
    QVERIFY(files.createFolder(QStringLiteral("Original folder")));
    QFile nested(directory + QStringLiteral("/Original folder/child.txt"));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("nested contents"); nested.close();
    files.refresh();
    auto select = [&](const QString &name) {
        for (int row = 0; row < files.rowCount(); ++row) {
            if (files.data(files.index(row), FilesBackend::NameRole).toString() == name) {
                files.activate(row); return true;
            }
        }
        return false;
    };
    files.setSelectionMode(true);
    QCOMPARE(files.selectedCount(), 0);
    QVERIFY(select(QStringLiteral("original.txt")));
    QCOMPARE(files.selectedName(), QStringLiteral("original.txt"));
    QVERIFY(!files.selectedFolder());
    QVERIFY(!files.previewVisible());
    QCOMPARE(files.currentPath(), directory);
    QVERIFY(select(QStringLiteral("Original folder")));
    QCOMPARE(files.selectedCount(), 2);
    QVERIFY(files.selectedName().isEmpty());
    QVERIFY(select(QStringLiteral("original.txt")));
    QCOMPARE(files.selectedName(), QStringLiteral("Original folder"));
    QVERIFY(files.selectedFolder());
    QVERIFY(files.renameItem(QStringLiteral("Original folder"), QStringLiteral("Renamed folder"), directory));
    QVERIFY(!files.selectionMode());
    QCOMPARE(files.selectedCount(), 0);
    QVERIFY(!QFileInfo::exists(nested.fileName()));
    QVERIFY(QFileInfo::exists(directory + QStringLiteral("/Renamed folder/child.txt")));
    QVERIFY(files.createFile(QStringLiteral("existing.txt")));
    QVERIFY(!files.renameItem(QStringLiteral("original.txt"), QStringLiteral("existing.txt"), directory));
    QVERIFY(!files.renameItem(QStringLiteral("original.txt"), QStringLiteral("Renamed folder"), directory));
    QVERIFY(!files.renameItem(QStringLiteral("original.txt"), QStringLiteral("../escape"), directory));
    QVERIFY(!files.renameItem(QStringLiteral("original.txt"), QStringLiteral(".."), directory));
    QVERIFY(!files.renameItem(QStringLiteral("original.txt"), QStringLiteral("renamed.txt"), directory + QStringLiteral("/Documents")));
    QVERIFY(QFile::link(original.fileName(), directory + QStringLiteral("/link.txt")));
    QVERIFY(!files.renameItem(QStringLiteral("link.txt"), QStringLiteral("renamed.txt"), directory));
    QVERIFY(files.renameItem(QStringLiteral("original.txt"), QStringLiteral("renamed.txt"), directory));
    QVERIFY(!QFileInfo::exists(original.fileName()));
    QFile renamed(directory + QStringLiteral("/renamed.txt"));
    QVERIFY(renamed.open(QIODevice::ReadOnly));
    QCOMPARE(renamed.readAll(), QByteArray("preserve contents"));
    QVERIFY(files.renameItem(QStringLiteral("renamed.txt"), QStringLiteral("renamed.txt"), directory));
    QVERIFY(!files.renameItem(QStringLiteral("missing.txt"), QStringLiteral("anything.txt"), directory));
    QVERIFY(!QFileInfo::exists(directory + QStringLiteral("/anything.txt")));
}

void FilesBackendTest::deletionCancellationAndStaleSelection()
{
    QTemporaryDir storage;
    FilesBackend files("root", storage.path() + "/Users");
    const QString root = files.rootPath();
    QVERIFY(files.prepareDeletion().isEmpty());
    QVERIFY(writeFile(root + "/a.txt", "keep A"));
    QVERIFY(writeFile(root + "/b.txt", "keep B"));
    files.refresh(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "a.txt"));
    QString token = files.prepareDeletion();
    QVERIFY(!token.isEmpty());
    files.cancelDeletion();
    QVERIFY(!files.deleteSelection(token, root));
    QCOMPARE(readFile(root + "/a.txt"), QByteArray("keep A"));
    token = files.prepareDeletion();
    QVERIFY(selectItem(files, "b.txt"));
    QVERIFY(!files.deleteSelection(token, root));
    token = files.prepareDeletion();
    QVERIFY(!files.deleteSelection(token, root + "/Documents"));
    token = files.prepareDeletion();
    QVERIFY(QFile::rename(root + "/b.txt", root + "/saved-b.txt"));
    QVERIFY(writeFile(root + "/b.txt", "replacement"));
    QVERIFY(!files.deleteSelection(token, root));
    QCOMPARE(readFile(root + "/a.txt"), QByteArray("keep A"));
    QCOMPARE(readFile(root + "/b.txt"), QByteArray("replacement"));
    token = files.prepareDeletion();
    QVERIFY(files.deleteSelection(token, root));
    QVERIFY(!QFileInfo::exists(root + "/a.txt"));
    QVERIFY(!QFileInfo::exists(root + "/b.txt"));
    QCOMPARE(readFile(root + "/saved-b.txt"), QByteArray("keep B"));
    QVERIFY(!files.selectionMode());
    QVERIFY(!files.deleteSelection(token, root));
}

void FilesBackendTest::recursiveDeletionDoesNotFollowLinks()
{
    QTemporaryDir storage;
    FilesBackend files("root", storage.path() + "/Users");
    const QString root = files.rootPath(), outside = storage.path() + "/outside";
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(writeFile(outside + "/keep.txt", "outside bytes"));
    QVERIFY(QDir().mkpath(root + "/Tree/.hidden/child"));
    QVERIFY(writeFile(root + "/Tree/.hidden/child/secret", "nested bytes"));
    QVERIFY(QFile::link(outside, root + "/Tree/link"));
    QVERIFY(QFile::link(outside + "/missing", root + "/Tree/broken"));
    files.refresh(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "Tree"));
    const QString token = files.prepareDeletion();
    QVERIFY(files.deleteSelection(token, root));
    QVERIFY(!QFileInfo::exists(root + "/Tree"));
    QCOMPARE(readFile(outside + "/keep.txt"), QByteArray("outside bytes"));
    QVERIFY(!QFileInfo::exists(outside + "/missing"));
}

void FilesBackendTest::copyingAndMovingSelection()
{
    QTemporaryDir storage;
    FilesBackend files("root", storage.path() + "/Users");
    const QString root = files.rootPath(), outside = storage.path() + "/keep.txt";
    const QByteArray bytes(150000, '\x7f');
    QVERIFY(writeFile(root + "/a.bin", bytes));
    QVERIFY(QDir().mkpath(root + "/Tree/.hidden"));
    QVERIFY(writeFile(root + "/Tree/.hidden/nested", "nested bytes"));
    QVERIFY(writeFile(outside, "external"));
    QVERIFY(QFile::link(outside, root + "/Tree/link"));
    files.refresh(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "a.bin")); QVERIFY(selectItem(files, "Tree"));
    QVERIFY(files.prepareTransfer("copy"));
    QCOMPARE(files.transferCount(), 2);
    QVERIFY(!files.selectionMode());
    QVERIFY(files.goToDirectory(root + "/Tree/.hidden"));
    QVERIFY(!files.transferSelection(files.currentPath()));
    QVERIFY(!QFileInfo::exists(root + "/Tree/.hidden/Tree"));
    QVERIFY(writeFile(root + "/Documents/a.bin", "existing bytes"));
    QVERIFY(files.goToDirectory(root + "/Documents"));
    QVERIFY(!files.transferSelection(files.currentPath()));
    QCOMPARE(readFile(root + "/Documents/a.bin"), QByteArray("existing bytes"));
    QVERIFY(!QFileInfo::exists(root + "/Documents/Tree"));
    QVERIFY(files.goToDirectory(root + "/Videos"));
    QVERIFY(files.transferSelection(files.currentPath()));
    QCOMPARE(readFile(root + "/Videos/a.bin"), bytes);
    QCOMPARE(readFile(root + "/a.bin"), bytes);
    QCOMPARE(readFile(root + "/Videos/Tree/.hidden/nested"), QByteArray("nested bytes"));
    QVERIFY(QFileInfo(root + "/Videos/Tree/link").isSymbolicLink());
    QCOMPARE(QFileInfo(root + "/Videos/Tree/link").symLinkTarget(), outside);
    QVERIFY(files.transferMode().isEmpty());
    files.goToRoot(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "a.bin")); QVERIFY(selectItem(files, "Tree"));
    QVERIFY(files.prepareTransfer("move"));
    QVERIFY(files.goToDirectory(root + "/Musics"));
    QVERIFY(files.transferSelection(files.currentPath()));
    QVERIFY(!QFileInfo::exists(root + "/a.bin")); QVERIFY(!QFileInfo::exists(root + "/Tree"));
    QCOMPARE(readFile(root + "/Musics/a.bin"), bytes);
    QCOMPARE(readFile(root + "/Musics/Tree/.hidden/nested"), QByteArray("nested bytes"));
    QCOMPARE(readFile(outside), QByteArray("external"));
    files.goToRoot(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "Documents")); QVERIFY(files.prepareTransfer("move"));
    files.cancelTransfer();
    QVERIFY(!files.transferSelection(root));
    QVERIFY(QFileInfo::exists(root + "/Documents"));
}

void FilesBackendTest::transferRejectsStaleSourceAndCleansFailedCopy()
{
    QTemporaryDir storage;
    FilesBackend files("root", storage.path() + "/Users");
    const QString root = files.rootPath();
    QVERIFY(writeFile(root + "/original", "original"));
    files.refresh(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "original")); QVERIFY(files.prepareTransfer("move"));
    QVERIFY(QFile::rename(root + "/original", root + "/saved"));
    QVERIFY(writeFile(root + "/original", "replacement"));
    QVERIFY(files.goToDirectory(root + "/Documents"));
    QVERIFY(!files.transferSelection(files.currentPath()));
    QCOMPARE(readFile(root + "/original"), QByteArray("replacement"));
    QVERIFY(!QFileInfo::exists(root + "/Documents/original"));
    files.cancelTransfer(); files.goToRoot();
    QVERIFY(QDir().mkpath(root + "/Tree"));
    QVERIFY(writeFile(root + "/Tree/a.txt", "keep"));
    QVERIFY(::mkfifo(QFile::encodeName(root + "/Tree/z.pipe").constData(), 0600) == 0);
    files.refresh(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "Tree")); QVERIFY(files.prepareTransfer("copy"));
    QVERIFY(files.goToDirectory(root + "/Documents"));
    QVERIFY(!files.transferSelection(files.currentPath()));
    QVERIFY(!QFileInfo::exists(root + "/Documents/Tree"));
    QCOMPARE(readFile(root + "/Tree/a.txt"), QByteArray("keep"));
    files.cancelTransfer(); files.goToRoot(); files.setSelectionMode(true);
    QVERIFY(selectItem(files, "original")); QVERIFY(files.prepareTransfer("copy"));
    QVERIFY(files.goToDirectory(root + "/Documents"));
    QVERIFY(QFile::link(root + "/missing", root + "/Documents/original"));
    QVERIFY(!files.transferSelection(files.currentPath()));
    QVERIFY(QFileInfo(root + "/Documents/original").isSymbolicLink());
}

QTEST_GUILESS_MAIN(FilesBackendTest)
#include "FilesBackendTest.moc"
