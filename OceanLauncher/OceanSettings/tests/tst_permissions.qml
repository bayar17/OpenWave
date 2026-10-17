import QtQuick
import QtTest
import ".."

TestCase {
    name: "PermissionsPage"
    visible: true
    when: windowShown
    width: 672; height: 1000
    ListModel {
        id: policies
        property bool busy: false
        property string error: ""
        property int refreshes: 0
        property var updates: []
        property var records: []
        function refreshApplications() { refreshes++ }
        function setPermission(application, permission, state) { updates = updates.concat([{application: application, permission: permission, state: state}]) }
    }
    QtObject {
        id: mockPrompts
        property bool busy: false
        property string kind: ""
        property var options: ({})
        signal finished(string requestId, string result, string text)
        function selection(title, message, value) { kind = "selection"; options = value; busy = true }
        function confirmation(title, message, value) { kind = "confirmation"; options = value; busy = true }
        function cancel() { busy = false; finished("test", "cancelled", "") }
        function answer(result, text) { busy = false; finished("test", result, text) }
    }
    Component { id: view; PermissionsPage { width: 672; height: 1000; client: policies; prompts: mockPrompts } }
    property var page
    function init() {
        policies.clear(); policies.updates = []; policies.error = ""; policies.busy = false
        mockPrompts.busy = false; mockPrompts.kind = ""
        policies.append({applicationId: "browser", applicationName: "Browser", permissionId: "files.read",
            permissionLabel: "Open selected files", permissionState: "not-determined", grantDuration: "permanent", trustedComponent: false})
        policies.append({applicationId: "files", applicationName: "Files", permissionId: "files.read",
            permissionLabel: "Open selected files", permissionState: "not-determined", grantDuration: "permanent", trustedComponent: true})
        policies.records = [
            {application: "browser", name: "Browser", permission: "files.read", label: "Open selected files", state: "not-determined", duration: "permanent", trusted: false, requestedAt: 1000, supported: true},
            {application: "browser", name: "Browser", permission: "files.write", label: "Save files", state: "granted", duration: "permanent", trusted: false, requestedAt: 0, supported: true},
            {application: "files", name: "Files", permission: "files.read", label: "Open selected files", state: "granted", duration: "permanent", trusted: true, requestedAt: 1000, supported: true}]
        page = createTemporaryObject(view, this); verify(page)
        wait(20)
    }
    function test_denyRequiresConfirmation() {
        page.edit("browser", "files.read", "Browser", "not-determined")
        compare(mockPrompts.kind, "selection"); compare(mockPrompts.options.initialText, "not-determined")
        compare(Object.keys(mockPrompts.options).sort(), ["acceptLabel", "cancelLabel", "choices", "initialText"])
        compare(mockPrompts.options.acceptLabel, "Apply"); compare(mockPrompts.options.cancelLabel, "Cancel")
        compare(Object.keys(mockPrompts.options.choices).sort(), ["denied", "granted", "not-determined", "restricted"])
        mockPrompts.answer("accepted", "denied")
        compare(mockPrompts.kind, "confirmation"); compare(policies.updates.length, 0)
        compare(Object.keys(mockPrompts.options).sort(), ["acceptLabel", "cancelLabel"])
        compare(mockPrompts.options.acceptLabel, "Change"); compare(mockPrompts.options.cancelLabel, "Cancel")
        mockPrompts.answer("accepted", "")
        compare(policies.updates.length, 1); compare(policies.updates[0].state, "denied")
        compare(policies.updates[0].application, "browser"); compare(policies.updates[0].permission, "files.read")
    }
    function test_cancelDoesNotChangePolicy() {
        page.edit("browser", "files.read", "Browser", "not-determined")
        mockPrompts.answer("accepted", "restricted")
        mockPrompts.answer("cancelled", "")
        compare(policies.updates.length, 0)
    }
    function test_allowUsesVerifiedBackend() {
        page.edit("browser", "files.read", "Browser", "not-determined")
        mockPrompts.answer("accepted", "granted")
        compare(policies.updates.length, 1); compare(policies.updates[0].state, "granted")
    }
    function test_hiddenPageCancelsPendingPrompt() {
        page.edit("browser", "files.read", "Browser", "not-determined")
        page.visible = false
        compare(mockPrompts.busy, false); compare(policies.updates.length, 0)
    }
    function test_appShowsOnlyItsRequestedPermissions() {
        page.applicationId = "browser"; page.requestedOnly = true
        compare(page.entries.length, 1); compare(page.entries[0].permission, "files.read")
        compare(findChild(page, "permissionRows").count, 1)
    }
    function test_noRequestsShowsEmptyState() {
        page.applicationId = "new-app"; page.requestedOnly = true
        verify(findChild(page, "permissionsEmptyState").visible)
        compare(page.entries.length, 0)
    }
    function test_declaredUnavailablePermissionAppearsBeforeItsFirstRequest() {
        page.applicationId = "browser"; page.requestedOnly = true
        policies.records = [{application: "browser", name: "Browser", permission: "camera", label: "Camera",
            state: "restricted", trusted: false, requestedAt: 0, supported: false, declared: true}]
        compare(page.entries.length, 1)
        compare(findChild(page, "permissionRows").itemAt(0).enabled, false)
        verify(!findChild(page, "permissionsEmptyState").visible)
    }
    function test_trustedAppAndUnsupportedPermissionCannotBeChanged() {
        page.applicationId = "files"; page.requestedOnly = true
        compare(findChild(page, "permissionRows").itemAt(0).enabled, false)
        policies.records = [{application: "files", name: "Files", permission: "camera", label: "camera", state: "restricted", trusted: false, requestedAt: 1000, supported: false}]
        compare(findChild(page, "permissionRows").itemAt(0).enabled, false)
    }
}
