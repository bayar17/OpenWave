import QtQuick
import QtTest
import ".."

Item {
    id: window
    width: 768
    height: 1366

    AppearanceNotice {
        id: notice
        anchors.fill: parent
        fadeInDuration: 150
        holdDuration: 200
        fadeOutDuration: 150
    }
    SignalSpy { id: shownSpy; target: notice; signalName: "shown" }
    SignalSpy { id: finishedSpy; target: notice; signalName: "finished" }

    TestCase {
        name: "AppearanceNotice"
        when: windowShown

        function init() {
            notice.cancel()
            notice.reduceMotion = false
            shownSpy.clear()
            finishedSpy.clear()
        }

        function test_it_is_hidden_and_does_not_block_input_by_default() {
            compare(notice.opacity, 0)
            verify(!notice.visible)
            verify(!notice.enabled)
        }

        function test_the_name_fades_in_holds_and_fades_out() {
            notice.show("Dusk")
            compare(shownSpy.count, 1)
            compare(findChild(notice, "appearanceNoticeText").text, "Dusk")
            tryVerify(() => notice.opacity > 0 && notice.opacity < 1)
            tryCompare(notice, "opacity", 1)
            verify(notice.visible)
            tryVerify(() => notice.opacity < 1 && notice.opacity > 0)
            tryCompare(finishedSpy, "count", 1)
            compare(notice.opacity, 0)
            verify(!notice.visible)
        }

        function test_the_name_is_centered_on_screen() {
            notice.show("Ocean")
            tryCompare(notice, "opacity", 1)
            const label = findChild(notice, "appearanceNoticeText")
            const center = label.mapToItem(window, label.width / 2, label.height / 2)
            verify(Math.abs(center.x - window.width / 2) < 2)
            verify(Math.abs(center.y - window.height / 2) < 2)
            tryCompare(finishedSpy, "count", 1)
        }

        function test_the_label_eases_in_with_a_small_scale_and_drift() {
            notice.show("Space")
            const label = findChild(notice, "appearanceNoticeText")
            verify(label.scale < 1)
            verify(label.yOffset > 0)
            tryCompare(label, "scale", 1)
            tryCompare(label, "yOffset", 0)
            tryCompare(finishedSpy, "count", 1)
        }

        function test_reduced_motion_shows_and_hides_without_fading() {
            notice.reduceMotion = true
            notice.show("Purple")
            compare(notice.opacity, 1)
            const label = findChild(notice, "appearanceNoticeText")
            compare(label.scale, 1)
            compare(label.yOffset, 0)
            tryCompare(finishedSpy, "count", 1)
            compare(notice.opacity, 0)
        }

        function test_showing_again_restarts_with_the_new_name() {
            notice.show("First")
            tryCompare(notice, "opacity", 1)
            notice.show("Second")
            compare(shownSpy.count, 2)
            compare(findChild(notice, "appearanceNoticeText").text, "Second")
            tryCompare(finishedSpy, "count", 1)
            compare(finishedSpy.count, 1)
        }

        function test_cancel_hides_immediately_without_finishing() {
            notice.show("Cancelled")
            tryCompare(notice, "opacity", 1)
            notice.cancel()
            compare(notice.opacity, 0)
            wait(400)
            compare(finishedSpy.count, 0)
        }

        function test_long_names_are_elided_within_the_screen() {
            notice.show("An appearance with a remarkably long name that cannot fit on one line of the screen")
            tryCompare(notice, "opacity", 1)
            const label = findChild(notice, "appearanceNoticeText")
            verify(label.width <= window.width - 80)
            tryCompare(finishedSpy, "count", 1)
        }
    }
}
