import QtQuick
import QtQuick.Controls

Item {
    id: page
    property var backend: null
    property var timeBackend: null
    property bool largeText: false
    property bool reducedMotion: false
    property string category: ""
    readonly property var categories: [
        {key: "about", title: qsTr("About")},
        {key: "storage", title: qsTr("Storage")},
        {key: "locale", title: qsTr("Locale & Languages")},
        {key: "power", title: qsTr("Power options")}
    ]
    readonly property string categoryTitle: (categories.find(entry => entry.key === category) || {}).title || ""
    readonly property var systemInfo: backend ? backend.systemInfo : ({})
    signal editDeviceNameRequested()
    signal editTimezoneRequested()
    signal permissionsRequested()
    signal powerRequested(string action)

    clip: true
    onVisibleChanged: if (!visible) category = ""
    onCategoryChanged: details.contentY = 0
    function openCategory(key) {
        if (categories.some(entry => entry.key === key)) category = key
    }
    function back() {
        if (!category.length) return false
        category = ""
        return true
    }

    Flickable {
        id: overview
        objectName: "settingsGeneralCategories"
        x: page.category.length ? -page.width : 0
        width: page.width; height: page.height
        contentHeight: categoryRows.y + categoryRows.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        enabled: !page.category.length
        clip: true
        Behavior on x { NumberAnimation { duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: categoryRows
            x: 6; y: 28; width: parent.width - 12; spacing: 32
            Repeater {
                model: page.categories
                AbstractButton {
                    id: categoryButton
                    required property var modelData
                    objectName: "settingsGeneral_" + modelData.key
                    width: categoryRows.width
                    height: Math.max(84, categoryLabel.implicitHeight + 24)
                    text: modelData.title
                    Accessible.name: text
                    onClicked: page.openCategory(modelData.key)
                    background: Rectangle {
                        color: categoryButton.down ? "#18ffffff" : "transparent"
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#80ffffff" }
                        border.color: categoryButton.visualFocus ? "#00b7f0" : "transparent"
                    }
                    contentItem: Item {
                        Text {
                            id: categoryLabel
                            x: 14; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 88
                            text: categoryButton.text; textFormat: Text.PlainText
                            color: "white"; font.family: "Inter"; font.weight: Font.ExtraLight
                            font.pixelSize: page.largeText ? 50 : 44
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
        objectName: "settingsGeneralDetails"
        x: page.category.length ? 0 : page.width
        width: page.width; height: page.height
        enabled: page.category.length > 0 && !detailSlide.running
        contentHeight: detailRows.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Behavior on x {
            NumberAnimation { id: detailSlide; duration: page.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic }
        }
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: detailRows
            width: parent.width
            SettingsRow {
                objectName: "settingsGeneralBack"
                heading: "‹ " + page.categoryTitle
                largeText: page.largeText
                onClicked: page.back()
            }
            Column {
                width: parent.width; visible: page.category === "about"
                SettingsRow {
                    objectName: "settingsDeviceName"
                    heading: qsTr("Device name")
                    detail: page.backend ? page.backend.deviceName : "KB Phone"
                    largeText: page.largeText
                    enabled: page.backend !== null
                    onClicked: page.editDeviceNameRequested()
                }
                Repeater {
                    model: [
                        {key: "os", title: qsTr("Operating system"), value: page.systemInfo.os || "OpenWave"},
                        {key: "architecture", title: qsTr("Architecture"), value: page.systemInfo.architecture || qsTr("Unavailable")},
                        {key: "kernel", title: qsTr("Kernel"), value: page.systemInfo.kernel || qsTr("Unavailable")},
                        {key: "processors", title: qsTr("Processors"), value: page.systemInfo.processors > 0 ? String(page.systemInfo.processors) : qsTr("Unavailable")}
                    ]
                    SettingsRow {
                        required property var modelData
                        objectName: "settingsSystem_" + modelData.key
                        heading: modelData.title; detail: modelData.value
                        largeText: page.largeText; enabled: false
                    }
                }
            }
            Column {
                width: parent.width; visible: page.category === "storage"
                SettingsRow {
                    objectName: "settingsStorageInfo"
                    heading: qsTr("Device storage")
                    detail: page.systemInfo.storageTotal > 0
                        ? qsTr("%1 GB available of %2 GB").arg((page.systemInfo.storageAvailable / 1073741824).toFixed(1))
                            .arg((page.systemInfo.storageTotal / 1073741824).toFixed(1)) : qsTr("Unavailable")
                    largeText: page.largeText; enabled: false
                }
                SettingsRow {
                    objectName: "settingsPermissions"
                    heading: qsTr("Permissions")
                    detail: qsTr("Application access to files and folders")
                    largeText: page.largeText
                    onClicked: page.permissionsRequested()
                }
            }
            Column {
                width: parent.width; visible: page.category === "locale"
                SettingsRow {
                    objectName: "settingsTimezone"
                    heading: qsTr("Time zone")
                    detail: page.timeBackend && page.timeBackend.serviceAvailable
                        ? page.timeBackend.timezone : qsTr("Time service unavailable")
                    largeText: page.largeText
                    enabled: page.timeBackend && page.timeBackend.serviceAvailable
                    onClicked: page.editTimezoneRequested()
                }
            }
            Column {
                width: parent.width; visible: page.category === "power"
                SettingsRow {
                    objectName: "settingsRestart"
                    heading: qsTr("Restart"); largeText: page.largeText
                    enabled: page.backend !== null && page.backend.powerActionsAvailable
                    onClicked: page.powerRequested("restart")
                }
                SettingsRow {
                    objectName: "settingsShutdown"
                    heading: qsTr("Shut down"); largeText: page.largeText
                    enabled: page.backend !== null && page.backend.powerActionsAvailable
                    onClicked: page.powerRequested("shutdown")
                }
                Text {
                    width: parent.width - 20; x: 10; topPadding: 20
                    visible: !page.backend || !page.backend.powerActionsAvailable
                    text: qsTr("Power controls are unavailable in this session.")
                    font.family: "Inter"; font.pixelSize: page.largeText ? 30 : 25
                    color: "#d4dadd"; wrapMode: Text.Wrap
                }
            }
        }
    }
}
