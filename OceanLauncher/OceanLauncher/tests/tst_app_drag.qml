import QtQuick
import QtTest
import ".."

Item {
    width: 500; height: 500
    property int favoritesLaunched: 0
    property int favoritesDragged: 0
    property int favoritesDropped: 0
    property int gridLaunched: 0
    property int gridDragged: 0
    property int gridDropped: 0

    AppIcon {
        id: favorite
        x: 30; y: 20
        title: "Files"
        source: "assets/icons/Files.svg"
        draggable: true
        onClicked: favoritesLaunched++
        onDragStarted: favoritesDragged++
        onDragFinished: favoritesDropped++
    }
    DrawerAppIcon {
        id: unpinned
        x: 220; y: 200
        title: "Settings"
        source: "assets/icons/Settings.png"
        draggable: true
        onClicked: gridLaunched++
        onDragStarted: gridDragged++
        onDragFinished: gridDropped++
    }
    Row {
        id: dynamicFavorites
        x: 0; y: 400
        property var apps: ["files", "settings"]
        Repeater {
            id: favoriteRepeater
            model: dynamicFavorites.apps
            delegate: AppIcon {
                required property string modelData
                appId: modelData
                title: modelData
            }
        }
    }

    TestCase {
        name: "AppDrag"
        when: windowShown

        function init() {
            favoritesLaunched = 0
            favoritesDragged = 0
            favoritesDropped = 0
            gridLaunched = 0
            gridDragged = 0
            gridDropped = 0
        }

        function test_tap_launches_without_drag() {
            mouseClick(favorite, 65, 65)
            compare(favoritesLaunched, 1)
            compare(favoritesDragged, 0)
            mouseClick(unpinned, 75, 65)
            compare(gridLaunched, 1)
            compare(gridDragged, 0)
        }

        function test_favorites_repeater_uses_each_app_once() {
            compare(favoriteRepeater.count, 2)
            compare(favoriteRepeater.itemAt(0).appId, "files")
            compare(favoriteRepeater.itemAt(1).appId, "settings")
            dynamicFavorites.apps = ["settings"]
            compare(favoriteRepeater.count, 1)
            compare(favoriteRepeater.itemAt(0).appId, "settings")
        }

        function test_drag_suppresses_launch_and_reports_drop() {
            mousePress(favorite, 65, 65)
            mouseMove(favorite, 100, 90, 30)
            tryCompare(favorite, "dragging", true)
            mouseMove(favorite, 120, 220, 30)
            mouseRelease(favorite, 120, 220)
            tryCompare(favorite, "dragging", false)
            compare(favoritesDragged, 1)
            compare(favoritesDropped, 1)
            compare(favoritesLaunched, 0)

            mousePress(unpinned, 75, 65)
            mouseMove(unpinned, 115, 95, 30)
            tryCompare(unpinned, "dragging", true)
            mouseRelease(unpinned, 115, -120)
            compare(gridDragged, 1)
            compare(gridDropped, 1)
            compare(gridLaunched, 0)
        }
    }
}
