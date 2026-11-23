import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0

Item {
    id: root
    property bool largeText: false
    property bool reducedMotion: false
    property alias paletteExpanded: paletteCard.paletteExpanded
    width: parent ? parent.width : 400
    height: paletteCard.height

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
                duration: root.reducedMotion ? 0 : 160
                easing.type: Easing.OutCubic
            }
        }

        Text {
            x: 16; y: 16; width: parent.width - 32
            text: qsTr("Accent Color")
            color: "white"; font.family: Fonts.family; font.weight: Font.Light
            font.pixelSize: root.largeText ? 42 : 36
        }
        Text {
            id: paletteDescription
            objectName: "settingsAccentPaletteDescription"
            x: 16; y: 64; width: parent.width - 32
            text: qsTr("Choose the color used for selections and active controls")
            color: "#e0e3e4"; font.family: Fonts.family; font.weight: Font.Light
            font.pixelSize: root.largeText ? 30 : 25
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
                NumberAnimation { duration: root.reducedMotion ? 0 : 190; easing.type: Easing.OutCubic }
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
                    font.pixelSize: root.largeText ? 34 : 30
                }
                SettingsSymbol {
                    x: parent.width - 60; y: 7; width: 48; height: 48
                    kind: "chevron"; rotation: paletteCard.paletteExpanded ? -90 : 90
                    color: "white"
                    Behavior on rotation { NumberAnimation { duration: root.reducedMotion ? 0 : 160 } }
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
                        font.pixelSize: root.largeText ? 34 : 30
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { color: parent.down ? "#20ffffff" : "transparent"; radius: 8 }
                }
            }
        }
    }
}
