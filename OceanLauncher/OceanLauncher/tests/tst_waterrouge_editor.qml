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
