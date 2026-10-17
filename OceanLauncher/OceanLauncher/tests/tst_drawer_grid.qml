import QtQuick
import QtTest
import ".."

Item {
    width: 900
    height: 600
    DrawerAppGrid {
        id: grid
        width: 668
        height: 340
        draggable: true
        onAppDragStarted: appDragActive = true
        onAppDragFinished: appDragActive = false
    }
    SignalSpy { id: clicks; target: grid; signalName: "clicked" }
    SignalSpy { id: drags; target: grid; signalName: "appDragStarted" }

    TestCase {
        name: "DrawerGrid"
        when: windowShown

        function apps(count) {
            const result = []
            for (let i = 0; i < count; ++i) result.push("app" + i)
            return result
        }
        function init() {
            grid.cancelFlick()
            grid.appDragActive = false
            grid.width = 668
            grid.height = 340
            grid.apps = []
            grid.contentY = 0
            clicks.clear(); drags.clear()
        }
        function verifyRows() {
            wait(20)
            const identities = []
            for (let row = 0; row < grid.rowCount; ++row) {
                const first = grid.itemAt(row * grid.columns)
                const count = Math.min(grid.columns, grid.count - row * grid.columns)
                const last = grid.itemAt(row * grid.columns + count - 1)
                verify(first !== null && last !== null)
                const start = first.mapToItem(grid, 0, 0)
                const end = last.mapToItem(grid, last.width, 0)
                fuzzyCompare((start.x + end.x) / 2, grid.width / 2, 0.1)
                for (let col = 0; col < count; ++col) {
                    const index = row * grid.columns + col
                    const item = grid.itemAt(index)
                    const point = item.mapToItem(grid, 0, 0)
                    identities.push(item.appId)
                    compare(item.appId, grid.apps[index])
                    compare(item.width, grid.cellWidth)
                    compare(item.height, grid.cellHeight)
                    fuzzyCompare(point.x, start.x + col * (grid.cellWidth + grid.columnSpacing), 0.1)
                    fuzzyCompare(point.y + grid.contentY,
                        grid.topPadding + row * (grid.cellHeight + grid.rowSpacing), 0.1)
                    verify(point.x >= 0 && point.x + item.width <= grid.width)
                }
            }
            compare(identities.length, grid.apps.length)
            compare(new Set(identities).size, grid.apps.length)
        }
        function test_centered_rows_data() {
            return [{tag: "one", count: 1}, {tag: "two", count: 2}, {tag: "three", count: 3},
                    {tag: "four", count: 4}, {tag: "eight", count: 8}, {tag: "empty", count: 0}]
        }
        function test_centered_rows(data) {
            grid.apps = apps(data.count)
            verifyRows()
        }
        function test_updates_and_resizing() {
            grid.apps = apps(8); verifyRows()
            grid.apps = ["app2", "app7"]; verifyRows()
            grid.apps = ["app7"]; verifyRows()
            grid.apps = apps(9)
            for (const width of [330, 520, 860, 668]) {
                grid.width = width
                verifyRows()
            }
        }
        function test_a_scroll_and_click() {
            grid.apps = apps(20); verifyRows()
            verify(grid.contentHeight > grid.height)
            compare(grid.interactive, true)
            mouseWheel(grid, 200, 180, 0, -360)
            tryVerify(() => grid.contentY > 0)
            compare(grid.contentX, 0)
            mouseWheel(grid, 200, 180, -120, 0)
            compare(grid.contentX, 0)
            grid.cancelFlick()
            let target = null
            for (let i = 0; i < grid.count; ++i) {
                const item = grid.itemAt(i)
                const point = item.mapToItem(grid, 75, 65)
                if (point.y > 30 && point.y < grid.height - 30) { target = item; break }
            }
            verify(target !== null)
            mouseClick(target, 75, 65)
            compare(clicks.count, 1)
            compare(clicks.signalArguments[0][0].appId, target.appId)
            grid.contentY = grid.contentHeight - grid.height
            grid.apps = apps(1)
            tryCompare(grid, "contentY", 0)
            compare(grid.contentHeight, grid.height)
            verifyRows()
        }
        function test_scrolled_drop_uses_centered_row() {
            grid.apps = apps(8); wait(20)
            grid.contentY = 207
            const left = grid.itemAt(6).mapToItem(grid, 0, 0)
            const right = grid.itemAt(7).mapToItem(grid, grid.cellWidth, 0)
            compare(grid.insertionIndex(left.x, left.y + 50), 6)
            compare(grid.insertionIndex(right.x, right.y + 50), 8)
            const marker = grid.insertionPoint(6)
            fuzzyCompare(marker.x, left.x - grid.columnSpacing / 2, 0.1)
            fuzzyCompare(marker.y, left.y, 0.1)
        }
        function test_drag_autoscroll_stays_bounded() {
            grid.apps = apps(20); wait(20)
            grid.appDragY = grid.height - 1
            grid.appDragActive = true
            tryVerify(() => grid.contentY > 0)
            verify(grid.contentY <= grid.contentHeight - grid.height)
            compare(grid.contentX, 0)
            grid.appDragActive = false
            const stoppedAt = grid.contentY
            wait(40)
            compare(grid.contentY, stoppedAt)
            grid.appDragActive = true
            grid.contentY = grid.contentHeight - grid.height
            wait(40)
            compare(grid.contentY, grid.contentHeight - grid.height)
            grid.appDragY = 0
            tryVerify(() => grid.contentY < grid.contentHeight - grid.height)
            grid.appDragActive = false
        }
        function test_finger_swipe_scrolls_without_dragging_app() {
            grid.apps = apps(20); wait(30)
            const touch = touchEvent(grid)
            touch.press(0, grid, 120, 135).commit()
            wait(20)
            touch.move(0, grid, 120, 105).commit()
            wait(20)
            touch.move(0, grid, 120, 60).commit()
            wait(20)
            touch.release(0, grid, 120, 60).commit()
            tryVerify(() => grid.contentY > 0)
            compare(drags.count, 0)
            compare(clicks.count, 0)
            compare(grid.contentX, 0)
        }
        function test_held_finger_can_move_app() {
            grid.apps = apps(8); wait(30)
            const item = grid.itemAt(0)
            const touch = touchEvent(item)
            touch.press(0, item, 75, 65).commit()
            wait(1100)
            verify(item.touchDragArmed)
            touch.move(0, item, 110, 90).commit()
            tryCompare(drags, "count", 1)
            touch.move(0, item, 110, 140).commit()
            touch.release(0, item, 110, 140).commit()
            tryCompare(grid, "appDragActive", false)
            compare(clicks.count, 0)
        }
    }
}
