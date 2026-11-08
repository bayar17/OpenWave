import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0

Item {
    id: page
    property var backend: null
    property var timeBackend: null
    property bool largeText: false
    property bool reducedMotion: false
    property string category: ""
    readonly property var categories: [
        {key: "about", title: qsTr("About")},
        {key: "appearance", title: qsTr("Appearance")},
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
    onCategoryChanged: {
        details.contentY = 0
        if (category !== "appearance") paletteCard.paletteExpanded = false
    }
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
                        border.color: categoryButton.visualFocus ? Theme.focusColor : "transparent"
                    }
                    contentItem: Item {
                        Text {
                            id: categoryLabel
                            x: 14; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 88
                            text: categoryButton.text; textFormat: Text.PlainText
                            color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
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
                width: parent.width; visible: page.category === "appearance"
                Item {
                    id: paletteCard
                    objectName: "settingsAccentPalette"
                    width: parent.width
                    height: paletteSurface.y + 78
                    z: paletteExpanded ? 1 : 0
                    property bool paletteExpanded: false
                    property real choicesOpacity: paletteExpanded ? 1 : 0
                    readonly property var palettes: [
                        {key: "ocean", label: qsTr("Ocean")},
                        {key: "ocean-cyan", label: qsTr("Ocean Cyan")},
                        {key: "rouge", label: qsTr("Rouge")},
                        {key: "violet", label: qsTr("Violet")},
                        {key: "amber", label: qsTr("Amber")}
                    ]
                    readonly property var selectedPalette: palettes.find(item => item.key === Theme.palette) || palettes[0]

                    Behavior on choicesOpacity {
                        NumberAnimation {
                            duration: page.reducedMotion ? 0 : 160
                            easing.type: Easing.OutCubic
                        }
                    }

                    Text {
                        x: 16; y: 16; width: parent.width - 32
                        text: qsTr("Accent Color")
                        color: "white"; font.family: Fonts.family; font.weight: Font.Light
                        font.pixelSize: page.largeText ? 42 : 36
                    }
                    Text {
                        id: paletteDescription
                        objectName: "settingsAccentPaletteDescription"
                        x: 16; y: 64; width: parent.width - 32
                        text: qsTr("Choose the color used for selections and active controls")
                        color: "#e0e3e4"; font.family: Fonts.family; font.weight: Font.Light
                        font.pixelSize: page.largeText ? 30 : 25
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        id: paletteSurface
                        objectName: "settingsAccentPaletteSurface"
                        x: 8; y: paletteDescription.y + paletteDescription.implicitHeight + 16
                        width: parent.width - 28
                        height: paletteCard.paletteExpanded ? 287 : 62
                        radius: 20; color: "#808486"
                        Behavior on height {
                            NumberAnimation { duration: page.reducedMotion ? 0 : 190; easing.type: Easing.OutCubic }
                        }
                    }
                    AbstractButton {
                        id: paletteButton
                        objectName: "settingsAccentPaletteButton"
                        x: paletteSurface.x; y: paletteSurface.y
                        width: paletteSurface.width; height: 62
                        Accessible.name: qsTr("Accent color, %1").arg(paletteCard.selectedPalette.label)
                        Accessible.description: paletteCard.paletteExpanded ? qsTr("Collapse color choices") : qsTr("Show color choices")
                        onClicked: paletteCard.paletteExpanded = !paletteCard.paletteExpanded
                        contentItem: Item {
                            Text {
                                x: 24; anchors.verticalCenter: parent.verticalCenter
                                text: paletteCard.selectedPalette.label
                                color: "white"; font.family: Fonts.family; font.weight: Font.Light
                                font.pixelSize: page.largeText ? 34 : 30
                            }
                            SettingsSymbol {
                                x: parent.width - 60; y: 7; width: 48; height: 48
                                kind: "chevron"; rotation: paletteCard.paletteExpanded ? -90 : 90
                                color: "white"
                                Behavior on rotation { NumberAnimation { duration: page.reducedMotion ? 0 : 160 } }
                            }
                        }
                        background: Rectangle {
                            color: paletteButton.down ? "#18ffffff" : "transparent"
                            radius: 20
                            Rectangle { anchors.fill: parent; color: "transparent"; border.color: paletteButton.visualFocus ? Theme.focusColor : "transparent"; border.width: 2; radius: 20 }
                        }
                    }
                    Rectangle {
                        x: paletteSurface.x + 24; y: paletteSurface.y + 62
                        width: paletteSurface.width - 48; height: 1
                        visible: paletteCard.paletteExpanded || paletteCard.choicesOpacity > 0
                        opacity: paletteCard.choicesOpacity
                        color: "#bfffffff"
                    }
                    Column {
                        objectName: "settingsAccentPaletteChoices"
                        x: paletteSurface.x + 16; y: paletteSurface.y + 68
                        width: paletteSurface.width - 32
                        visible: paletteCard.paletteExpanded || paletteCard.choicesOpacity > 0
                        opacity: paletteCard.choicesOpacity
                        enabled: paletteCard.paletteExpanded
                        Repeater {
                            model: paletteCard.palettes.filter(item => item.key !== Theme.palette)
                            AbstractButton {
                                required property var modelData
                                objectName: "settingsAccentChoice_" + modelData.key
                                width: parent.width; height: 52
                                Accessible.name: modelData.label
                                onClicked: {
                                    paletteCard.paletteExpanded = false
                                    Theme.selectPalette(modelData.key)
                                }
                                contentItem: Text {
                                    x: 8; text: modelData.label
                                    color: "white"; font.family: Fonts.family; font.weight: Font.Light
                                    font.pixelSize: page.largeText ? 34 : 30
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle { color: parent.down ? "#20ffffff" : "transparent"; radius: 8 }
                            }
                        }
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
                    text: page.backend && page.backend.powerBusy ? qsTr("Closing applications…")
                        : qsTr("Power controls are unavailable in this session.")
                    font.family: Fonts.family; font.pixelSize: page.largeText ? 30 : 25
                    color: "#d4dadd"; wrapMode: Text.Wrap
                }
            }
        }
    }

}
