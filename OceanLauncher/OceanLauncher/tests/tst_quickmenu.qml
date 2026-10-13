import QtQuick
import QtTest
import ".."

Item {
    id: window
    width: 768
    height: 1366

    FontLoader { id: regularFont; source: "../assets/fonts/Inter-Regular.otf" }
    FontLoader { id: lightFont; source: "../assets/fonts/Inter-Light.otf" }
    QtObject {
        id: radios
        property string modemLabel: "NO MODEM"
        property bool wifiEnabled: true
        property bool wifiAvailable: true
        property bool bluetoothEnabled: true
        property bool bluetoothAvailable: true
        property bool airplaneMode: false
        property int volume: 40
        property int brightness: 60
        function setPolling(enabled) {}
    }

    Image {
        id: wallpaper
        anchors.fill: parent
        source: "../assets/wallpaper.png"
    }

    QuickMenu {
        id: menu
        anchors.fill: parent
        wallpaper: wallpaper
        canOpen: true
        reduceMotion: true
        timeText: "12:12 PM"
        dateText: "Oct 4, 2026 - Sunday"
        backend: radios
        assetBase: Qt.resolvedUrl("../../OceanTopbard/assets/QuickMenu/")
        regularFont: regularFont.name
        lightFont: lightFont.name
    }

    TestCase {
        name: "QuickMenuGesture"
        when: windowShown

        function test_disabled_handle_stays_visible() {
            const handle = findChild(menu, "quickMenuHandle")
            menu.reduceMotion = true
            menu.settle(false)
            menu.canOpen = false
            menu.showDisabledHandle = true
            verify(handle.visible)
            verify(!handle.enabled)
            mouseClick(handle, handle.width / 2, handle.height / 2)
            compare(menu.progress, 0)
            menu.showDisabledHandle = false
            menu.canOpen = true
            menu.reduceMotion = false
        }

        function test_handle_tap_and_geometry() {
            compare(menu.progress, 0)
            const handle = findChild(menu, "quickMenuHandle")
            const panel = findChild(menu, "quickMenuPanel")
            const chevron = findChild(menu, "topChevron")
            verify(handle !== null)
            verify(panel !== null)
            verify(chevron !== null)
            compare(Math.round(chevron.rotation), 180)
            mouseClick(handle, handle.width / 2, handle.height / 2)
            tryCompare(menu, "progress", 1)
            compare(panel.height, 1145)
            compare(Math.round(chevron.rotation), 0)
            compare(Math.round(handle.y + handle.height / 2), 1112)
            mouseClick(handle, handle.width / 2, handle.height / 2)
            tryCompare(menu, "progress", 0)
        }

        function test_pull_down_and_back() {
            const handle = findChild(menu, "quickMenuHandle")
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, handle.height / 2 + 25, 20)
            mouseMove(handle, handle.width / 2, handle.height / 2 + 600, 30)
            verify(menu.progress > 0.25)
            mouseRelease(handle, handle.width / 2, handle.height / 2 + 600)
            tryCompare(menu, "progress", 1)
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, handle.height / 2 - 25, 20)
            mouseMove(handle, handle.width / 2, handle.height / 2 - 700, 30)
            mouseRelease(handle, handle.width / 2, handle.height / 2 - 700)
            tryCompare(menu, "progress", 0)
        }

        function test_arrival_rotation_and_crossfade() {
            const chevron = findChild(menu, "topChevron")
            const contents = findChild(menu, "quickMenuContents")
            menu.reduceMotion = false
            menu.progress = 0.70
            compare(Math.round(chevron.rotation), 180)
            menu.progress = 0.91
            verify(chevron.rotation > 0 && chevron.rotation < 180)
            verify(menu.contentOpacity > 0 && menu.contentOpacity < 1)
            menu.progress = 1
            compare(Math.round(chevron.rotation), 0)
            tryCompare(contents, "opacity", 1)
            menu.progress = 0
            tryCompare(contents, "opacity", 0)
            menu.reduceMotion = true
        }

        function test_sim_label_tracks_modem_state() {
            const label = findChild(menu, "quickModemLabel")
            const date = findChild(menu, "quickDate")
            compare(label.text, "NO MODEM")
            compare(date.text, "Oct 4, 2026 - Sunday")
            radios.modemLabel = "Vodafone DE - Unknown number"
            compare(label.text, radios.modemLabel)
            radios.modemLabel = "NO MODEM"
        }

        function test_airplane_icon_is_visible() {
            const tile = findChild(menu, "quickAirplane")
            verify(tile !== null)
            menu.progress = 1
            tryCompare(findChild(menu, "quickMenuContents"), "opacity", 1)
            waitForRendering(tile)
            const image = grabImage(window)
            let whitePixels = 0
            for (let y = 380; y <= 440; y += 5) {
                for (let x = 630; x <= 690; x += 5) {
                    const pixel = image.pixel(x, y)
                    if (pixel.r > 0.9 && pixel.g > 0.9 && pixel.b > 0.9)
                        whitePixels++
                }
            }
            verify(whitePixels > 2, "The airplane glyph must paint inside its tile")
            menu.progress = 0
        }
    }
}
