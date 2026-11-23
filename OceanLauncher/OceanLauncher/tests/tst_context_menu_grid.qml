import QtQuick
import QtTest
import ".."

Item {
    id: stage
    width: 700
    height: 900

    property var launched: []
    property string requested: ""

    DrawerAppGrid {
        id: grid
        width: 668
        height: 700
        draggable: true
        reduceMotion: true
        revealProgress: 1
        onClicked: app => stage.launched = stage.launched.concat([app.appId])
        onAppLongPressed: app => {
            const topLeft = app.mapToItem(menu, 0, 0)
            menu.show(app.appId, false, Qt.rect(topLeft.x, topLeft.y, app.width, app.height))
        }
    }
    AppContextMenu {
        id: menu
        anchors.fill: parent
        reduceMotion: true
        onDetailsRequested: appId => stage.requested = "details:" + appId
        onMoveRequested: appId => stage.requested = "move:" + appId
    }

    TestCase {
        name: "ContextMenuOverGrid"
        when: windowShown

        function apps(count) {
            const result = []
            for (let i = 0; i < count; ++i) result.push("app" + i)
            return result
        }
        function init() {
            grid.apps = apps(15)
            grid.favoriteCount = 0
            grid.contentY = 0
            stage.launched = []
            stage.requested = ""
            menu.close()
            wait(80)
        }
        function iconCenter(name) {
            const item = findChild(grid, "drawerApp_" + name)
            verify(item !== null)
            return item.mapToItem(stage, item.width / 2, item.height / 2)
        }
        function rowCenter(key) {
            const row = findChild(menu, "appMenu_" + key)
            verify(row !== null)
            return row.mapToItem(stage, row.width / 2, row.height / 2)
        }
        function holdMouse(p) {
            mousePress(stage, p.x, p.y)
            wait(1500)
            mouseRelease(stage, p.x, p.y)
            wait(80)
        }
        function overlapsAnIcon(p) {
            for (let i = 0; i < 15; ++i) {
                const q = iconCenter("app" + i)
                if (Math.abs(q.x - p.x) < 50 && Math.abs(q.y - p.y) < 60) return "app" + i
            }
            return ""
        }

        function test_menu_row_over_an_icon_in_the_grid_does_not_launch_it() {
            const target = iconCenter("app6")
            holdMouse(target)
            compare(menu.open, true)
            const p = rowCenter("details")
            const under = overlapsAnIcon(p)
            console.info("row over icon: " + under)
            mouseClick(stage, p.x, p.y)
            wait(80)
            compare(stage.requested, "details:app6")
            compare(stage.launched.length, 0)
        }

        function test_second_row_over_an_icon_in_the_grid_does_not_launch_it() {
            const target = iconCenter("app7")
            holdMouse(target)
            compare(menu.open, true)
            const p = rowCenter("move")
            console.info("move row over icon: " + overlapsAnIcon(p))
            mouseClick(stage, p.x, p.y)
            wait(80)
            compare(stage.requested, "move:app7")
            compare(stage.launched.length, 0)
        }
    }
}
