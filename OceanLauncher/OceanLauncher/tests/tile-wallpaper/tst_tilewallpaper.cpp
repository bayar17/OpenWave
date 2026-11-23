#include <QTest>
#include <QQuickWindow>
#include <cmath>
#include "../../TileWallpaperItem.h"
#include "../../TilePuzzleModel.h"

class TileWallpaperTests final : public QObject
{
    Q_OBJECT

private slots:
    void puzzleMovesOnlyIntoAdjacentVacancy()
    {
        TilePuzzleModel puzzle;
        puzzle.reset(4, 3, 1234);
        QCOMPARE(puzzle.count(), 12);
        QVERIFY(puzzle.blankCell() >= 0 && puzzle.blankCell() < puzzle.count());
        QCOMPARE(puzzle.tileAt(puzzle.blankCell()), -1);
        QCOMPARE(puzzle.blankCell(), (puzzle.rows() / 2) * puzzle.columns() + puzzle.columns() / 2);

        puzzle.reset(4, 3, 1234);
        const int blank = puzzle.blankCell();
        int source = -1;
        TilePuzzleModel::Direction direction = TilePuzzleModel::None;
        if (blank % puzzle.columns() > 0) {
            source = blank - 1;
            direction = TilePuzzleModel::Right;
        } else if (blank % puzzle.columns() + 1 < puzzle.columns()) {
            source = blank + 1;
            direction = TilePuzzleModel::Left;
        } else if (blank / puzzle.columns() > 0) {
            source = blank - puzzle.columns();
            direction = TilePuzzleModel::Down;
        } else {
            source = blank + puzzle.columns();
            direction = TilePuzzleModel::Up;
        }
        const int tile = puzzle.tileAt(source);
        QVERIFY(puzzle.forceMove(direction));
        QVERIFY(puzzle.move().active);
        QCOMPARE(puzzle.move().tile, tile);
        QCOMPARE(puzzle.move().from, source);
        QCOMPARE(puzzle.move().to, blank);
        QVERIFY(!puzzle.forceMove());
        puzzle.finishMove();
        QCOMPARE(puzzle.blankCell(), source);
        QCOMPARE(puzzle.tileAt(blank), tile);
        QCOMPARE(puzzle.tileAt(source), -1);

        QVector<int> seen;
        int blanks = 0;
        for (int cell = 0; cell < puzzle.count(); ++cell) {
            const int tile = puzzle.tileAt(cell);
            if (tile < 0) {
                ++blanks;
                continue;
            }
            QVERIFY(!seen.contains(tile));
            seen.append(tile);
        }
        QCOMPARE(blanks, 1);
        QCOMPARE(seen.size(), puzzle.count() - 1);
    }

    void gesturesScheduleLegalDirectionalChains()
    {
        TilePuzzleModel puzzle;
        puzzle.reset(4, 3, 42);
        const int blank = puzzle.blankCell();
        const int source = blank % puzzle.columns() > 0 ? blank - 1 : blank + 1;
        const auto direction = source < blank ? TilePuzzleModel::Right : TilePuzzleModel::Left;
        puzzle.gesture(source, direction, 2200);
        puzzle.advance(0.05, true);
        QVERIFY(puzzle.move().active);
        QCOMPARE(puzzle.move().from, source);
        QCOMPARE(puzzle.move().to, blank);

        int checkedMoves = 0;
        for (int tick = 0; tick < 5000; ++tick) {
            puzzle.advance(0.05, true);
            const auto move = puzzle.move();
            if (move.active) {
                const int dx = std::abs(move.from % puzzle.columns() - move.to % puzzle.columns());
                const int dy = std::abs(move.from / puzzle.columns() - move.to / puzzle.columns());
                QCOMPARE(dx + dy, 1);
            }
            int blanks = 0;
            QVector<int> tiles;
            for (int cell = 0; cell < puzzle.count(); ++cell) {
                const int tile = puzzle.tileAt(cell);
                if (tile < 0) {
                    ++blanks;
                    continue;
                }
                QVERIFY(!tiles.contains(tile));
                tiles.append(tile);
            }
            QCOMPARE(blanks, 1);
            QCOMPARE(tiles.size(), puzzle.count() - 1);
            checkedMoves = puzzle.completedMoves();
        }
        QVERIFY(checkedMoves > 10);
    }

