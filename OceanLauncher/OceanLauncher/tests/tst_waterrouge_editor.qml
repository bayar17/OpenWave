import QtQuick
import QtTest
import ".."

Item {
    width: 768
    height: 1366

    WaterRougeQuickMenuEditor {
        id: editor
        anchors.fill: parent
        reduceMotion: true
    }

    SignalSpy { id: applied; target: editor; signalName: "applyRequested" }
    SignalSpy { id: cancelled; target: editor; signalName: "cancelRequested" }

    TestCase {
        name: "WaterRougeQuickMenuEditor"
        when: windowShown

        function test_default_layout() {
            editor.beginEdit([], [])
            compare(editor.currentOrder().join(","), "wifi,bluetooth,airplane,volume,brightness,lock")
            compare(editor.currentHidden().length, 0)
            verify(findChild(editor, "waterRougeEditorSheet") !== null)
            editor.cancel()
        }

        function test_reordering_visibility_and_apply() {
            applied.clear()
            editor.beginEdit(["volume", "wifi", "lock"], ["lock"])
            compare(editor.currentOrder().join(","), "volume,wifi,lock,bluetooth,airplane,brightness")
            compare(editor.currentHidden().join(","), "lock")
            editor.moveControl(0, 2)
            compare(editor.currentOrder().join(","), "wifi,lock,volume,bluetooth,airplane,brightness")
            editor.toggleVisibility("lock")
            compare(editor.currentHidden().length, 0)
            editor.accept()
            compare(applied.count, 1)
            compare(editor.editing, false)
        }

        function test_done_button_is_clickable() {
            applied.clear()
            editor.beginEdit(["lock", "wifi"], ["bluetooth"])
            const done = findChild(editor, "waterRougeDone")
            verify(done !== null)
            verify(done.visible)
            verify(done.enabled)
            waitForRendering(done)
            mouseClick(done, done.width / 2, done.height / 2, Qt.LeftButton)
            tryCompare(applied, "count", 1)
            compare(editor.editing, false)
        }

        function test_cancel_button_is_clickable() {
            cancelled.clear()
            editor.beginEdit([], [])
            const cancel = findChild(editor, "waterRougeCancel")
            verify(cancel !== null)
            verify(cancel.enabled)
            mouseClick(cancel, cancel.width / 2, cancel.height / 2, Qt.LeftButton)
            tryCompare(cancelled, "count", 1)
            compare(editor.editing, false)
        }

        function test_restore_button_is_clickable() {
            editor.beginEdit(["lock", "volume"], ["wifi"])
            const restore = findChild(editor, "waterRougeRestoreDefaults")
            verify(restore !== null)
            mouseClick(restore, restore.width / 2, restore.height / 2, Qt.LeftButton)
            compare(editor.currentOrder().join(","), "wifi,bluetooth,airplane,volume,brightness,lock")
            compare(editor.currentHidden().length, 0)
            editor.cancel()
        }

        function test_restore_and_cancel() {
            cancelled.clear()
            editor.beginEdit(["lock", "volume"], ["wifi"])
            editor.restoreDefaults()
            compare(editor.currentOrder().join(","), "wifi,bluetooth,airplane,volume,brightness,lock")
            compare(editor.currentHidden().length, 0)
            editor.cancel()
            compare(cancelled.count, 1)
        }

        function test_invalid_duplicate_entries() {
            editor.beginEdit(["wifi", "wifi", "unknown", "lock"], ["wifi"])
            compare(editor.currentOrder().join(","), "wifi,lock,bluetooth,airplane,volume,brightness")
            compare(editor.currentHidden().join(","), "wifi")
            editor.cancel()
        }
    }
}
