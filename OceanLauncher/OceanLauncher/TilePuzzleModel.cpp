#include "TilePuzzleModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

void TilePuzzleModel::reset(int columns, int rows, quint32 seed)
{
    m_columns = std::max(0, columns);
    m_rows = std::max(0, rows);
    m_cells.resize(m_columns * m_rows);
    for (int i = 0; i < m_cells.size(); ++i)
        m_cells[i] = i;
    m_random.seed(seed);
    m_blank = m_cells.isEmpty() ? -1 : (m_rows / 2) * m_columns + m_columns / 2;
    if (m_blank >= 0)
        m_cells[m_blank] = -1;
    m_lastTile = -1;
    m_lastSource = -1;
    m_completedMoves = 0;
    m_move = {};
    m_activity = 0;
    m_duration = 0.32;
    m_gestureCell = -1;
    m_preferred = None;
    m_stopped = false;
    scheduleNext();
}

void TilePuzzleModel::advance(qreal seconds, bool enabled)
{
    if (!enabled || m_stopped || m_cells.size() < 4)
        return;
    const qreal dt = std::clamp<qreal>(seconds, 0, 0.05);
    m_activity = std::max<qreal>(0, m_activity - dt * 0.62);
    if (m_move.active) {
        m_move.progress = std::min<qreal>(1, m_move.progress + dt / std::max<qreal>(0.06, m_duration));
        if (m_move.progress >= 1) {
            finishMove();
            scheduleNext();
        }
        return;
    }
    m_wait -= dt * (1 + m_activity * 5.0);
    if (m_wait <= 0) {
        const Direction preferred = m_preferred;
        const int nearCell = m_gestureCell;
        m_preferred = None;
        m_gestureCell = -1;
        startMove(preferred, nearCell, true);
    }
}

void TilePuzzleModel::gesture(int cell, Direction direction, qreal speed)
{
    if (m_stopped || m_cells.isEmpty())
        return;
    const qreal normalized = std::clamp<qreal>(speed / 2600.0, 0, 1);
    m_activity = std::max(m_activity, 0.45 + normalized * 0.55);
    if (direction != None)
        m_preferred = direction;
    if (cell >= 0 && cell < m_cells.size())
        m_gestureCell = cell;
    m_wait = std::min(m_wait, 0.035);
}

bool TilePuzzleModel::forceMove(Direction direction)
{
    if (m_move.active || m_stopped)
        return false;
    return startMove(direction, -1, false);
}

void TilePuzzleModel::finishMove()
{
    if (!m_move.active)
        return;
    m_cells[m_move.from] = -1;
    m_cells[m_move.to] = m_move.tile;
    m_blank = m_move.from;
    m_lastTile = m_move.tile;
    m_lastSource = m_move.to;
    ++m_completedMoves;
    m_move = {};
}

void TilePuzzleModel::stop()
{
    finishMove();
    m_stopped = true;
    m_wait = 0;
}

void TilePuzzleModel::pause()
{
    m_stopped = true;
    m_preferred = None;
    m_gestureCell = -1;
}

void TilePuzzleModel::resume()
{
    if (!m_stopped)
        return;
    m_stopped = false;
    m_wait = 0.12;
}

int TilePuzzleModel::columns() const { return m_columns; }
int TilePuzzleModel::rows() const { return m_rows; }
int TilePuzzleModel::count() const { return m_cells.size(); }
int TilePuzzleModel::blankCell() const { return m_blank; }
int TilePuzzleModel::tileAt(int cell) const { return cell >= 0 && cell < m_cells.size() ? m_cells[cell] : -1; }
const TilePuzzleModel::Move &TilePuzzleModel::move() const { return m_move; }
int TilePuzzleModel::completedMoves() const { return m_completedMoves; }

bool TilePuzzleModel::startMove(Direction preferred, int nearCell, bool randomFallback)
{
    QVector<int> candidates = legalSources();
    if (candidates.isEmpty())
        return false;

    const auto matchesDirection = [this, preferred](int source) {
        return preferred == None || directionBetween(source, m_blank) == preferred;
    };
    QVector<int> matching;
    for (int source : candidates) {
        if (matchesDirection(source))
            matching.append(source);
    }

    int source = -1;
    if (!matching.isEmpty()) {
        candidates = matching;
    } else if (!randomFallback && preferred != None) {
        return false;
    }

    if (nearCell >= 0) {
        int bestDistance = std::numeric_limits<int>::max();
        QVector<int> nearest;
        for (int candidate : candidates) {
            const int distance = std::abs(candidate % m_columns - nearCell % m_columns)
                               + std::abs(candidate / m_columns - nearCell / m_columns);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = {candidate};
            } else if (distance == bestDistance) {
                nearest.append(candidate);
            }
        }
        candidates = nearest;
    } else if (randomFallback && candidates.size() > 1) {
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [this](int candidate) {
            return m_cells[candidate] == m_lastTile && candidate == m_lastSource;
        }), candidates.end());
        if (candidates.isEmpty())
            candidates = legalSources();
    }

    source = candidates[m_random.bounded(static_cast<quint32>(candidates.size()))];
    const qreal intensity = m_activity;
    m_duration = 0.32 - intensity * 0.22;
    m_move = {m_cells[source], source, m_blank, 0, true};
    return true;
}

void TilePuzzleModel::scheduleNext()
{
    m_wait = (0.72 + m_random.generateDouble() * 0.55) * (1 - m_activity * 0.88);
}

QVector<int> TilePuzzleModel::legalSources() const
{
    QVector<int> result;
    if (m_blank < 0 || m_columns < 2 || m_rows < 2)
        return result;
    const int x = m_blank % m_columns;
    const int y = m_blank / m_columns;
    if (x > 0) result.append(m_blank - 1);
    if (x + 1 < m_columns) result.append(m_blank + 1);
    if (y > 0) result.append(m_blank - m_columns);
    if (y + 1 < m_rows) result.append(m_blank + m_columns);
    return result;
}

TilePuzzleModel::Direction TilePuzzleModel::directionBetween(int from, int to) const
{
    const int delta = to - from;
    if (delta == -1) return Left;
    if (delta == 1) return Right;
    if (delta == -m_columns) return Up;
    if (delta == m_columns) return Down;
    return None;
}
