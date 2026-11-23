import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0
import OpenWave.QuickMenu as Applets

Item {
    id: card
    property var preset: ({})
    property bool selected: false
    property bool largeText: false
    property bool reduceMotion: false
    property bool interactive: true
    readonly property bool favorite: !!preset && !!preset.favorite
    signal tapped()
    signal held()
    signal menuRequested()

    height: 170
    scale: tapArea.pressed ? 0.98 : 1
    Behavior on scale {
        enabled: !card.reduceMotion
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }
    Accessible.role: Accessible.RadioButton
    Accessible.name: preset ? preset.name : ""
    Accessible.checkable: true
    Accessible.checked: selected
    Accessible.onPressAction: card.tapped()

    Applets.AppearancePreview {
        anchors.fill: parent
        preset: card.preset
        compact: true
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.45
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: "#99000000" }
        }
    }

    Text {
        objectName: "settingsAppearanceName"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: parent.width - 220
        text: card.preset ? card.preset.name : ""
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: "white"
        font.family: Fonts.family
        font.weight: Font.ExtraLight
        font.pixelSize: card.largeText ? 50 : 44
    }

    SettingsSymbol {
        objectName: "settingsAppearanceFavoriteMark"
        visible: card.favorite
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        width: 34
        height: 34
        kind: "star"
        color: "white"
    }

    Rectangle {
        objectName: "settingsAppearanceSelection"
        anchors.fill: parent
        color: "transparent"
        border.color: "white"
        border.width: 4
        opacity: card.selected ? 1 : 0
        Behavior on opacity {
            enabled: !card.reduceMotion
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
    }

    Item {
        id: tapArea
        anchors.fill: parent
        readonly property bool pressed: tap.pressed
        TapHandler {
            id: tap
            enabled: card.interactive
            onTapped: card.tapped()
            onLongPressed: card.held()
        }
    }

    AbstractButton {
        id: menuButton
        objectName: "settingsAppearanceMenuButton"
        visible: card.interactive
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 96
        height: 96
        Accessible.name: qsTr("Options for %1").arg(card.preset ? card.preset.name : "")
        onClicked: card.menuRequested()
        background: Item {}
        contentItem: Item {
            SettingsSymbol {
                x: 20
                y: parent.height - height - 26
                width: 40
                height: 40
                kind: "menu"
                color: "white"
            }
        }
    }
}
