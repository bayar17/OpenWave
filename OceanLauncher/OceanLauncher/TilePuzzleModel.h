#pragma once

#include <QRandomGenerator>
#include <QVector>

class TilePuzzleModel
{
public:
    enum Direction { None, Left, Right, Up, Down };

    struct Move {
        int tile = -1;
        int from = -1;
        int to = -1;
        qreal progress = 0;
        bool active = false;
    };

    void reset(int columns, int rows, quint32 seed = QRandomGenerator::global()->generate());
    void advance(qreal seconds, bool enabled);
    void gesture(int cell, Direction direction, qreal speed);
    bool forceMove(Direction direction = None);
    void finishMove();
    void stop();
    void pause();
    void resume();

    int columns() const;
    int rows() const;
    int count() const;
    int blankCell() const;
    int tileAt(int cell) const;
    const Move &move() const;
    int completedMoves() const;

private:
    bool startMove(Direction preferred, int nearCell, bool randomFallback);
    void scheduleNext();
    QVector<int> legalSources() const;
    Direction directionBetween(int from, int to) const;

    int m_columns = 0;
    int m_rows = 0;
    int m_blank = -1;
    int m_lastTile = -1;
    int m_lastSource = -1;
    int m_completedMoves = 0;
    QVector<int> m_cells;
    Move m_move;
    QRandomGenerator m_random;
    qreal m_wait = 0;
    qreal m_activity = 0;
    qreal m_duration = 0.32;
    int m_gestureCell = -1;
    Direction m_preferred = None;
    bool m_stopped = false;
};
