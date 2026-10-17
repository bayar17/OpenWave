import QtQuick
import QtTest
import ".."

Item {
    id: scene
    width: 768; height: 1366

    MouseArea { id: app; anchors.fill: parent; property int clicks: 0; onClicked: clicks++ }
    EdgeNavigationMenu {
        id: menu
        anchors.fill: parent
        reduceMotion: true
        backAvailable: true
    }
    SignalSpy { id: back; target: menu; signalName: "backRequested" }
    SignalSpy { id: home; target: menu; signalName: "homeRequested" }

    TestCase {
        name: "EdgeNavigation"
        when: windowShown

        function init() {
            menu.canOpen = true
            menu.backAvailable = true
            menu.reduceMotion = true
            menu.swipeDirection = -1
            menu.close()
            tryCompare(menu, "progress", 0)
            app.clicks = 0; back.clear(); home.clear()
        }
        function cleanup() { menu.close(); tryCompare(menu, "progress", 0) }
        function swipe(y) {
            mouseDrag(scene, scene.width - 10, y, -80, 0, Qt.LeftButton)
        }
        function test_only_right_edge_can_open() {
            mouseDrag(scene, 200, 400, 22, 0, Qt.LeftButton)
            verify(!menu.open)
            const clicks = app.clicks
            mouseClick(scene, 200, 400)
            compare(app.clicks, clicks + 1)
            swipe(400)
            verify(menu.open)
        }
        function test_tap_and_vertical_drag_do_not_open() {
            mouseClick(scene, scene.width - 10, 400)
            verify(!menu.open)
            mouseDrag(scene, scene.width - 10, 400, 0, -120, Qt.LeftButton)
            verify(!menu.open)
            compare(back.count, 0); compare(home.count, 0)
        }
        function test_touch_swipe_opens_near_finger() {
            const touch = touchEvent(scene)
            touch.press(0, scene, scene.width - 10, 600).commit()
            touch.move(0, scene, scene.width - 90, 600).commit()
            touch.release(0, scene, scene.width - 90, 600).commit()
            tryCompare(menu, "open", true)
            tryCompare(menu, "progress", 1)
            const panel = findChild(menu, "edgeNavigationPanel")
            compare(panel.y + panel.height / 2, 600)
        }
        function test_top_and_bottom_positions_stay_on_screen() {
            const panel = findChild(menu, "edgeNavigationPanel")
            for (const y of [1, 350, scene.height - 1]) {
                menu.reveal(y)
                tryCompare(menu, "progress", 1)
                verify(panel.y >= menu.safeTop)
                verify(panel.y + panel.height <= menu.height - menu.safeBottom)
                verify(panel.x >= 0 && panel.x + panel.width <= menu.width)
                menu.close(); tryCompare(menu, "progress", 0)
            }
        }
        function test_back_and_home_emit_once_and_close() {
            menu.reveal(400); tryCompare(menu, "progress", 1)
            const backButton = findChild(menu, "edgeNavigationBack")
            mouseClick(backButton, backButton.width / 2, backButton.height / 2)
            compare(back.count, 1); compare(home.count, 0)
            verify(!menu.open)
            tryCompare(menu, "progress", 0)
            menu.reveal(700); tryCompare(menu, "progress", 1)
            const homeButton = findChild(menu, "edgeNavigationHome")
            mouseClick(homeButton, homeButton.width / 2, homeButton.height / 2)
            compare(back.count, 1); compare(home.count, 1)
            verify(!menu.open)
            compare(app.clicks, 0)
        }
        function test_unavailable_back_does_nothing() {
            menu.backAvailable = false
            menu.reveal(400); tryCompare(menu, "progress", 1)
            const button = findChild(menu, "edgeNavigationBack")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(back.count, 0); compare(home.count, 0)
            verify(menu.open)
        }
        function test_outside_tap_dismisses_without_clicking_app() {
            menu.reveal(400); tryCompare(menu, "progress", 1)
            mouseClick(scene, 200, 400)
            verify(!menu.open)
            compare(app.clicks, 0)
            compare(back.count, 0); compare(home.count, 0)
        }
        function test_modal_or_transition_blocks_gesture_and_closes_menu() {
            menu.canOpen = false
            swipe(400)
            verify(!menu.open)
            menu.canOpen = true
            menu.reveal(400); tryCompare(menu, "progress", 1)
            menu.canOpen = false
            tryCompare(menu, "progress", 0)
            verify(!menu.open)
        }
        function test_escape_dismisses_without_navigation() {
            menu.reveal(400); tryCompare(menu, "progress", 1)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "open", false)
            compare(back.count, 0); compare(home.count, 0)
        }
        function test_reduced_motion_keeps_panel_position_fixed() {
            menu.reveal(400)
            const panel = findChild(menu, "edgeNavigationPanel")
            compare(panel.x, scene.width - panel.width - 16)
            tryCompare(menu, "progress", 1)
            menu.close()
            compare(panel.x, scene.width - panel.width - 16)
        }
        function test_menu_slides_and_fades_without_resizing() {
            menu.reduceMotion = false
            const panel = findChild(menu, "edgeNavigationPanel")
            menu.reveal(400)
            wait(40)
            verify(menu.progress > 0 && menu.progress < 1)
            verify(panel.x > scene.width - panel.width - 16 && panel.x < scene.width)
            compare(panel.width, 82); compare(panel.height, 176)
            tryCompare(menu, "progress", 1)
            menu.close()
            wait(40)
            verify(panel.visible && panel.opacity > 0 && panel.opacity < 1)
            tryCompare(menu, "progress", 0)
            verify(!panel.visible)
        }
        function test_outward_gesture_does_not_open() {
            mouseDrag(scene, scene.width - 26, 400, 22, 0, Qt.LeftButton)
            verify(!menu.open)
        }
    }
}
