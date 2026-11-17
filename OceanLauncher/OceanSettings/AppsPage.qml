import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Item {
    id: page
    property var backend: null
    property var permissions: null
    property var prompts: null
    property bool largeText: false
    property bool reducedMotion: false
    property int depth: 0
    property var selectedApplication: ({})
    readonly property string applicationId: selectedApplication.appId || ""
    readonly property var requestedPermissions: permissions
        ? (permissions.records || []).filter(row => row.application === applicationId && (row.requestedAt > 0 || row.declared === true)) : []
    clip: true
    property string pendingApplicationId: ""
    function showApplication(appId) {
        pendingApplicationId = appId
        if (!visible) return
        for (const entry of (backend ? backend.applications : [])) {
            if (entry.appId === appId) {
                pendingApplicationId = ""
                depth = 0
                openApplication(entry)
                return
            }
        }
        if (backend) backend.refresh()
    }
    function openApplication(application) {
        selectedApplication = application
        depth = 1
        if (backend) { backend.loadDetails(application.appId); backend.refreshFileTypes() }
        if (permissions) permissions.refreshApplications()
    }
    function back() {
        if (!depth) return false
        if (prompts && prompts.busy) { prompts.cancel(); return true }
        if (backend) backend.cancelUninstallPrompt()
        depth = depth === 3 ? 1 : depth - 1
        return true
    }
    onVisibleChanged: {
        if (visible && pendingApplicationId) Qt.callLater(() => page.showApplication(page.pendingApplicationId))
        if (visible && backend) backend.refresh()
        if (visible && permissions) permissions.refreshApplications()
        if (!visible) { if (backend) backend.cancelUninstallPrompt(); if (prompts && prompts.busy && depth === 2) prompts.cancel(); depth = 0; selectedApplication = ({}) }
    }
    Connections {
        target: page.backend
        function onChanged() {
            if (page.pendingApplicationId) page.showApplication(page.pendingApplicationId)
            else if (page.applicationId) {
                const updated = page.backend.applications.find(a => a.appId === page.applicationId)
                if (updated) page.selectedApplication = updated
                else if (!page.backend.busy && !page.backend.error) { page.selectedApplication = ({}); page.depth = 0 }
            }
        }
        function onUninstalled(appId) {
            if (page.applicationId === appId) { page.depth = 0; page.selectedApplication = ({}) }
            if (page.permissions) page.permissions.refreshApplications()
        }
    }
    Flickable {
        objectName: "settingsAppsList"
        x: page.depth ? -page.width : 0
        width: page.width; height: page.height
        enabled: page.depth === 0
        contentHeight: list.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
        Column {
            id: list
            width: parent.width
            spacing: 12
            Text {
                text: qsTr("Apps"); color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
                font.pixelSize: page.largeText ? 44 : 40
                width: parent.width; wrapMode: Text.Wrap; bottomPadding: 12
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"; font.family: Fonts.family
                font.pixelSize: page.largeText ? 30 : 25
                visible: !page.backend || page.backend.busy || page.backend.error.length || !page.backend.applications.length
                text: !page.backend ? qsTr("Application service unavailable") : page.backend.busy ? qsTr("Loading apps…")
                    : page.backend.error || qsTr("No apps installed")
            }
            Repeater {
                model: page.backend ? page.backend.applications : []
                SettingsRow {
                    required property var modelData
                    objectName: "settingsApp_" + modelData.appId
                    heading: modelData.name
                    detail: modelData.trusted ? qsTr("Trusted system app") : modelData.appId
                    largeText: page.largeText
                    leftPadding: 98
                    onClicked: page.openApplication(modelData)
                    Rectangle {
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 72; height: 72; radius: 14; color: "#26ffffff"
                        visible: icon.status !== Image.Ready
                        Text { anchors.centerIn: parent; text: modelData.name.slice(0, 1); color: "white"; font.pixelSize: 36 }
                    }
                    Image {
                        id: icon
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 72; height: 72; source: modelData.icon || ""
                        fillMode: Image.PreserveAspectFit; asynchronous: true; visible: status === Image.Ready
                    }
                }
            }
        }
    }
    Flickable {
        objectName: "settingsAppDetails"
        x: page.depth === 1 ? 0 : page.depth > 1 ? -page.width : page.width
        width: page.width; height: page.height
        enabled: page.depth === 1
        contentHeight: details.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
        Column {
            id: details
            width: parent.width
            SettingsRow {
                objectName: "settingsAppDetailsBack"
                heading: "‹ " + qsTr("Apps")
                onClicked: page.back()
            }
            SettingsRow {
                heading: page.selectedApplication.name || ""
                detail: page.selectedApplication.trusted ? qsTr("Trusted system app") : page.applicationId
                enabled: false; largeText: page.largeText
            }
            Image {
                width: 80; height: 80; source: page.selectedApplication.icon || ""
                fillMode: Image.PreserveAspectFit
            }
            SettingsRow {
                heading: qsTr("Version and developer")
                detail: (page.selectedApplication.version || qsTr("Unknown version")) + " · " + (page.selectedApplication.developer || qsTr("Developer not declared"))
                enabled: false; largeText: page.largeText
            }
            SettingsRow {
                heading: qsTr("Package and source")
                detail: page.applicationId + "\n" + (page.selectedApplication.installationSource || qsTr("Android / Waydroid"))
                enabled: false; largeText: page.largeText
            }
            SettingsRow {
                heading: qsTr("Storage usage")
                detail: page.backend ? page.backend.storageUsage : qsTr("Unavailable")
                enabled: false; largeText: page.largeText
            }
            SettingsRow {
                heading: qsTr("Supported file types")
                detail: (page.selectedApplication.opens || []).concat(page.selectedApplication.mimeTypes || []).join(", ") || qsTr("None declared")
                enabled: false; largeText: page.largeText
            }
            SettingsRow {
                heading: qsTr("Clear defaults")
                detail: qsTr("Ask again before opening files with this app")
                largeText: page.largeText
                onClicked: if (page.backend) page.backend.clearDefaults(page.applicationId)
            }
            SettingsRow {
                objectName: "settingsAppPermissions"
                heading: qsTr("Permissions")
                detail: page.permissions && page.permissions.busy ? qsTr("Loading permissions…")
                    : page.permissions && page.permissions.error ? page.permissions.error
                    : page.requestedPermissions.length ? qsTr("Declared or requested permissions: %1").arg(page.requestedPermissions.length)
                    : qsTr("No permissions requested")
                largeText: page.largeText
                onClicked: { page.depth = 2; if (page.permissions) page.permissions.refreshApplications() }
            }
            Text {
                objectName: "settingsAppSettingsHeading"
                text: qsTr("App settings"); color: "#d4dadd"; font.family: Fonts.family
                font.pixelSize: page.largeText ? 30 : 25
                width: parent.width; topPadding: 24; bottomPadding: 8; leftPadding: 10
            }
            SettingsRow {
                objectName: "settingsAppDefaults"
                heading: qsTr("Open by default")
                detail: qsTr("Choose which app opens each type of file")
                largeText: page.largeText
                onClicked: { page.depth = 3; if (page.backend) page.backend.refreshFileTypes() }
            }
            SettingsRow {
                objectName: "settingsAppUninstall"
                visible: !!page.selectedApplication.canUninstall
                enabled: !!page.backend && !page.backend.uninstallBusy
                heading: page.backend && page.backend.uninstallBusy ? qsTr("Uninstalling…") : qsTr("Uninstall")
                detail: qsTr("Remove this app and its saved data")
                largeText: page.largeText
                onClicked: page.backend.requestUninstall(page.applicationId)
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; color: "#ffd1d1"; font.family: Fonts.family
                font.pixelSize: page.largeText ? 30 : 25
                visible: !!page.backend && page.backend.error.length > 0
                text: page.backend ? page.backend.error : ""
                textFormat: Text.PlainText
            }
        }
    }
    AppDefaultsPage {
        objectName: "settingsAppDefaultsPage"
        x: page.depth === 3 ? 0 : page.width
        width: page.width; height: page.height
        visible: page.visible && (page.depth === 3 || x < page.width)
        enabled: page.depth === 3
        backend: page.backend; prompts: page.prompts
        applicationId: page.applicationId === "com.openwave.files" ? "" : page.applicationId
        applicationName: page.selectedApplication.name || ""
        largeText: page.largeText; reducedMotion: page.reducedMotion
        onBackRequested: page.back()
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    }
    PermissionsPage {
        objectName: "settingsAppPermissionsPage"
        x: page.depth === 2 ? 0 : page.width
        width: page.width; height: page.height
        visible: page.visible && (page.depth === 2 || x < page.width)
        enabled: page.depth === 2
        applicationId: page.applicationId
        applicationName: page.selectedApplication.name || ""
        trustedApplication: !!page.selectedApplication.trusted
        requestedOnly: true
        client: page.permissions; prompts: page.prompts
        largeText: page.largeText; reducedMotion: page.reducedMotion
        onBackRequested: page.back()
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    }
}
