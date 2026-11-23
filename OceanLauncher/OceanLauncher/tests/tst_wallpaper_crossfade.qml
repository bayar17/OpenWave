import QtQuick
import QtTest
import ".."

Item {
    id: window
    width: 400
    height: 700

    readonly property url first: Qt.resolvedUrl("../assets/wallpaper.png")
    readonly property url second: Qt.resolvedUrl("../../../userspace/quickmenu/tests/fixtures/appearances/space-wallpaper.png")
    readonly property url third: Qt.resolvedUrl("../../../userspace/quickmenu/tests/fixtures/appearances/ocean-wallpaper.png")

    CrossfadeWallpaper {
        id: wallpaper
        width: 400
        height: 700
        fadeDuration: 400
    }
    SignalSpy { id: started; target: wallpaper; signalName: "crossfadeStarted" }
    SignalSpy { id: finished; target: wallpaper; signalName: "crossfadeFinished" }

    TestCase {
        name: "CrossfadeWallpaper"
        when: windowShown

        function init() {
            wallpaper.reduceMotion = false
            wallpaper.fadeDuration = 400
            started.clear()
            finished.clear()
        }

        function test_a_first_source_appears_without_a_fade() {
            wallpaper.source = window.first
            tryCompare(wallpaper, "status", Image.Ready)
            compare(wallpaper.settledSource, window.first)
            compare(started.count, 0)
            compare(wallpaper.front.opacity, 1)
        }

        function test_changing_the_source_crossfades_to_the_new_image() {
            wallpaper.source = window.second
            tryCompare(started, "count", 1)
            compare(started.signalArguments[0][0], window.second)
            tryVerify(() => wallpaper.back.opacity > 0 && wallpaper.back.opacity < 1)
            verify(wallpaper.fading)
            tryCompare(finished, "count", 1)
            compare(wallpaper.settledSource, window.second)
            compare(wallpaper.front.source, window.second)
            compare(wallpaper.front.opacity, 1)
            compare(wallpaper.back.opacity, 0)
            tryCompare(wallpaper, "status", Image.Ready)
            verify(!wallpaper.fading)
        }

        function test_the_incoming_image_is_stacked_above_the_outgoing_one() {
            wallpaper.source = window.third
            tryCompare(started, "count", 1)
            verify(wallpaper.back.z > wallpaper.front.z)
            tryCompare(finished, "count", 1)
            verify(wallpaper.back.z > wallpaper.front.z)
            compare(wallpaper.settledSource, window.third)
        }

        function test_the_same_source_is_ignored() {
            const before = wallpaper.settledSource
            wallpaper.source = before
            wait(100)
            compare(started.count, 0)
            compare(finished.count, 0)
            compare(wallpaper.settledSource, before)
        }

        function test_unreadable_images_never_fade() {
            const before = wallpaper.settledSource
            wallpaper.source = Qt.resolvedUrl("../assets/does-not-exist.png")
            wait(300)
            compare(started.count, 0)
            compare(wallpaper.settledSource, before)
            compare(wallpaper.front.opacity, 1)
            wallpaper.source = before
            compare(wallpaper.settledSource, before)
        }

        function test_reduced_motion_swaps_without_a_visible_fade() {
            wallpaper.reduceMotion = true
            const target = wallpaper.settledSource === window.second ? window.first : window.second
            wallpaper.source = target
            tryCompare(finished, "count", 1, 1500)
            compare(wallpaper.settledSource, target)
            compare(wallpaper.front.opacity, 1)
            compare(wallpaper.back.opacity, 0)
        }

        function test_returning_to_the_settled_source_cancels_a_pending_fade() {
            const settled = wallpaper.settledSource
            const other = settled === window.second ? window.third : window.second
            wallpaper.source = other
            wallpaper.source = settled
            wait(700)
            compare(wallpaper.settledSource, settled)
            compare(wallpaper.front.source, settled)
            compare(wallpaper.front.opacity, 1)
        }

        function test_a_second_change_completes_the_running_fade_first() {
            const settled = wallpaper.settledSource
            const one = settled === window.second ? window.third : window.second
            const two = settled === window.first ? window.third : window.first
            wallpaper.source = one
            tryCompare(started, "count", 1)
            wallpaper.source = two === one ? window.first : two
            tryCompare(finished, "count", 2, 3000)
            compare(wallpaper.settledSource, wallpaper.source)
            compare(wallpaper.front.opacity, 1)
        }
    }
}
