#include "TileWallpaperItem.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGSimpleRectNode>
#include <QSGTexture>
#include <QSGTextureMaterial>
#include <QThread>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

class TileNode final : public QSGGeometryNode
{
public:
    explicit TileNode(QSGTexture *texture)
        : texture(texture)
    {
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0, 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
        geometry->setIndexDataPattern(QSGGeometry::StaticPattern);
        setGeometry(geometry);
        setFlag(OwnsGeometry, true);

        auto *material = new QSGTextureMaterial;
        material->setTexture(texture);
        material->setFiltering(QSGTexture::Linear);
        setMaterial(material);
        setFlag(OwnsMaterial, true);
    }

    ~TileNode() override { delete texture; }

    QSGTexture *texture = nullptr;
    quint64 generation = 0;
};

QImage makeShadowImage()
{
    constexpr int size = 96;
    QImage image(size, size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    const qreal centerX = size / 2.0;
    const qreal centerY = size / 2.0 - 5.0;
    const qreal halfWidth = 31.0;
    const qreal halfHeight = 31.0;
    const qreal radius = 8.0;
    for (int y = 0; y < size; ++y) {
        auto *pixel = image.scanLine(y);
        for (int x = 0; x < size; ++x) {
            const qreal dx = std::max<qreal>(std::abs(x + 0.5 - centerX) - (halfWidth - radius), 0);
            const qreal dy = std::max<qreal>(std::abs(y + 0.5 - centerY) - (halfHeight - radius), 0);
            const qreal outside = std::hypot(dx, dy) - radius;
            const qreal alpha = 0.27 * std::exp(-(outside * outside) / (2.0 * 8.0 * 8.0));
            const int offset = x * 4;
            pixel[offset] = 0;
            pixel[offset + 1] = 0;
            pixel[offset + 2] = 0;
            pixel[offset + 3] = qRound(alpha * 255);
        }
    }
    return image;
}

class PuzzleNode final : public QSGNode
{
public:
    PuzzleNode(QSGTexture *texture, QSGTexture *shadowTexture, quint64 generation)
        : vacancy(new QSGSimpleRectNode), shadowOpacity(new QSGOpacityNode),
          shadows(new TileNode(shadowTexture)), tiles(new TileNode(texture))
    {
        appendChildNode(vacancy);
        shadowOpacity->appendChildNode(shadows);
        appendChildNode(shadowOpacity);
        appendChildNode(tiles);
        shadows->generation = generation;
        tiles->generation = generation;
    }

    QSGSimpleRectNode *vacancy;
    QSGOpacityNode *shadowOpacity;
    TileNode *shadows;
    TileNode *tiles;
};

QImage decodeImage(const QUrl &url)
{
    QImageReader reader(url.isLocalFile() ? url.toLocalFile() : url.toString());
    reader.setAutoTransform(true);
    return reader.read();
}

}

TileWallpaperItem::TileWallpaperItem(QQuickItem *parent)
    : QQuickItem(parent), m_frameTimer(new QTimer(this))
{
    setFlag(ItemHasContents, true);
    setClip(true);
    m_frameTimer->setInterval(16);
    connect(m_frameTimer, &QTimer::timeout, this, &TileWallpaperItem::advance);
    connect(this, &QQuickItem::visibleChanged, this, &TileWallpaperItem::updateActivity);
    const auto watchWindow = [this](QQuickWindow *window) {
        if (window)
            connect(window, &QWindow::visibilityChanged, this,
                    [this](QWindow::Visibility) { updateActivity(); });
        updateActivity();
    };
    connect(this, &QQuickItem::windowChanged, this, watchWindow);
    if (window())
        watchWindow(window());
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState) { updateActivity(); });
}

TileWallpaperItem::~TileWallpaperItem()
{
    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->quit();
        m_decodeThread->wait();
    }
}

QUrl TileWallpaperItem::source() const { return m_source; }

void TileWallpaperItem::setSource(const QUrl &source)
{
    if (m_source == source)
        return;
    m_source = source;
    ++m_sourceGeneration;
    m_image = {};
    rebuildGrid();
    update();
    emit sourceChanged();
    decodeSource();
}

qreal TileWallpaperItem::separation() const { return m_separation; }

void TileWallpaperItem::setSeparation(qreal separation)
{
    const qreal bounded = std::clamp<qreal>(separation, 0, 1);
    if (qFuzzyCompare(m_separation + 1, bounded + 1))
        return;
    setSeparationInternal(bounded);
    emit separationChanged();
}

