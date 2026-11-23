#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QImage>
#include <QQuickImageProvider>

class PortalFileChooserBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString mutedTitle READ mutedTitle NOTIFY changed)
    Q_PROPERTY(QString primaryTitle READ primaryTitle NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString initialDirectory READ initialDirectory NOTIFY changed)
    Q_PROPERTY(QString suggestedFilename READ suggestedFilename NOTIFY changed)
    Q_PROPERTY(QString applicationId READ applicationId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool imageSources READ imageSources NOTIFY changed)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY changed)
    Q_PROPERTY(QStringList mimeTypes READ mimeTypes NOTIFY changed)
    Q_PROPERTY(QVariantList galleryItems READ galleryItems NOTIFY changed)
    Q_PROPERTY(bool galleryLoading READ galleryLoading NOTIFY changed)
    Q_PROPERTY(QString galleryError READ galleryError NOTIFY changed)
public:
    explicit PortalFileChooserBackend(QObject *parent = nullptr);
    ~PortalFileChooserBackend() override;
    QString requestId() const { return m_requestId; }
    QString title() const { return m_title; }
    QString mutedTitle() const { return m_mutedTitle; }
    QString primaryTitle() const { return m_primaryTitle; }
    QString mode() const { return m_mode; }
    QString initialDirectory() const { return m_initialDirectory; }
    QString suggestedFilename() const { return m_suggestedFilename; }
    QString applicationId() const { return m_applicationId; }
    bool busy() const { return m_busy; }
    bool imageSources() const { return m_imageSources; }
    QString imageSource() const { return m_imageSource; }
    QStringList mimeTypes() const { return m_mimeTypes; }
    QVariantList galleryItems() const { return m_galleryItems; }
    bool galleryLoading() const { return m_galleryLoading; }
    QString galleryError() const { return m_galleryError; }
    Q_INVOKABLE bool acceptsPath(const QString &path) const;
    Q_INVOKABLE void choose(const QStringList &paths);
    Q_INVOKABLE void chooseGallery(const QStringList &tokens);
    Q_INVOKABLE void loadGallery();
    Q_INVOKABLE QString galleryImageUrl(const QString &token) const;
    Q_INVOKABLE void cancel();
    QImage galleryImage(const QString &requestId, const QString &token,
                        const QSize &requestedSize) const;
signals:
    void changed();
    void errorOccurred(const QString &message);
private:
    struct Pending {
        QString id, title, mutedTitle, primaryTitle, mode, initialDirectory;
        QString suggestedFilename, applicationId;
        QStringList mimeTypes;
        bool imageSources = false;
        QString imageSource;
        QString error;
    };
    static Pending next(bool *registered, void *connection);
    void poll();
    void complete(uint status, const QStringList &paths);
    void completeGallery(uint status, const QStringList &tokens);
    QThreadPool m_pool;
    QTimer m_timer;
    QString m_requestId, m_title, m_mutedTitle, m_primaryTitle;
    QString m_mode, m_initialDirectory;
    QString m_suggestedFilename, m_applicationId;
    QVariantList m_galleryItems;
    QString m_galleryError;
    bool m_imageSources = false;
    QString m_imageSource;
    bool m_galleryLoading = false;
    bool m_galleryLoaded = false;
    QStringList m_mimeTypes;
    bool m_registered = false;
    bool m_polling = false;
    bool m_busy = false;
    QString m_lastError;
    void *m_bus = nullptr;
};

class PortalGalleryImageProvider : public QQuickImageProvider
{
public:
    explicit PortalGalleryImageProvider(const PortalFileChooserBackend *backend);
    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;
private:
    const PortalFileChooserBackend *m_backend;
};
