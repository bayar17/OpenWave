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
        depth = depth === 3 ? 1 : depth === 4 ? 0 : depth - 1
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
            SettingsRow {
                objectName: "settingsAppsDefaultApps"
                heading: qsTr("Default apps")
                detail: qsTr("Browser, email, photos, music, videos, documents and file types")
                largeText: page.largeText
                onClicked: { page.depth = 4; if (page.backend) { page.backend.refreshCategories(); page.backend.refreshFileTypes() } }
            }
            Text {
                text: qsTr("Installed apps"); color: "#d4dadd"; font.family: Fonts.family
                font.pixelSize: page.largeText ? 30 : 25
                width: parent.width; topPadding: 12; bottomPadding: 4; leftPadding: 10
            }
            Repeater {
                model: page.backend ? page.backend.applications : []
                SettingsRow {
                    required property var modelData
                    objectName: "settingsApp_" + modelData.appId
                    heading: modelData.name
                    detail: modelData.trusted ? qsTr("Trusted system app") : modelData.kind === "linux" ? qsTr("Linux app (cannot be launched)") : modelData.appId
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
                heading: "‹ " + (page.selectedApplication.name || qsTr("Apps"))
                onClicked: page.back()
            }
            Item {
                objectName: "settingsAppHeader"
                width: parent.width
                height: 50 + 205 + 25
                readonly property real textScale: page.largeText ? 1.12 : 1
                Image {
                    objectName: "settingsAppHeaderIcon"
                    x: 0; y: 50; width: 205; height: 205
                    source: page.selectedApplication.icon || ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 410; sourceSize.height: 410
                }
                Column {
                    x: 237; y: 50
                    width: parent.width - 237 - 10
                    spacing: 6
                    Text {
                        objectName: "settingsAppHeaderName"
                        width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                        text: page.selectedApplication.name || ""
                        color: "white"; font.family: Fonts.family; font.weight: Font.Normal
                        font.pixelSize: 42 * parent.parent.textScale
                    }
                    Text {
                        objectName: "settingsAppHeaderId"
                        width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
                        text: page.selectedApplication.androidPackage || page.applicationId
                        color: "#d4dadd"; font.family: Fonts.family; font.weight: Font.ExtraLight
                        font.pixelSize: 36 * parent.parent.textScale
                    }
                    Text {
                        objectName: "settingsAppHeaderVersion"
                        width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                        visible: text.length > 0
                        text: page.selectedApplication.version ? qsTr("Version %1").arg(page.selectedApplication.version)
                            : page.selectedApplication.kind === "android" ? qsTr("Android app")
                            : page.selectedApplication.kind === "linux" ? qsTr("Linux app") : ""
                        color: "#d4dadd"; font.family: Fonts.family; font.weight: Font.ExtraLight
                        font.pixelSize: 36 * parent.parent.textScale
                    }
                    Text {
                        objectName: "settingsAppHeaderStorage"
                        width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                        readonly property string usage: page.backend ? page.backend.storageUsage : ""
                        visible: usage.length > 0 && usage.indexOf(qsTr("Unavailable")) !== 0
                        text: usage
                        color: "#d4dadd"; font.family: Fonts.family; font.weight: Font.ExtraLight
                        font.pixelSize: 36 * parent.parent.textScale
                    }
                }
            }
            Text {
                objectName: "settingsAppSettingsHeading"
                text: qsTr("App settings"); color: "#e6eaec"; font.family: Fonts.family; font.weight: Font.ExtraLight
                font.pixelSize: page.largeText ? 40 : 36
                width: parent.width; topPadding: 20; bottomPadding: 16; leftPadding: 16
                Rectangle {
                    anchors.bottom: parent.bottom
                    x: -20; width: parent.width + 40; height: 1; color: "#80ffffff"
                }
            }
            SettingsRow {
                objectName: "settingsAppDefaults"
                heading: qsTr("Open by default")
                detail: qsTr("Choose which app opens each type of file")
                largeText: page.largeText
                onClicked: { page.depth = 3; if (page.backend) page.backend.refreshFileTypes() }
            }
            SettingsRow {
                objectName: "settingsAppPermissions"
                heading: qsTr("Permissions")
                detail: page.permissions && page.permissions.error ? page.permissions.error : qsTr("Change declared or requested permissions")
                largeText: page.largeText
                onClicked: { page.depth = 2; if (page.permissions) page.permissions.refreshApplications() }
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
                objectName: "settingsAppLaunchNote"
                width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"; font.family: Fonts.family
                font.pixelSize: page.largeText ? 30 : 25; topPadding: 16; leftPadding: 10
                visible: !!page.selectedApplication.launchUnavailable
                text: page.selectedApplication.launchUnavailable || ""
                textFormat: Text.PlainText
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
    DefaultAppsPage {
        objectName: "settingsDefaultAppsPage"
        x: page.depth === 4 ? 0 : page.depth > 4 ? -page.width : page.width
        width: page.width; height: page.height
        visible: page.visible && (page.depth === 4 || (x > -page.width && x < page.width))
        enabled: page.depth === 4
        backend: page.backend; prompts: page.prompts
        largeText: page.largeText; reducedMotion: page.reducedMotion
        onBackRequested: page.back()
        onFileTypesRequested: { page.depth = 5; if (page.backend) page.backend.refreshFileTypes() }
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    }
    AppDefaultsPage {
        objectName: "settingsAllFileTypesPage"
        x: page.depth === 5 ? 0 : page.width
        width: page.width; height: page.height
        visible: page.visible && (page.depth === 5 || x < page.width)
        enabled: page.depth === 5
        backend: page.backend; prompts: page.prompts
        applicationId: ""
        applicationName: qsTr("Default apps")
        largeText: page.largeText; reducedMotion: page.reducedMotion
        onBackRequested: page.back()
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
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
        application: page.selectedApplication
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
