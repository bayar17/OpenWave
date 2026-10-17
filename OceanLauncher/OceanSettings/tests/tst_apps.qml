import QtQuick
import QtTest
import ".."

TestCase {
    name: "AppsPage"
    visible: true
    when: windowShown
    width: 672; height: 1000
    QtObject {
        id: apps
        property bool busy: false
        property bool uninstallBusy: false
        property string requestedUninstall: ""
        signal uninstalled(string appId)
        function requestUninstall(id) { requestedUninstall = id }
        function cancelUninstallPrompt() {}
        property string error: ""
        property var applications: [
            {appId: "browser", name: "Browser", trusted: false, icon: ""},
            {appId: "files", name: "Files", trusted: true, icon: ""},
            {appId: "settings", name: "Settings", trusted: true, icon: ""},
            {appId: "webapp", name: "Website", trusted: false, canUninstall: true, icon: ""}]
        function refresh() {}
    }
    QtObject {
        id: policies
        property bool busy: false
        property string error: ""
        property var records: [{application: "browser", permission: "files.read", name: "Browser", label: "Open selected files", state: "denied", trusted: false, supported: true, requestedAt: 1000}]
        function refreshApplications() {}
    }
    Component { id: view; AppsPage { width: 672; height: 1000; backend: apps; permissions: policies; reducedMotion: true } }
    property var page
    function init() { apps.uninstallBusy = false; apps.requestedUninstall = ""; page = createTemporaryObject(view, this); verify(page); wait(10) }
    function test_listsRegularAndSystemApps() {
        verify(findChild(page, "settingsApp_browser")); verify(findChild(page, "settingsApp_files")); verify(findChild(page, "settingsApp_settings"))
    }
    function test_appDetailsAndPermissionsNavigation() {
        mouseClick(findChild(page, "settingsApp_browser"))
        compare(page.depth, 1); compare(page.applicationId, "browser")
        compare(page.requestedPermissions.length, 1)
        mouseClick(findChild(page, "settingsAppPermissions"))
        compare(page.depth, 2)
        const permissions = findChild(page, "settingsAppPermissionsPage")
        compare(permissions.entries.length, 1); compare(permissions.entries[0].state, "denied")
        verify(page.back()); compare(page.depth, 1)
        verify(page.back()); compare(page.depth, 0)
        compare(page.back(), false)
    }
    function test_systemAppWithNoRequests() {
        mouseClick(findChild(page, "settingsApp_settings"))
        compare(page.depth, 1); compare(page.requestedPermissions.length, 0)
        mouseClick(findChild(page, "settingsAppPermissions"))
        const permissions = findChild(page, "settingsAppPermissionsPage")
        verify(permissions.trustedApplication)
        verify(findChild(permissions, "permissionsEmptyState").visible)
    }
    function test_uninstallOnlyForWebApps() {
        page.openApplication(apps.applications[0]); wait(10)
        verify(!findChild(page, "settingsAppUninstall").visible)
        page.openApplication(apps.applications[2]); wait(10)
        verify(!findChild(page, "settingsAppUninstall").visible)
        page.openApplication(apps.applications[3]); wait(10)
        const action = findChild(page, "settingsAppUninstall")
        verify(action.visible); verify(action.enabled)
        mouseClick(action); compare(apps.requestedUninstall, "webapp")
        apps.uninstallBusy = true; verify(!action.enabled)
        apps.uninstalled("webapp"); compare(page.depth, 0); compare(page.applicationId, "")
    }
    function test_leavingTabResetsNavigation() {
        page.openApplication(apps.applications[0]); page.depth = 2; page.visible = false
        compare(page.depth, 0); compare(page.applicationId, "")
    }
}
