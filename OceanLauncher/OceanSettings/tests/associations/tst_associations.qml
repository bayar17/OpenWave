import QtQuick
import QtTest
import "../.." as Settings
import "../../../OceanFiles" as Files

Item {
    width: 768; height: 1366
    QtObject {
        id: backend
        property var fileTypes: []
        property string fileTypesError: ""
        function refreshFileTypes() {}
        function setFileDefault(type, id) {
            fileTypes = fileTypes.map(t => Object.assign({}, t, t.extension === type ? {defaultId: id, defaultName: id ? "Viewer" : ""} : {}))
        }
    }
    QtObject {
        id: prompts
        property bool busy: false
        property var options: ({})
        signal finished(string requestId, string result, string text)
        function selection(title, message, choices) { options = choices; busy = true }
        function cancel() { busy = false; finished("1", "cancelled", "") }
        function accept(value) { busy = false; finished("1", "accepted", value) }
    }
    Settings.AppDefaultsPage { id: files; width: 360; height: 700; backend: backend; prompts: prompts }
    Settings.AppDefaultsPage { id: settings; x: 380; width: 360; height: 700; backend: backend; prompts: prompts }
    Files.OpenWithChooser { id: chooser; y: 740; width: 548; height: implicitHeight }
    SignalSpy { id: opened; target: chooser; signalName: "openRequested" }
    TestCase {
        name: "AssociationInterfaces"; when: windowShown
        function init() {
            files.applicationId = ""; settings.applicationId = ""
            backend.fileTypes = [
                {extension: "application/pdf", label: ".pdf", handlers: [{id: "org.test.viewer", name: "Viewer"}], defaultId: "", defaultName: "", protectedType: false},
                {extension: "application/x-rpm", label: ".rpm", handlers: [{id: "system.package-installer", name: "Installer"}], defaultId: "", defaultName: "", protectedType: true}
            ]
            chooser.selectedId = ""; chooser.busy = false
            chooser.handlers = [{id: "org.test.viewer", name: "Viewer"}]
            opened.clear()
            wait(0)
        }
        function test_changes_visible_in_both_interfaces() {
            let a = findChild(files, "settingsFileType_application/pdf")
            let b = findChild(settings, "settingsFileType_application/pdf")
            compare(a.detail, "No default"); compare(b.detail, "No default")
            files.choose(backend.fileTypes[0]); prompts.accept("org.test.viewer")
            a = findChild(files, "settingsFileType_application/pdf"); b = findChild(settings, "settingsFileType_application/pdf")
            compare(a.detail, "Viewer"); compare(b.detail, "Viewer")
            settings.choose(backend.fileTypes[0]); prompts.accept("none")
            a = findChild(files, "settingsFileType_application/pdf"); b = findChild(settings, "settingsFileType_application/pdf")
            compare(a.detail, "No default"); compare(b.detail, "No default")
        }
        function test_packages_cannot_be_defaults() {
            verify(!findChild(files, "settingsFileType_application/x-rpm").enabled)
            verify(!findChild(settings, "settingsFileType_application/x-rpm").enabled)
        }
        function test_app_details_filter() {
            settings.applicationId = "org.test.viewer"; wait(0)
            verify(findChild(settings, "settingsFileType_application/pdf") !== null)
            verify(findChild(settings, "settingsFileType_application/x-rpm") === null)
        }
        function test_first_open_even_one_handler_requires_choice() {
            verify(!findChild(chooser, "filesJustOnce").enabled)
            verify(!findChild(chooser, "filesAlways").enabled)
            compare(opened.count, 0)
        }
        function test_just_once_and_always_are_distinct() {
            mouseClick(findChild(chooser, "filesHandler_org.test.viewer"))
            mouseClick(findChild(chooser, "filesJustOnce"))
            compare(opened.count, 1); compare(opened.signalArguments[0][0], "org.test.viewer"); compare(opened.signalArguments[0][1], false)
            mouseClick(findChild(chooser, "filesAlways"))
            compare(opened.count, 2); compare(opened.signalArguments[1][1], true)
        }
        function test_removed_selection_cannot_be_opened() {
            chooser.selectedId = "org.test.viewer"; chooser.handlers = []; wait(0)
            compare(chooser.selectedId, ""); verify(!findChild(chooser, "filesAlways").enabled)
        }
    }
}
