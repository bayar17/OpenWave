#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class WallpaperClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit WallpaperClient(QObject *parent = nullptr);
    ~WallpaperClient() override;

    QUrl source() const { return m_source; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap imageSize(const QUrl &url) const;
    Q_INVOKABLE void setCroppedWallpaper(const QUrl &url, double x, double y,
                                         double width, double height);
signals:
    void changed();
    void saved();
    void errorOccurred(const QString &message);
private:
    struct Result { QString path; QString hash; QString error; QByteArray bytes; };
    static Result getCurrent(const QString &knownHash);
    static Result saveCrop(const QUrl &url, double x, double y,
                           double width, double height);
    QThreadPool m_pool;
    QTimer m_refreshTimer;
    QUrl m_source;
    QString m_hash;
    bool m_refreshing = false;
    bool m_busy = false;
};
