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
        property var quickMenuOrder: ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
        property var quickMenuHiddenControls: []
        function setPolling(enabled) {}
        function setWifiEnabled(enabled) { wifiEnabled = enabled }
        function setBluetoothEnabled(enabled) { bluetoothEnabled = enabled }
        function setAirplaneMode(enabled) { airplaneMode = enabled }
        function setVolume(value) { volume = value }
        function setBrightness(value) { brightness = value }
        function setQuickMenuOrder(order) { quickMenuOrder = order; return true }
        function setQuickMenuHiddenControls(hidden) { quickMenuHiddenControls = hidden; return true }
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
        assetBase: Qt.resolvedUrl("../assets/topbar/QuickMenu/")
        regularFont: regularFont.name
        lightFont: lightFont.name
    }
    SignalSpy { id: lockActionSpy; target: menu; signalName: "lockRequested" }

    Rectangle {
        id: statusBackground
        width: parent.width
        height: 70
        z: 1
        color: "white"
        visible: false
    }
    Item {
        id: handleLayer
        anchors.fill: parent
        z: 2
    }

    TestCase {
        name: "QuickMenuGesture"
        when: windowShown

        function test_lightPageHandlePaintsAboveStatusBackground() {
            const handle = findChild(menu, "quickMenuHandle")
            const chevron = findChild(menu, "topChevron")
            try {
                menu.reduceMotion = true
                menu.settle(false)
                menu.appVisible = true
                menu.handleLayer = handleLayer
                menu.handleColor = "black"
                statusBackground.visible = true
                compare(handle.parent, handleLayer)
                compare(Math.round(handle.y + handle.height / 2), 35)
                compare(chevron.color, Qt.color("black"))
                waitForRendering(handle)
                const image = grabImage(window)
                let darkPixels = 0
                for (let y = 27; y < 44; y++) {
                    for (let x = 370; x < 399; x++) {
                        const pixel = image.pixel(x, y)
                        if (pixel.r < 0.2 && pixel.g < 0.2 && pixel.b < 0.2)
                            darkPixels++
                    }
                }
                verify(darkPixels > 15, "The black chevron must paint above the white status bar")
                mouseClick(handle, handle.width / 2, handle.height / 2)
                tryCompare(menu, "progress", 1)
                compare(Math.round(handle.y + handle.height / 2), 1112)
            } finally {
                menu.settle(false)
                menu.appVisible = false
                menu.handleColor = "white"
                menu.handleLayer = null
                statusBackground.visible = false
            }
        }

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

        function test_lock_action_requests_a_trusted_session_lock() {
            menu.reduceMotion = true
            menu.settle(true)
            const action = findChild(menu, "quickMenuLockAction")
            verify(action !== null)
            verify(action.enabled)
            mouseClick(action, action.width / 2, action.height / 2)
            compare(lockActionSpy.count, 1)
            menu.settle(false)
            menu.reduceMotion = false
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
            const clock = findChild(menu, "quickMenuClock")
            const lock = findChild(menu, "quickMenuLockAction")
            menu.reduceMotion = false
            menu.progress = 0.22
            compare(Math.round(chevron.rotation), 180)
            verify(menu.contentOpacity > 0 && menu.contentOpacity < 1)
            verify(clock.opacity > 0 && clock.opacity < 1)
            menu.progress = 0.45
            verify(findChild(menu, "quickMenuSlot_wifi").opacity > 0.9)
            verify(findChild(menu, "quickMenuSlot_bluetooth").opacity < 1)
            menu.progress = 0.65
            verify(findChild(menu, "quickMenuSlot_lock").opacity > 0
                && findChild(menu, "quickMenuSlot_lock").opacity < 1)
            menu.progress = 0.9
            verify(chevron.rotation > 0 && chevron.rotation < 180)
            menu.progress = 1
            compare(Math.round(chevron.rotation), 0)
            tryCompare(findChild(menu, "quickMenuSlot_lock"), "opacity", 1)
            menu.progress = 0
            compare(menu.contentOpacity, 0)
            compare(clock.opacity, 0)
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
                for (let y = 410; y <= 450; y += 5) {
                    for (let x = 450; x <= 500; x += 5) {
                    const pixel = image.pixel(x, y)
                    if (pixel.r > 0.9 && pixel.g > 0.9 && pixel.b > 0.9)
                        whitePixels++
                }
            }
            verify(whitePixels > 2, "The airplane glyph must paint inside its tile")
            menu.progress = 0
        }

        function test_defaultControlOrderAndLayoutCustomization() {
            compare(menu.controlOrder, ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"])
            menu.appController = radios
            menu.settle(true)
            verify(findChild(menu, "quickWifi") !== null)
            verify(findChild(menu, "quickBluetooth") !== null)
            verify(findChild(menu, "quickAirplane") !== null)
            verify(findChild(menu, "quickVolume") !== null)
            verify(findChild(menu, "quickBrightness") !== null)
            verify(findChild(menu, "quickMenuLockAction") !== null)
            compare(findChild(menu, "quickMenuSlot_wifi").x, 0)
            compare(findChild(menu, "quickMenuSlot_bluetooth").x, 180)
            compare(findChild(menu, "quickMenuSlot_airplane").x, 360)
            compare(findChild(menu, "quickMenuSlot_lock").x, 540)
            compare(findChild(menu, "quickMenuSlot_volume").y, 260)
            compare(findChild(menu, "quickMenuSlot_brightness").y, 386)
            compare(findChild(menu, "quickMenuDoneEditing").visible, false)
            radios.quickMenuOrder = ["lock", "brightness", "volume", "airplane", "bluetooth", "wifi"]
            radios.quickMenuHiddenControls = ["wifi"]
            compare(menu.controlOrder, ["lock", "brightness", "volume", "airplane", "bluetooth", "wifi"])
            compare(menu.visibleControlOrder, ["lock", "brightness", "volume", "airplane", "bluetooth"])
            verify(findChild(menu, "quickWifi") === null)
            radios.quickMenuHiddenControls = []
            menu.settle(false)
        }

        function test_longPressEntersInlineEditingAndPersistsVisibilityAndOrder() {
            menu.reduceMotion = true
            menu.appController = radios
            radios.quickMenuOrder = ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
            radios.quickMenuHiddenControls = []
            menu.settle(true)

            const wifiSlot = findChild(menu, "quickMenuSlot_wifi")
            verify(wifiSlot !== null)
            mousePress(wifiSlot, wifiSlot.width / 2, wifiSlot.height / 2)
            wait(950)
            mouseRelease(wifiSlot, wifiSlot.width / 2, wifiSlot.height / 2)
            tryCompare(menu, "editMode", true)
            compare(radios.wifiEnabled, true, "Long-pressing a control must not activate it")
            compare(findChild(menu, "quickMenuDoneEditing").visible, true)
            compare(menu.displayedControlOrder.length, 6)

            compare(radios.quickMenuHiddenControls, [], "Reordering should not toggle control visibility")

            const hideWifi = findChild(menu, "quickMenuVisibility_wifi")
            verify(hideWifi.visible)
            verify(hideWifi.enabled)
            const hidePosition = hideWifi.mapToItem(window, hideWifi.width / 2, hideWifi.height / 2)
            mouseClick(window, hidePosition.x, hidePosition.y)
            compare(radios.quickMenuHiddenControls, ["wifi"])
            compare(menu.visibleControlOrder.includes("wifi"), false)
            compare(menu.displayedControlOrder.includes("wifi"), true)

            for (const id of ["bluetooth", "airplane", "volume", "brightness"])
                menu.setControlHidden(id, true)
            menu.setControlHidden("lock", true)
            compare(menu.visibleControlOrder, ["lock"], "Keep one control visible so editing remains reachable")

            menu.moveControl("lock", 0)
            compare(radios.quickMenuOrder[0], "lock")
            compare(findChild(menu, "quickMenuSlot_lock").index, 0)
            mouseClick(findChild(menu, "quickMenuDoneEditing"))
            compare(menu.editMode, false)
            compare(menu.displayedControlOrder.includes("wifi"), false)

            radios.quickMenuOrder = ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
            radios.quickMenuHiddenControls = []
            menu.appController = null
            menu.settle(false)
            menu.reduceMotion = false
        }

        function test_reorderDropUsesTheSlotUnderTheFingerAcrossDifferentSizes() {
            menu.reduceMotion = true
            menu.appController = radios
            radios.quickMenuOrder = ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
            radios.quickMenuHiddenControls = []
            menu.settle(true)
            menu.beginEditing()
            verify(menu.editMode)

            let source = findChild(menu, "quickMenuSlot_wifi")
            let target = findChild(menu, "quickMenuSlot_airplane")
            menu.dropControl("wifi", 0, 0, source)
            compare(radios.quickMenuOrder,
                ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"],
                "A press without a real drag must leave the order unchanged")
            menu.dropControl("wifi", target.x + target.width / 2 - source.x - source.width / 2,
                target.y + target.height / 2 - source.y - source.height / 2, source)
            compare(radios.quickMenuOrder,
                ["bluetooth", "airplane", "wifi", "lock", "volume", "brightness"],
                "Dropping on a tile should place the dragged item at that tile's slot")

            source = findChild(menu, "quickMenuSlot_brightness")
            target = findChild(menu, "quickMenuSlot_wifi")
            menu.dropControl("brightness", target.x + target.width / 2 - source.x - source.width / 2,
                target.y + target.height / 2 - source.y - source.height / 2, source)
            compare(radios.quickMenuOrder,
                ["bluetooth", "airplane", "brightness", "wifi", "lock", "volume"],
                "A full-width slider should target the tile under the pointer, not a nearby center")

            menu.endEditing()
            radios.quickMenuOrder = ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
            menu.appController = null
            menu.settle(false)
            menu.reduceMotion = false
        }

        function test_fastReleaseVelocitySettlesDirectionally() {
            menu.reduceMotion = false
            menu.progress = 0.18
            menu.settle(menu.progress >= 0.5, 1200)
            compare(menu.open, false)
            menu.progress = 0.42
            menu.settle(true, 1200)
            compare(menu.open, true)
            menu.settle(false, -1200)
            compare(menu.open, false)
            menu.reduceMotion = true
            menu.progress = 0
        }
    }
}
