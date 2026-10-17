import QtQuick
import QtTest
import ".."

Item {
    id: scene
    width: 768; height: 1100
    ListModel { id: apps }
    QtObject {
        id: controller
        signal terminationFailed(string appId, string message)
    }
    RecentAppGrid {
        id: grid
        x: 32; y: 100; width: 704; height: 700
        model: apps; viewportItem: scene; appController: controller
        reduceMotion: true
    }
    SignalSpy { id: activation; target: grid; signalName: "activated" }
    SignalSpy { id: termination; target: grid; signalName: "terminationRequested" }
    TestCase {
        name: "RecentAppGrid"
        when: windowShown
        function populate(count) {
            apps.clear()
            for (let i = 0; i < count; ++i)
                apps.append({appId: "app" + i, title: "Application " + i,
                    iconSource: "", previewUrl: "", revealed: true})
            grid.forceLayout()
            wait(30)
        }
        function card(index) { return findChild(grid, "recentCard_app" + index) }
        function init() {
            grid.width = 704; grid.height = 700; grid.interactionEnabled = true
            grid.contentY = 0; activation.clear(); termination.clear()
            populate(3)
        }
        function cleanup() { grid.cancelFlick(); apps.clear() }
        function test_two_columns_then_next_row() {
            const a = card(0).mapToItem(grid, 0, 0)
            const b = card(1).mapToItem(grid, 0, 0)
            const c = card(2).mapToItem(grid, 0, 0)
            compare(a.y, b.y); compare(a.x, c.x)
            compare(b.x - a.x, grid.cellWidth)
            compare(c.y - a.y, grid.cellHeight)
            verify(a.x >= 0 && b.x + card(1).width <= grid.width)
        }
        function test_two_columns_after_resizing() {
            for (const width of [330, 520, 704, 1000]) {
                grid.width = width; grid.forceLayout(); wait(10)
                compare(card(0).mapToItem(grid, 0, 0).y, card(1).mapToItem(grid, 0, 0).y)
                compare(card(2).mapToItem(grid, 0, 0).y, grid.cellHeight)
                verify(card(0).width + 34 <= grid.cellWidth)
                verify(card(1).mapToItem(grid, card(1).width, 0).x <= grid.width)
            }
        }
        function test_a_mouse_wheel_scrolls_vertically() {
            populate(12)
            verify(grid.contentHeight > grid.height)
            mouseWheel(grid, 350, 250, 0, -360)
            tryVerify(() => grid.contentY > 0)
            compare(grid.contentX, 0)
        }
        function test_drag_on_card_scrolls_without_activation() {
            populate(12)
            mouseDrag(card(0), 100, 300, 0, -220, Qt.LeftButton)
            tryVerify(() => grid.contentY > 0)
            compare(grid.contentX, 0); compare(activation.count, 0); compare(termination.count, 0)
        }
        function test_scrolled_card_activation_uses_visible_origin() {
            populate(12)
            grid.positionViewAtIndex(6, GridView.Beginning); grid.forceLayout(); wait(20)
            const target = card(6)
            verify(target !== null)
            const origin = target.mapToItem(scene, 0, 0)
            verify(origin.y >= grid.y && origin.y < grid.y + grid.height)
            mouseClick(target, 100, 100)
            compare(activation.count, 1)
            compare(activation.signalArguments[0][0], "app6")
            compare(activation.signalArguments[0][1], target)
        }
        function test_touch_drag_scrolls_without_activation() {
            populate(12)
            const touch = touchEvent(grid)
            touch.press(0, grid, 150, 360).commit()
            for (let i = 1; i <= 12; ++i) {
                touch.move(0, grid, 150, 360 - i * 20).commit()
                wait(16)
            }
            touch.release(0, grid, 150, 120).commit()
            tryVerify(() => grid.contentY > 0)
            compare(grid.contentX, 0); compare(activation.count, 0); compare(termination.count, 0)
        }
        function test_removal_reflows_rows() {
            apps.remove(0); grid.forceLayout(); wait(20)
            compare(card(1).mapToItem(grid, 0, 0).y, card(2).mapToItem(grid, 0, 0).y)
            verify(card(1).x < card(2).mapToItem(grid, 0, 0).x)
        }
        function test_disabled_card_does_not_activate() {
            grid.interactionEnabled = false
            mouseClick(card(0), 100, 100)
            compare(activation.count, 0)
        }
        function test_hold_dismissal_and_failed_termination_recovery() {
            const target = card(0)
            mousePress(target, 100, 100); wait(3100)
            verify(target.armed)
            mouseMove(target, -150, 100, 20)
            tryCompare(termination, "count", 1)
            compare(termination.signalArguments[0][0], "app0")
            controller.terminationFailed("app0", "Try again")
            compare(target.terminating, false)
            mouseRelease(target, -150, 100)
            compare(activation.count, 0)
        }
    }
}
