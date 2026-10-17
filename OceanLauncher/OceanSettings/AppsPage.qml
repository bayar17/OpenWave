import QtQuick
import QtQuick.Controls

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
    function openApplication(application) {
        selectedApplication = application
        depth = 1
        if (permissions) permissions.refreshApplications()
    }
    function back() {
        if (!depth) return false
        if (prompts && prompts.busy) { prompts.cancel(); return true }
        if (backend) backend.cancelUninstallPrompt()
        depth--
        return true
    }
    onVisibleChanged: {
        if (visible && backend) backend.refresh()
        if (visible && permissions) permissions.refreshApplications()
        if (!visible) { if (backend) backend.cancelUninstallPrompt(); if (prompts && prompts.busy && depth === 2) prompts.cancel(); depth = 0; selectedApplication = ({}) }
    }
    Connections {
        target: page.backend
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
                text: qsTr("Apps"); color: "white"; font.family: "Inter"; font.weight: Font.ExtraLight
                font.pixelSize: page.largeText ? 44 : 40
                width: parent.width; wrapMode: Text.Wrap; bottomPadding: 12
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"; font.family: "Inter"
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
            SettingsRow {
                objectName: "settingsAppUninstall"
                visible: !!page.selectedApplication.canUninstall
                enabled: !!page.backend && !page.backend.uninstallBusy
                heading: page.backend && page.backend.uninstallBusy ? qsTr("Uninstalling…") : qsTr("Uninstall")
                detail: qsTr("Remove this web app and its saved website data")
                largeText: page.largeText
                onClicked: page.backend.requestUninstall(page.applicationId)
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; color: "#ffd1d1"; font.family: "Inter"
                font.pixelSize: page.largeText ? 30 : 25
                visible: !!page.backend && page.backend.error.length > 0
                text: page.backend ? page.backend.error : ""
                textFormat: Text.PlainText
            }
        }
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
