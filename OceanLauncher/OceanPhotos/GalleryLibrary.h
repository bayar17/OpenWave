#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QTemporaryDir>

class GalleryLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList photos READ photos NOTIFY changed)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY changed)
    Q_PROPERTY(QStringList supportedFormats READ supportedFormats CONSTANT)
    Q_PROPERTY(QStringList supportedMimeTypes READ supportedMimeTypes CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit GalleryLibrary(QObject *parent = nullptr);
    explicit GalleryLibrary(const QString &storagePath, QObject *parent = nullptr);
    QVariantList photos() const { return m_photos; }
    QVariantList albums() const;
    QStringList supportedFormats() const;
    QStringList supportedMimeTypes() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool loadFolder(const QUrl &folder);
    Q_INVOKABLE int openImage(const QUrl &image);
    Q_INVOKABLE int importImage(const QUrl &image, const QString &name);
    bool loadSampleLibrary(const QString &path);
signals:
    void changed();
private:
    int appendImage(const QString &path, const QString &album = {}, const QString &date = {});
    QVariantList m_photos;
    QString m_error;
    QTemporaryDir m_imports;
    QString m_storage;
};
