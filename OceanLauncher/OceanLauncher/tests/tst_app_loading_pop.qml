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
        name: "AppLoadingPop"
        when: windowShown

        function init() { loading.reset(); loading.reduceMotion = false; ready.clear() }
        function cleanup() { loading.reset() }

        function test_pop_starts_small_in_place_and_grows_to_the_loading_position() {
            loading.startPop("clock", "Clock", Qt.resolvedUrl("../assets/icons/Files.svg"))
            verify(loading.active)
            verify(loading.popping)
            verify(!loading.crossfade)
            compare(loading.selectedIcon, null)
            const icon = findChild(loading, "loadingIcon")
            compare(icon.width, 64)
            compare(icon.x + icon.width / 2, loading.width / 2)
            compare(icon.y + icon.height / 2, 430 + 65)
            tryCompare(ready, "count", 1)
            compare(loading.progress, 1)
            compare(icon.width, 130)
            compare(icon.x, (loading.width - 130) / 2)
            compare(icon.y, 430)
        }

        function test_pop_does_not_ask_for_a_launch_by_itself_when_told_not_to() {
            loading.launchOnReady = false
            loading.startPop("clock", "Clock", Qt.resolvedUrl("../assets/icons/Files.svg"))
            tryCompare(ready, "count", 1)
            compare(ready.signalArguments[0][0], "clock")
            compare(loading.launchOnReady, false)
            loading.beginHandoff()
            tryCompare(loading, "active", false)
            compare(loading.launchOnReady, true)
        }

        function test_reduced_motion_pop_is_immediate() {
            loading.reduceMotion = true
            loading.startPop("clock", "Clock", Qt.resolvedUrl("../assets/icons/Files.svg"))
            tryCompare(ready, "count", 1)
            compare(loading.progress, 1)
        }

        function test_a_second_start_is_ignored_while_popping() {
            loading.startPop("clock", "Clock", Qt.resolvedUrl("../assets/icons/Files.svg"))
            loading.startPop("other", "Other", Qt.resolvedUrl("../assets/icons/Files.svg"))
            compare(loading.appId, "clock")
            loading.startCrossfade("third", "Third", Qt.resolvedUrl("../assets/icons/Files.svg"))
            verify(!loading.crossfade)
        }

        function test_reset_clears_the_pop() {
            loading.startPop("clock", "Clock", Qt.resolvedUrl("../assets/icons/Files.svg"))
            loading.reset()
            verify(!loading.active)
            verify(!loading.popping)
            compare(loading.appId, "")
        }
    }
}
