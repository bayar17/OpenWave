import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0
import OceanSettings 1.0

Item {
    id: root
    objectName: "generalModule"
    readonly property var service: typeof General !== "undefined" ? General : null
    readonly property var prompts: typeof Prompts !== "undefined" ? Prompts : null
    readonly property var environment: typeof SettingsEnvironment !== "undefined" ? SettingsEnvironment : null
    readonly property bool largeText: !!environment && environment.largeText
    readonly property bool reducedMotion: !!environment && environment.reduceMotion
    readonly property var categories: [
        {key: "about", title: qsTr("About")},
        {key: "storage", title: qsTr("Storage")},
        {key: "locale", title: qsTr("Locale & Languages")},
        {key: "power", title: qsTr("Power options")}
    ]
    readonly property string categoryTitle: (categories.find(entry => entry.key === category) || {}).title || ""
    readonly property var systemInfo: service ? service.systemInfo : ({})
    property string category: ""
    property string pendingAction: ""
    property string errorMessage: ""

    function showError(message) {
        errorMessage = message
        errorTimer.restart()
    }

    function cancelPrompt() {
        const waiting = pendingAction.length > 0
        pendingAction = ""
        if (waiting && prompts && prompts.busy)
            prompts.cancel()
    }

    function openCategory(key) {
        if (categories.some(entry => entry.key === key))
            category = key
    }

    function openEntry(key) {
        if (key === "name") {
            openCategory("about")
            editName()
        } else if (key === "timezone") {
            openCategory("locale")
            editTimezone()
        } else {
            openCategory(key)
        }
    }

    function back() {
        if (pendingAction.length > 0 && prompts && prompts.busy) {
            cancelPrompt()
            return true
        }
        if (category.length === 0)
            return false
        category = ""
        return true
    }

    function editName() {
        if (!service || pendingAction.length > 0 || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        pendingAction = "name"
        prompts.textInput(qsTr("Device name"), "", {
            initialText: service.deviceName, placeholder: qsTr("Type device name"),
            minLength: 1, maxLength: 64, validationPattern: "[\\s\\S]*\\S[\\s\\S]*",
            acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel")})
    }

    function editTimezone() {
        if (!service || !service.timeServiceAvailable || service.timeBusy
                || pendingAction.length > 0 || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        if (service.zones.length === 0) {
            service.refreshTimeZones()
            showError(qsTr("Time zones are still loading. Try again."))
            return
        }
        const choices = ({})
        for (const zone of service.zones)
            choices[zone] = zone.split("_").join(" ")
        pendingAction = "timezone"
        prompts.selection(qsTr("Time zone"), "", {
            choices: choices, initialText: choices[service.timezone] ? service.timezone : "",
            placeholder: qsTr("Search city or region"), acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel")})
    }

    function requestPower(action) {
        if (!["restart", "shutdown"].includes(action) || !service || !service.powerActionsAvailable
                || pendingAction.length > 0 || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        pendingAction = action
        prompts.confirmation(action === "restart" ? qsTr("Restart OpenWave?") : qsTr("Shut down OpenWave?"),
            qsTr("Open applications will close. Save your work before continuing."), {
                acceptLabel: action === "restart" ? qsTr("Restart") : qsTr("Shut down"), cancelLabel: qsTr("Cancel")
            })
    }

    onVisibleChanged: {
        if (!visible) {
            cancelPrompt()
            category = ""
        }
    }

    onCategoryChanged: details.contentY = 0

    Component.onDestruction: cancelPrompt()

    Timer {
        id: errorTimer
        interval: 3000
        onTriggered: root.errorMessage = ""
    }

    Connections {
        target: root.service
        ignoreUnknownSignals: true
        function onErrorOccurred(message) { root.showError(message) }
    }

    Connections {
        target: root.prompts
        ignoreUnknownSignals: true
        function onFinished(requestId, result, text) {
            const action = root.pendingAction
            root.pendingAction = ""
            if (result !== "accepted" || action.length === 0 || !root.visible || !root.service)
                return
            if (action === "name")
                root.service.saveDeviceName(text)
            else if (action === "timezone")
                root.service.setTimezone(text)
            else if (action === "restart" || action === "shutdown")
                root.service.requestPowerAction(action)
        }
    }

    Rectangle {
        visible: root.errorMessage.length > 0
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        width: Math.min(parent.width - 32, toastText.implicitWidth + 40)
        height: toastText.implicitHeight + 32
        color: "#ed25313a"
        radius: 10
        Text {
            id: toastText
            objectName: "generalToastText"
            width: parent.width - 40
            anchors.centerIn: parent
            text: root.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Fonts.family
            font.pixelSize: 26
            color: "white"
        }
    }

    clip: true

    Flickable {
        id: overview
        objectName: "generalCategories"
        x: root.category.length ? -root.width : 0
        width: root.width; height: root.height
        contentHeight: categoryRows.y + categoryRows.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        enabled: !root.category.length
        clip: true
        Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: categoryRows
            x: 6; y: 28; width: parent.width - 12; spacing: 32
            Repeater {
                model: root.categories
                AbstractButton {
                    id: categoryButton
                    required property var modelData
                    objectName: "generalCategory_" + modelData.key
                    width: categoryRows.width
                    height: Math.max(84, categoryLabel.implicitHeight + 24)
                    text: modelData.title
                    Accessible.name: text
                    onClicked: root.openCategory(modelData.key)
                    background: Rectangle {
                        color: categoryButton.down ? "#18ffffff" : "transparent"
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#80ffffff" }
                        border.color: categoryButton.visualFocus ? Theme.focusColor : "transparent"
                    }
                    contentItem: Item {
                        Text {
                            id: categoryLabel
                            x: 14; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 88
                            text: categoryButton.text; textFormat: Text.PlainText
                            color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
                            font.pixelSize: root.largeText ? 50 : 44
                            wrapMode: Text.Wrap
                        }
                        SettingsSymbol {
                            anchors.right: parent.right; anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            width: 44; height: 44; kind: "chevron"
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: details
        objectName: "generalDetails"
        x: root.category.length ? 0 : root.width
        width: root.width; height: root.height
        enabled: root.category.length > 0 && !detailSlide.running
        contentHeight: detailRows.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on x {
            NumberAnimation { id: detailSlide; duration: root.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic }
        }
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: detailRows
            width: parent.width
            SettingsRow {
                objectName: "generalBack"
                heading: "‹ " + root.categoryTitle
                largeText: root.largeText
                onClicked: root.back()
            }
            Column {
                width: parent.width; visible: root.category === "about"
                SettingsRow {
                    objectName: "generalDeviceName"
                    heading: qsTr("Device name")
                    detail: root.service ? root.service.deviceName : ""
                    largeText: root.largeText
                    enabled: root.service !== null
                    onClicked: root.editName()
                }
                Repeater {
                    model: [
                        {key: "os", title: qsTr("Operating system"), value: root.systemInfo.os || "OpenWave"},
                        {key: "architecture", title: qsTr("Architecture"), value: root.systemInfo.architecture || qsTr("Unavailable")},
                        {key: "kernel", title: qsTr("Kernel"), value: root.systemInfo.kernel || qsTr("Unavailable")},
                        {key: "processors", title: qsTr("Processors"), value: root.systemInfo.processors > 0 ? String(root.systemInfo.processors) : qsTr("Unavailable")}
                    ]
                    SettingsRow {
                        required property var modelData
                        objectName: "generalSystem_" + modelData.key
                        heading: modelData.title; detail: modelData.value
                        largeText: root.largeText; enabled: false
                    }
                }
            }
            Column {
                width: parent.width; visible: root.category === "storage"
                SettingsRow {
                    objectName: "generalStorageInfo"
                    heading: qsTr("Device storage")
                    detail: root.systemInfo.storageTotal > 0
                        ? qsTr("%1 GB available of %2 GB").arg((root.systemInfo.storageAvailable / 1073741824).toFixed(1))
                            .arg((root.systemInfo.storageTotal / 1073741824).toFixed(1)) : qsTr("Unavailable")
                    largeText: root.largeText; enabled: false
                }
            }
            Column {
                width: parent.width; visible: root.category === "locale"
                SettingsRow {
                    objectName: "generalTimezone"
                    heading: qsTr("Time zone")
                    detail: root.service && root.service.timeServiceAvailable
                        ? root.service.timezone : qsTr("Time service unavailable")
                    largeText: root.largeText
                    enabled: !!root.service && root.service.timeServiceAvailable
                    onClicked: root.editTimezone()
                }
            }
            Column {
                width: parent.width; visible: root.category === "power"
                SettingsRow {
                    objectName: "generalRestart"
                    heading: qsTr("Restart"); largeText: root.largeText
                    enabled: !!root.service && root.service.powerActionsAvailable
                    onClicked: root.requestPower("restart")
                }
                SettingsRow {
                    objectName: "generalShutdown"
                    heading: qsTr("Shut down"); largeText: root.largeText
                    enabled: !!root.service && root.service.powerActionsAvailable
                    onClicked: root.requestPower("shutdown")
                }
                Text {
                    objectName: "generalPowerNote"
                    width: parent.width - 20; x: 10; topPadding: 20
                    visible: !root.service || !root.service.powerActionsAvailable
                    text: root.service && root.service.powerBusy ? qsTr("Closing applications…")
                        : qsTr("Power controls are unavailable in this session.")
                    font.family: Fonts.family; font.pixelSize: root.largeText ? 30 : 25
                    color: "#d4dadd"; wrapMode: Text.Wrap
                }
            }
        }
    }
}