    void stoppingFinishesTheCurrentLegalMove()
    {
        TilePuzzleModel puzzle;
        puzzle.reset(3, 3, 7);
        const int blank = puzzle.blankCell();
        int source = -1;
        TilePuzzleModel::Direction direction = TilePuzzleModel::None;
        if (blank % puzzle.columns() > 0) {
            source = blank - 1;
            direction = TilePuzzleModel::Right;
        } else if (blank % puzzle.columns() + 1 < puzzle.columns()) {
            source = blank + 1;
            direction = TilePuzzleModel::Left;
        } else if (blank / puzzle.columns() > 0) {
            source = blank - puzzle.columns();
            direction = TilePuzzleModel::Down;
        } else {
            source = blank + puzzle.columns();
            direction = TilePuzzleModel::Up;
        }
        QVERIFY(puzzle.forceMove(direction));
        const auto current = puzzle.move();
        puzzle.stop();
        QVERIFY(!puzzle.move().active);
        QCOMPARE(puzzle.blankCell(), current.from);
        QCOMPARE(puzzle.tileAt(current.to), current.tile);
        QVERIFY(!puzzle.forceMove());
        puzzle.advance(10, true);
        QCOMPARE(puzzle.completedMoves(), 1);
    }

    void pausingPreservesTheCurrentVisualMove()
    {
        TilePuzzleModel puzzle;
        puzzle.reset(5, 4, 101);
        QVERIFY(puzzle.forceMove());
        puzzle.advance(0.08, true);
        QVERIFY(puzzle.move().active);
        const qreal progress = puzzle.move().progress;
        const int completed = puzzle.completedMoves();

        puzzle.pause();
        puzzle.advance(1, true);
        QVERIFY(puzzle.move().active);
        QCOMPARE(puzzle.move().progress, progress);
        QCOMPARE(puzzle.completedMoves(), completed);

        puzzle.resume();
        for (int i = 0; i < 12; ++i)
            puzzle.advance(0.05, true);
        QCOMPARE(puzzle.completedMoves(), completed + 1);
    }

    void loadsOneSourceIntoAdaptiveSquareGrid()
    {
        TileWallpaperItem item;
        item.setWidth(768);
        item.setHeight(1366);
        item.setSource(QUrl::fromLocalFile(QStringLiteral(WALLPAPER_FIXTURE)));

        QTRY_VERIFY_WITH_TIMEOUT(item.tileCount() > 100, 5000);
        QVERIFY(item.tileCount() < 400);

        item.setWidth(960);
        item.setHeight(540);
        QTRY_VERIFY_WITH_TIMEOUT(item.tileCount() > 80, 1000);
        QVERIFY(item.tileCount() < 300);
    }

    void revealKeepsGestureProgressAndCancellationResetsIt()
    {
        TileWallpaperItem item;
        item.setRevealProgress(0.38);
        item.beginReveal();
        QCOMPARE(item.revealProgress(), 0.38);
        item.cancelReveal();
        QCOMPARE(item.revealProgress(), 0.0);
    }

    void reducedMotionStopsSimulation()
    {
        QQuickWindow window;
        auto *item = new TileWallpaperItem(window.contentItem());
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::applicationState(), Qt::ApplicationActive, 2000);
        item->setActive(true);
        QVERIFY(item->active());

        item->setReduceMotion(true);
        QVERIFY(!item->active());
        item->applyGesture(150, 250, 1800, -900);
        QVERIFY(!item->active());

        item->setReduceMotion(false);
        item->setActive(true);
        QVERIFY(item->active());
        window.hide();
    }

    void inactivityPausesAndResumesSimulation()
    {
        QQuickWindow window;
        auto *item = new TileWallpaperItem(window.contentItem());
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::applicationState(), Qt::ApplicationActive, 2000);
        item->setActive(true);
        QVERIFY(item->active());

        window.hide();
        QTRY_VERIFY_WITH_TIMEOUT(!item->active(), 2000);
        item->setReduceMotion(true);
        item->setReduceMotion(false);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::applicationState(), Qt::ApplicationActive, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(item->active(), 2000);
        window.hide();
    }

    void repeatedImpulsesKeepGridStable()
    {
        QQuickWindow window;
        auto *item = new TileWallpaperItem(window.contentItem());
        item->setWidth(768);
        item->setHeight(1366);
        item->setSource(QUrl::fromLocalFile(QStringLiteral(WALLPAPER_FIXTURE)));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->tileCount() > 100, 5000);
        const int count = item->tileCount();
        item->setActive(true);

        for (int i = 0; i < 3000; ++i)
            item->applyGesture((i * 37) % 768, (i * 61) % 1366, (i % 2 ? 1600 : -900), 720);

        QCOMPARE(item->tileCount(), count);
        QVERIFY(item->active());
        QTest::qWait(100);
        QCOMPARE(item->tileCount(), count);
        window.hide();
    }

};

QTEST_MAIN(TileWallpaperTests)
#include "tst_tilewallpaper.moc"