void TileWallpaperItem::setSeparationInternal(qreal separation)
{
    m_separation = separation;
    update();
}

bool TileWallpaperItem::active() const { return m_active; }

void TileWallpaperItem::setActive(bool active)
{
    m_requestedActive = active;
    updateActivity();
}

void TileWallpaperItem::updateActivity()
{
    const bool appActive = qGuiApp->applicationState() == Qt::ApplicationActive;
    const bool windowVisible = window() && window()->isVisible();
    const bool next = m_requestedActive && isVisible() && windowVisible && !m_reduceMotion && appActive;
    if (m_active == next) {
        if (next && !m_frameTimer->isActive()) {
            m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
            m_frameTimer->start();
        }
        return;
    }
    m_active = next;
    if (m_active) {
        m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
        m_frameTimer->start();
    } else {
        m_frameTimer->stop();
    }
    emit activeChanged();
}

bool TileWallpaperItem::reduceMotion() const { return m_reduceMotion; }

qreal TileWallpaperItem::revealProgress() const { return m_revealProgress; }

void TileWallpaperItem::setRevealProgress(qreal progress)
{
    const qreal bounded = std::clamp<qreal>(progress, 0, 1);
    if (qFuzzyCompare(m_revealProgress + 1, bounded + 1))
        return;
    m_revealProgress = bounded;
    update();
    emit revealProgressChanged();
}

void TileWallpaperItem::setReduceMotion(bool reduceMotion)
{
    if (m_reduceMotion == reduceMotion)
        return;
    m_reduceMotion = reduceMotion;
    if (reduceMotion && !m_revealing)
        rebuildGrid();
    updateActivity();
    update();
    emit reduceMotionChanged();
}

int TileWallpaperItem::tileCount() const { return m_tileCount; }

void TileWallpaperItem::applyGesture(qreal x, qreal y, qreal velocityX, qreal velocityY)
{
    if (m_reduceMotion || m_revealing || m_puzzle.count() == 0)
        return;
    const qreal speed = std::hypot(velocityX, velocityY);
    TilePuzzleModel::Direction direction = TilePuzzleModel::None;
    if (speed > 40) {
        if (std::abs(velocityX) > std::abs(velocityY))
            direction = velocityX < 0 ? TilePuzzleModel::Left : TilePuzzleModel::Right;
        else
            direction = velocityY < 0 ? TilePuzzleModel::Up : TilePuzzleModel::Down;
    }

    const int columns = m_puzzle.columns();
    const int rows = m_puzzle.rows();
    const qreal left = (width() - columns * m_tileSize) * 0.5;
    const qreal top = (height() - rows * m_tileSize) * 0.5;
    const int column = qFloor((x - left) / m_tileSize);
    const int row = qFloor((y - top) / m_tileSize);
    const int cell = column >= 0 && column < columns && row >= 0 && row < rows
                   ? row * columns + column : -1;
    m_puzzle.gesture(cell, direction, speed);
    setActive(true);
    requestFrame();
}

void TileWallpaperItem::settle()
{
    m_puzzle.stop();
    requestFrame();
}

void TileWallpaperItem::beginSession()
{
    m_revealing = false;
    setRevealProgress(0);
    rebuildGrid();
    requestFrame();
}

void TileWallpaperItem::beginReveal()
{
    if (m_revealing)
        return;
    m_revealing = true;
    m_revealMotionEnabled = !m_reduceMotion;
    m_puzzle.pause();
    setActive(false);
    requestFrame();
}

void TileWallpaperItem::cancelReveal()
{
    if (!m_revealing)
        return;
    m_revealing = false;
    setRevealProgress(0);
    m_puzzle.resume();
    updateActivity();
    requestFrame();
}

void TileWallpaperItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        rebuildGrid();
        update();
    }
}

