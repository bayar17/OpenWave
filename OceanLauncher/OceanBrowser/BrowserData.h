#pragma once

#include <QObject>
#include <QVariantList>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QMutex>

class BrowserPreviews : public QQuickImageProvider {
public:
    BrowserPreviews() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *, const QSize &) override;
    void put(const QString &, const QImage &);
    void remove(const QString &);
private:
    QMutex m_mutex;
    QHash<QString, QImage> m_images;
};

class BrowserData : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(bool reduceMotion READ reduceMotion CONSTANT)
public:
    explicit BrowserData(BrowserPreviews *, QString usersPath = QStringLiteral("/Data/Users"),
                         QString userName = {}, QString applicationId = {}, QObject *parent = nullptr);
    QString dataPath() const { return m_path; }
    QString userPath() const { return m_userPath; }
    bool reduceMotion() const;
    Q_INVOKABLE QVariantMap session() const;
    Q_INVOKABLE void saveSession(const QVariantList &tabs, const QString &activeId);
    Q_INVOKABLE bool capturePreview(QQuickItem *item, const QString &id, bool privateTab);
    Q_INVOKABLE QUrl previewSource(const QString &id) const;
    Q_INVOKABLE void removePreview(const QString &id);
signals:
    void previewReady(const QString &id, const QUrl &source);
    void storageError(const QString &message);
private:
    QVariant readJson(const QString &) const;
    bool writeJson(const QString &, const QVariant &);
    QString m_path;
    QString m_userPath;
    BrowserPreviews *m_previews;
    QHash<QString, quint64> m_previewGeneration;
    quint64 m_revision = 0;
};
