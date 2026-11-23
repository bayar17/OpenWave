import QtQuick
import QtTest
import ".."

Item {
    id: stage
    width: 700
    height: 900

    property int iconTaps: 0
    property string requested: ""

    DrawerAppIcon {
        id: under
        x: 40
        y: 300
        title: "Under"
        onClicked: stage.iconTaps++
    }
    DrawerAppIcon {
        id: other
        x: 220
        y: 300
        title: "Other"
        onClicked: stage.iconTaps++
    }
    AppContextMenu {
        id: menu
        anchors.fill: parent
        reduceMotion: true
        onDetailsRequested: appId => stage.requested = "details:" + appId
        onMoveRequested: appId => stage.requested = "move:" + appId
        onUninstallRequested: appId => stage.requested = "uninstall:" + appId
    }

    TestCase {
        name: "ContextMenuBlocking"
        when: windowShown

        function init() {
            stage.iconTaps = 0
            stage.requested = ""
            menu.close()
            wait(50)
        }

        function rowCenter(key) {
            const row = findChild(menu, "appMenu_" + key)
            verify(row !== null)
            return row.mapToItem(stage, row.width / 2, row.height / 2)
        }

        function test_row_over_an_icon_does_not_activate_the_icon() {
            menu.show("com.example.under", false, Qt.rect(40, 300, 115, 163))
            wait(50)
            const p = rowCenter("details")
            verify(p.x > 40 && p.x < 400)
            mouseClick(stage, p.x, p.y)
            wait(50)
            compare(stage.requested, "details:com.example.under")
            compare(stage.iconTaps, 0)
        }

        function test_a_tap_outside_the_menu_only_closes_it() {
            menu.show("com.example.under", false, Qt.rect(40, 300, 115, 163))
            wait(50)
            mouseClick(stage, other.x + 50, other.y + 50)
            wait(50)
            compare(menu.open, false)
            compare(stage.iconTaps, 0)
            compare(stage.requested, "")
        }

        function touchTap(x, y) {
            const t = touchEvent(stage)
            t.press(0, stage, x, y)
            t.commit()
            wait(30)
            t.release(0, stage, x, y)
            t.commit()
            wait(50)
        }

        function test_touch_on_a_row_over_an_icon_does_not_activate_the_icon() {
            menu.show("com.example.under", false, Qt.rect(40, 300, 115, 163))
            wait(50)
            const p = rowCenter("details")
            touchTap(p.x, p.y)
            compare(stage.requested, "details:com.example.under")
            compare(stage.iconTaps, 0)
        }

        function test_touch_outside_the_menu_only_closes_it() {
            menu.show("com.example.under", false, Qt.rect(40, 300, 115, 163))
            wait(50)
            touchTap(other.x + 50, other.y + 50)
            compare(menu.open, false)
            compare(stage.iconTaps, 0)
            compare(stage.requested, "")
        }
    }
}
