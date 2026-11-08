import QtQuick
import QtTest
import ".."

Item {
    id: scene
    width: 768
    height: 1366

    QtObject {
        id: controller
        property int state: 1
        property bool locked: state !== 0
        property bool unlockRequestActive: state === 2 || state === 3 || state === 4
        property bool unlockAuthorized: state === 4

        function requestDevelopmentSwipeUnlock() {
            if (state !== 1) return false
            state = 2
            Qt.callLater(() => {
                if (state !== 2) return
                state = 3
                Qt.callLater(() => { if (state === 3) state = 4 })
            })
            return true
        }
        function finishDevelopmentSwipeUnlock() {
            if (state !== 4) return false
            state = 0
            return true
        }
    }

    Image {
        id: wallpaper
        anchors.fill: parent
        source: Qt.resolvedUrl("../assets/wallpaper.png")
    }

    MouseArea {
        objectName: "underlyingShell"
        anchors.fill: parent
        z: 0
        property int clickCount: 0
        onClicked: clickCount++
    }

    LockScreen {
        id: lock
        anchors.fill: parent
        z: 1
        controller: controller
        wallpaper: wallpaper
        timeText: "12:12 PM"
        dateText: "Oct 23, 2026 - Friday"
        signalAssetBase: Qt.resolvedUrl("../assets/topbar/TopBar/")
        reduceMotion: true
        safeTop: 70
    }

    TestCase {
        name: "LockScreen"
        when: windowShown

        function init() {
            scene.width = 768
            scene.height = 1366
            controller.state = 1
            lock.dragOffset = 0
            lock.unlocking = false
            lock.reduceMotion = true
        }

        function test_clock_date_and_preview_are_visible() {
            verify(lock.visible)
            compare(findChild(lock, "lockClock").text, "12:12 PM")
            compare(findChild(lock, "lockDate").text, "Oct 23, 2026 - Friday")
            verify(findChild(lock, "developmentPreviewLabel") !== null)
            verify(findChild(lock, "lockDimmingOverlay") === null)
            const topGradient = findChild(lock, "lockTopGradient")
            const bottomGradient = findChild(lock, "lockBottomGradient")
            verify(topGradient !== null && topGradient.height < lock.height * 0.5)
            verify(bottomGradient !== null && bottomGradient.height < lock.height * 0.4)
            const topChevron = findChild(lock, "lockTopChevron")
            verify(Math.abs(topChevron.x + topChevron.width / 2 - lock.width / 2) < 1)
        }

        function test_taps_do_not_reach_the_underlying_shell() {
            const background = findChild(scene, "underlyingShell")
            mouseClick(scene, 320, 650)
            compare(background.clickCount, 0)
        }

        function test_landscape_resizes_clock_and_status_area() {
            scene.width = 1366
            scene.height = 768
            const clock = findChild(lock, "lockClock")
            const date = findChild(lock, "lockDate")
            const status = findChild(lock, "lockStatusIndicators")
            verify(clock.width > 0 && clock.height > 0)
            verify(date.x + date.width <= lock.width)
            verify(status.width <= lock.width)
            verify(lock.visible)
        }

        function test_short_bottom_swipe_restores_locked_screen() {
            mouseDrag(scene, 384, 1280, 0, -90, Qt.LeftButton)
            compare(controller.state, 1)
            tryCompare(lock, "dragOffset", 0)
            verify(lock.visible)
        }

        function test_live_reveal_tracks_holds_reverses_and_returns() {
            lock.reduceMotion = false
            mousePress(scene, 384, 1280, Qt.LeftButton)
            mouseMove(scene, 384, 1060, 120, Qt.LeftButton)
            verify(lock.revealProgress > 0.15 && lock.revealProgress < 0.5)
            const heldProgress = lock.revealProgress
            wait(120)
            compare(lock.revealProgress, heldProgress)
            mouseMove(scene, 384, 1160, 120, Qt.LeftButton)
            verify(lock.revealProgress < heldProgress)
            mouseRelease(scene, 384, 1160, Qt.LeftButton)
            compare(controller.state, 1)
            tryCompare(lock, "revealProgress", 0, 1000)
            verify(lock.visible)
        }

        function test_completed_swipe_uses_development_unlock_flow() {
            mouseDrag(scene, 384, 1280, 0, -560, Qt.LeftButton)
            tryCompare(controller, "state", 0, 1000)
            tryCompare(lock, "visible", false, 1000)
            verify(lock.revealProgress > 0.9)
            verify(!lock.revealActive)
        }

        function test_reveal_waits_for_authorization_and_keeps_the_screen_shielded() {
            lock.requestUnlock()
            compare(controller.state, 2)
            verify(!lock.unlocking)
            tryCompare(controller, "state", 4, 1000)
            verify(lock.unlocking)
            verify(controller.locked)
            tryVerify(() => lock.revealProgress > 0.2)
            verify(findChild(lock, "lockWallpaperBase").opacity < 0.8)
            verify(controller.locked)
            tryCompare(controller, "state", 0, 1000)
            tryCompare(lock, "visible", false, 1000)
        }

        function test_swipe_must_begin_near_bottom() {
            mouseDrag(scene, 384, 600, 0, -800, Qt.LeftButton)
            compare(controller.state, 1)
            verify(lock.visible)
        }
    }
}
