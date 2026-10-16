import QtQuick
import QtQuick.Controls

Flickable {
    id: page
    property var client: null
    property var prompts: null
    property bool largeText: false
    property bool reducedMotion: false
    property string applicationId: ""
    property string applicationName: ""
    property bool requestedOnly: false
    property bool trustedApplication: false
    readonly property var entries: client ? (client.records || []).filter(row =>
        (!applicationId.length || row.application === applicationId) && (!requestedOnly || row.requestedAt > 0 || row.declared === true)) : []
    property string pendingApplication: ""
    property string pendingPermission: ""
    property string pendingState: ""
    property string action: ""
    signal backRequested()
    clip: true
    contentHeight: body.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    onVisibleChanged: {
        if (visible && client) client.refreshApplications()
        if (!visible && action.length && prompts && prompts.busy) prompts.cancel()
    }
    function edit(application, permission, heading, currentState) {
        if (!client || !prompts || prompts.busy) return
        pendingApplication = application
        pendingPermission = permission
        action = "choose"
        prompts.selection(heading, qsTr("Choose how this application may use this capability."), {
            choices: {"granted": qsTr("Allow"), "denied": qsTr("Deny"),
                      "not-determined": qsTr("Ask next time"), "restricted": qsTr("Restricted")},
            initialText: currentState, acceptLabel: qsTr("Apply"), cancelLabel: qsTr("Cancel")
        })
    }
    Connections {
        target: page.prompts
        function onFinished(requestId, result, text) {
            if (!page.action.length) return
            const action = page.action
            page.action = ""
            if (!page.visible || result !== "accepted" || !page.client) return
            if (action === "choose" && ["granted", "denied", "not-determined", "restricted"].includes(text)) {
                page.pendingState = text
                if (text === "granted") page.client.setPermission(page.pendingApplication, page.pendingPermission, text)
                else {
                    page.action = "confirm"
                    page.prompts.confirmation(qsTr("Change permission?"),
                        qsTr("Active access will be closed. The application may need to be reopened."),
                        {acceptLabel: qsTr("Change"), cancelLabel: qsTr("Cancel")})
                }
            } else if (action === "confirm") {
                page.client.setPermission(page.pendingApplication, page.pendingPermission, page.pendingState)
            }
        }
    }
    Column {
        id: body
        width: page.width
        spacing: 16
        AbstractButton {
            width: parent.width; height: 86
            Accessible.name: page.applicationId.length ? qsTr("Back to app details") : qsTr("Back to General settings")
            onClicked: page.backRequested()
            background: Rectangle { color: parent.down ? "#18ffffff" : "transparent" }
            contentItem: Text {
                text: qsTr("‹  Permissions"); color: "white"; font.family: "Inter"
                font.pixelSize: page.largeText ? 38 : 34; verticalAlignment: Text.AlignVCenter
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"
            font.family: "Inter"; font.pixelSize: page.largeText ? 30 : 25
            text: page.applicationId.length ? page.applicationName
                : qsTr("Review application permissions and control access to protected capabilities.")
            textFormat: Text.PlainText
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"
            font.family: "Inter"; font.pixelSize: page.largeText ? 30 : 25
            visible: page.trustedApplication
            text: qsTr("This app is a trusted system component. Its system access cannot be changed here.")
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"
            font.family: "Inter"; font.pixelSize: page.largeText ? 30 : 25
            visible: !page.client || page.client.busy || page.client.error.length
            text: page.client ? page.client.busy ? qsTr("Loading permissions…") : page.client.error : qsTr("Permission service unavailable")
        }
        Text {
            objectName: "permissionsEmptyState"
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"
            font.family: "Inter"; font.pixelSize: page.largeText ? 30 : 25
            visible: page.client && !page.client.busy && !page.client.error.length && !page.entries.length
            text: page.requestedOnly ? qsTr("This app has not requested any permissions.") : qsTr("No permissions available")
        }
        Repeater {
            objectName: "permissionRows"
            model: page.entries
            delegate: SettingsRow {
                required property var modelData
                heading: (page.applicationId.length ? "" : modelData.name + " · ") + modelData.label
                detail: modelData.trusted ? qsTr("Trusted system component")
                    : modelData.supported === false ? qsTr("Restricted · Not supported by this system")
                    : modelData.state === "granted" ? qsTr("Allowed") + (modelData.duration === "session" ? qsTr(" · This session")
                        : modelData.duration === "application" ? qsTr(" · While using the application") : "")
                    : modelData.state === "denied" ? qsTr("Denied")
                    : modelData.state === "restricted" ? qsTr("Restricted") : qsTr("Ask next time")
                largeText: page.largeText
                enabled: !modelData.trusted && modelData.supported !== false && !(page.prompts && page.prompts.busy)
                onClicked: page.edit(modelData.application, modelData.permission, heading, modelData.state)
            }
        }
    }
}
