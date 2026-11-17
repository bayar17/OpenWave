#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QVector>

class FilesBackend : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY viewChanged)
    Q_PROPERTY(QString breadcrumb READ breadcrumb NOTIFY viewChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY viewChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY viewChanged)
    Q_PROPERTY(int sortMode READ sortMode NOTIFY viewChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode NOTIFY viewChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY viewChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY viewChanged)
    Q_PROPERTY(bool selectedFolder READ selectedFolder NOTIFY viewChanged)
    Q_PROPERTY(QString transferMode READ transferMode NOTIFY viewChanged)
    Q_PROPERTY(int transferCount READ transferCount NOTIFY viewChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString previewName READ previewName NOTIFY previewChanged)
    Q_PROPERTY(QString previewDetail READ previewDetail NOTIFY previewChanged)
    Q_PROPERTY(bool previewVisible READ previewVisible NOTIFY previewChanged)
    Q_PROPERTY(bool previewAudio READ previewAudio NOTIFY previewChanged)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        FolderRole,
        DetailRole,
        SelectedRole,
        AudioRole
    };
    enum SortMode { NameSort, NewestSort, SizeSort };
    Q_ENUM(SortMode)

    explicit FilesBackend(QString userName = {},
                          QString usersPath = QStringLiteral("/Data/Users"),
                          QObject *parent = nullptr);
    ~FilesBackend() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString userName() const { return m_userName; }
    QString rootPath() const { return m_rootPath; }
    QString currentPath() const { return m_currentPath; }
    QString breadcrumb() const;
    QString searchQuery() const { return m_searchQuery; }
    bool showHidden() const { return m_showHidden; }
    int sortMode() const { return m_sortMode; }
    bool selectionMode() const { return m_selectionMode; }
    int selectedCount() const { return m_selected.size(); }
    QString selectedName() const;
    bool selectedFolder() const;
    QString transferMode() const { return m_transferMode; }
    int transferCount() const { return m_transferEntries.size(); }
    QString error() const { return m_error; }
    QString previewName() const { return m_previewName; }
    QString previewDetail() const { return m_previewDetail; }
    bool previewVisible() const { return !m_previewName.isEmpty(); }
    bool previewAudio() const { return m_previewAudio; }
    QString previewPath() const { return m_previewPath; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setSearchQuery(const QString &query);
    Q_INVOKABLE void setShowHidden(bool enabled);
    Q_INVOKABLE void setSortMode(int mode);
    Q_INVOKABLE void setSelectionMode(bool enabled);
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE bool up();
    Q_INVOKABLE void goToRoot();
    Q_INVOKABLE bool goToDirectory(const QString &path);
    Q_INVOKABLE QString entryPath(int row) const;
    Q_INVOKABLE bool createFile(const QString &name);
    Q_INVOKABLE bool fileExists(const QString &name) const;
    Q_INVOKABLE bool replaceFile(const QString &name, const QString &expectedDirectory);
    Q_INVOKABLE bool createFolder(const QString &name);
    Q_INVOKABLE bool renameItem(const QString &oldName, const QString &newName,
                               const QString &expectedDirectory);
    Q_INVOKABLE QString prepareDeletion();
    Q_INVOKABLE bool deleteSelection(const QString &token, const QString &expectedDirectory);
    Q_INVOKABLE void cancelDeletion();
    Q_INVOKABLE bool prepareTransfer(const QString &mode);
    Q_INVOKABLE bool transferSelection(const QString &expectedDirectory);
    Q_INVOKABLE void cancelTransfer();
    Q_INVOKABLE void closePreview();
    Q_INVOKABLE void clearError();

signals:
    void viewChanged();
    void errorChanged();
    void previewChanged();

private:
    struct DeleteEntry {
        QString name;
        quint64 device = 0;
        quint64 inode = 0;
        bool folder = false;
    };
    QString m_deletionToken;
    QString m_deletionPath;
    int m_deletionDirectory = -1;
    QVector<DeleteEntry> m_deletionEntries;
    QString m_transferMode;
    QString m_transferPath;
    int m_transferDirectory = -1;
    QVector<DeleteEntry> m_transferEntries;
    struct Entry {
        QString name;
        QString detail;
        bool folder = false;
        bool selected = false;
        bool audio = false;
        qint64 bytes = 0;
        QDateTime modified;
    };
    bool validName(const QString &name) const;
    bool withinRoot(const QString &canonicalPath) const;
    void setError(const QString &message);
    void clearSelection();
    QString itemPath(const QString &name) const;
    QString m_userName;
    QString m_rootPath;
    QString m_currentPath;
    QString m_searchQuery;
    bool m_showHidden = false;
    QString m_error;
    QString m_previewName;
    QString m_previewDetail;
    QString m_previewPath;
    bool m_previewAudio = false;
    int m_sortMode = NameSort;
    bool m_selectionMode = false;
    QSet<QString> m_selected;
    QVector<Entry> m_entries;
};
