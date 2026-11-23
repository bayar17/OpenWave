import QtQuick
import QtTest
import ".."

Item {
    width: 768
    height: 1366
    Rectangle { id: wallpaper; anchors.fill: parent; color: "#454e53" }
    AppLoading { id: loading; anchors.fill: parent; wallpaper: wallpaper }
    SignalSpy { id: ready; target: loading; signalName: "ready" }

    TestCase {
        name: "ExternalAppCrossfade"
        when: windowShown

        function init() { loading.reset(); loading.reduceMotion = false; ready.clear() }
        function cleanup() { loading.reset() }

        function test_source_to_loading_then_application() {
            loading.startCrossfade("files", "Files", Qt.resolvedUrl("../assets/icons/Files.svg"))
            verify(loading.active)
            verify(loading.crossfade)
            compare(loading.selectedIcon, null)
            compare(ready.count, 0)
            const icon = findChild(loading, "loadingIcon")
            const title = findChild(loading, "loadingTitle")
            const x = icon.x, y = icon.y, titleX = title.x, titleY = title.y
            wait(100)
            verify(loading.opacity > 0 && loading.opacity < 1)
            compare(icon.x, x); compare(icon.y, y)
            compare(icon.width, 130); compare(icon.height, 130)
            compare(title.x, titleX); compare(title.y, titleY)
            compare(title.font.pixelSize, 34)
            compare(ready.count, 0)
            tryCompare(ready, "count", 1)
            compare(loading.progress, 1)
            compare(loading.opacity, 1)
            verify(!loading.handingOff)
            wait(80)
            verify(loading.active) // Remains visible while the target starts.
            loading.beginHandoff()
            wait(100)
            verify(loading.handoffProgress > 0 && loading.handoffProgress < 1)
            verify(loading.active)
            compare(loading.opacity, 1) // Incoming app blends over an opaque loader.
            loading.beginHandoff() // Repeated first-frame signals cannot restart it.
            tryCompare(loading, "active", false)
            compare(loading.appId, "")
        }

        function test_cancel_stops_launch_and_can_restart() {
            loading.startCrossfade("files", "Files", "")
            wait(60)
            loading.reset()
            wait(360)
            compare(ready.count, 0)
            verify(!loading.active)
            loading.startCrossfade("files", "Files", "")
            tryCompare(ready, "count", 1)
        }

        function test_reduced_motion_keeps_phase_order() {
            loading.reduceMotion = true
            loading.startCrossfade("files", "Files", "")
            tryCompare(ready, "count", 1)
            compare(loading.progress, 1)
            verify(loading.active)
            loading.beginHandoff()
            tryCompare(loading, "active", false)
        }
    }
}