void TileWallpaperItem::decodeSource()
{
    if (m_source.isEmpty() || m_decodeInFlight)
        return;
    m_decodeInFlight = true;
    m_imageWatcherGeneration = m_sourceGeneration;
    const QUrl source = m_source;
    const quint64 generation = m_imageWatcherGeneration;
    QPointer<TileWallpaperItem> guard(this);
    QObject *application = QCoreApplication::instance();
    QThread *thread = QThread::create([guard, application, source, generation] {
        const QImage image = decodeImage(source);
        QMetaObject::invokeMethod(application, [guard, generation, image] {
            if (guard)
                guard->finishDecode(generation, image);
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    m_decodeThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_decodeThread == thread)
            m_decodeThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

void TileWallpaperItem::finishDecode(quint64 generation, const QImage &image)
{
    m_decodeInFlight = false;
    if (generation != m_sourceGeneration) {
        decodeSource();
        return;
    }
    installImage(image);
}

void TileWallpaperItem::installImage(const QImage &image)
{
    m_image = image;
    m_textureGeneration = m_sourceGeneration;
    rebuildGrid();
    update();
}

void TileWallpaperItem::rebuildGrid()
{
    const qreal viewportWidth = width();
    const qreal viewportHeight = height();
    m_tileSize = std::clamp<qreal>(std::min(viewportWidth, viewportHeight) / 7.0, 64, 96);
    if (viewportWidth <= 0 || viewportHeight <= 0 || m_image.isNull()) {
        m_puzzle.reset(0, 0);
        if (m_tileCount != 0) {
            m_tileCount = 0;
            emit tileCountChanged();
        }
        return;
    }

    const int columns = std::max(2, qCeil(viewportWidth / m_tileSize));
    const int rows = std::max(2, qCeil(viewportHeight / m_tileSize));
    m_puzzle.reset(columns, rows);
    const int count = columns * rows - 1;
    if (m_tileCount != count) {
        m_tileCount = count;
        emit tileCountChanged();
    }
}

void TileWallpaperItem::advance()
{
    if (!m_active || m_reduceMotion || m_puzzle.count() == 0)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qreal dt = std::clamp<qreal>((now - m_lastFrameMs) / 1000.0, 0.001, 0.05);
    m_lastFrameMs = now;
    m_puzzle.advance(dt, true);
    requestFrame();
}

void TileWallpaperItem::requestFrame()
{
    if (m_active || !m_image.isNull())
        update();
}

QSGNode *TileWallpaperItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (m_image.isNull() || m_puzzle.count() == 0 || !window()) {
        delete node;
        return nullptr;
    }

    auto *puzzleNode = static_cast<PuzzleNode *>(node);
    if (!puzzleNode || puzzleNode->tiles->generation != m_textureGeneration) {
        delete puzzleNode;
        QSGTexture *texture = window()->createTextureFromImage(m_image);
        QSGTexture *shadowTexture = window()->createTextureFromImage(makeShadowImage());
        if (!texture || !shadowTexture) {
            delete texture;
            delete shadowTexture;
            return nullptr;
        }
        puzzleNode = new PuzzleNode(texture, shadowTexture, m_textureGeneration);
    }

    const int columns = m_puzzle.columns();
    const int rows = m_puzzle.rows();
    const int occupied = m_puzzle.count() - 1;
    QSGGeometry *geometry = puzzleNode->tiles->geometry();
    if (geometry->vertexCount() != occupied * 6)
        geometry->allocate(occupied * 6);
    const qreal viewportWidth = std::max<qreal>(1, width());
    const qreal viewportHeight = std::max<qreal>(1, height());
    const qreal scale = std::max(viewportWidth / m_image.width(), viewportHeight / m_image.height());
    const qreal sourceWidth = viewportWidth / scale;
    const qreal sourceHeight = viewportHeight / scale;
    const qreal cropX = (m_image.width() - sourceWidth) * 0.5;
    const qreal cropY = (m_image.height() - sourceHeight) * 0.5;
    const qreal left = (viewportWidth - columns * m_tileSize) * 0.5;
    const qreal top = (viewportHeight - rows * m_tileSize) * 0.5;
    const auto cellRect = [this, left, top](int cell) {
        return QRectF(left + (cell % m_puzzle.columns()) * m_tileSize,
                      top + (cell / m_puzzle.columns()) * m_tileSize,
                      m_tileSize, m_tileSize);
    };

    const auto revealRect = [this, viewportHeight](QRectF rect) {
        if (!m_revealMotionEnabled || m_revealProgress <= 0)
            return rect;
        const qreal verticalPosition = std::clamp<qreal>(rect.center().y() / viewportHeight, 0, 1);
        const qreal delay = (1 - verticalPosition) * 0.12;
        const qreal localProgress = std::clamp<qreal>((m_revealProgress - delay) / (1 - delay), 0, 1);
        rect.translate(0, -(viewportHeight + m_tileSize) * localProgress);
        return rect;
    };

    const auto writeQuad = [&](QSGGeometry *target, int tile, QRectF rect, int &index,
                               bool shadow) {
        const int sourceColumn = tile % columns;
        const int sourceRow = tile / columns;
        const qreal sourceX = left + sourceColumn * m_tileSize;
        const qreal sourceY = top + sourceRow * m_tileSize;
        const qreal u0 = (cropX + sourceX / viewportWidth * sourceWidth) / m_image.width();
        const qreal v0 = (cropY + sourceY / viewportHeight * sourceHeight) / m_image.height();
        const qreal u1 = (cropX + (sourceX + m_tileSize) / viewportWidth * sourceWidth) / m_image.width();
        const qreal v1 = (cropY + (sourceY + m_tileSize) / viewportHeight * sourceHeight) / m_image.height();
        if (shadow) {
            const qreal pad = std::clamp<qreal>(m_tileSize * 0.18, 12, 20);
            rect.adjust(-pad, -pad, pad, pad);
            rect.translate(0, std::clamp<qreal>(m_tileSize * 0.1, 6, 12));
        }
        const QPointF points[4] = {{rect.left(), rect.top()}, {rect.right(), rect.top()},
                                   {rect.right(), rect.bottom()}, {rect.left(), rect.bottom()}};
        const qreal uvInset = shadow ? 0 : 0;
        Q_UNUSED(uvInset);
        QPointF uv[4];
        if (shadow) {
            uv[0] = {0, 0}; uv[1] = {1, 0}; uv[2] = {1, 1}; uv[3] = {0, 1};
        } else {
            uv[0] = {u0, v0}; uv[1] = {u1, v0}; uv[2] = {u1, v1}; uv[3] = {u0, v1};
        }
        const int order[6] = {0, 1, 2, 0, 2, 3};
        auto *targetVertices = target->vertexDataAsTexturedPoint2D();
        for (int corner : order)
            targetVertices[index++].set(points[corner].x(), points[corner].y(), uv[corner].x(), uv[corner].y());
    };

    const TilePuzzleModel::Move &move = m_puzzle.move();
    const qreal t = move.active ? move.progress * move.progress * (3 - 2 * move.progress) : 0;
    QRectF movingRect;
    if (move.active) {
        const QRectF from = cellRect(move.from);
        const QRectF to = cellRect(move.to);
        movingRect = from.translated((to.x() - from.x()) * t, (to.y() - from.y()) * t);
    }

    const auto addCell = [&](int tile, QRectF rect, int &tileIndex, int &shadowIndex) {
        const QRectF translated = revealRect(rect);
        writeQuad(geometry, tile, translated, tileIndex, false);
        writeQuad(puzzleNode->shadows->geometry(), tile, translated, shadowIndex, true);
    };

    QSGGeometry *shadowGeometry = puzzleNode->shadows->geometry();
    if (shadowGeometry->vertexCount() != occupied * 6)
        shadowGeometry->allocate(occupied * 6);
    int index = 0;
    int shadowIndex = 0;
    for (int cell = 0; cell < m_puzzle.count(); ++cell) {
        const int tile = m_puzzle.tileAt(cell);
        if (tile < 0 || (move.active && tile == move.tile))
            continue;
        addCell(tile, cellRect(cell), index, shadowIndex);
    }
    if (move.active) {
        addCell(move.tile, movingRect, index, shadowIndex);
    }

    geometry->markVertexDataDirty();
    shadowGeometry->markVertexDataDirty();
    puzzleNode->tiles->markDirty(QSGNode::DirtyGeometry);
    puzzleNode->shadows->markDirty(QSGNode::DirtyGeometry);
    const qreal shadowIn = std::clamp<qreal>(m_revealProgress / 0.16, 0, 1);
    const qreal shadowOut = std::clamp<qreal>((1 - m_revealProgress) / 0.22, 0, 1);
    puzzleNode->shadowOpacity->setOpacity(m_revealMotionEnabled ? shadowIn * shadowOut : 0);
    const int visibleBlank = move.active ? move.to : m_puzzle.blankCell();
    puzzleNode->vacancy->setRect(cellRect(visibleBlank));
    puzzleNode->vacancy->setColor(Qt::transparent);
    return puzzleNode;
}
