#pragma once

#include "TilePuzzleModel.h"

#include <QImage>
#include <QQuickItem>
#include <QUrl>

class QThread;
class QTimer;

class TileWallpaperItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal separation READ separation WRITE setSeparation NOTIFY separationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(qreal revealProgress READ revealProgress WRITE setRevealProgress NOTIFY revealProgressChanged)
    Q_PROPERTY(int tileCount READ tileCount NOTIFY tileCountChanged)

public:
    explicit TileWallpaperItem(QQuickItem *parent = nullptr);
    ~TileWallpaperItem() override;

    QUrl source() const;
    void setSource(const QUrl &source);
    qreal separation() const;
    void setSeparation(qreal separation);
    bool active() const;
    void setActive(bool active);
    bool reduceMotion() const;
    void setReduceMotion(bool reduceMotion);
    qreal revealProgress() const;
    void setRevealProgress(qreal progress);
    int tileCount() const;

    Q_INVOKABLE void applyGesture(qreal x, qreal y, qreal velocityX, qreal velocityY);
    Q_INVOKABLE void beginSession();
    Q_INVOKABLE void settle();
    Q_INVOKABLE void beginReveal();
    Q_INVOKABLE void cancelReveal();

signals:
    void sourceChanged();
    void separationChanged();
    void activeChanged();
    void reduceMotionChanged();
    void revealProgressChanged();
    void tileCountChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void setSeparationInternal(qreal separation);
    void decodeSource();
    void installImage(const QImage &image);
    void finishDecode(quint64 generation, const QImage &image);
    void rebuildGrid();
    void advance();
    void requestFrame();
    void updateActivity();

    QUrl m_source;
    QImage m_image;
    TilePuzzleModel m_puzzle;
    QTimer *m_frameTimer = nullptr;
    qreal m_separation = 0;
    qreal m_revealProgress = 0;
    qreal m_tileSize = 80;
    bool m_active = false;
    bool m_requestedActive = false;
    bool m_reduceMotion = false;
    bool m_revealing = false;
    bool m_revealMotionEnabled = true;
    bool m_decodeInFlight = false;
    QThread *m_decodeThread = nullptr;
    quint64 m_sourceGeneration = 0;
    quint64 m_textureGeneration = 0;
    quint64 m_imageWatcherGeneration = 0;
    qint64 m_lastFrameMs = 0;
    int m_tileCount = 0;
};
