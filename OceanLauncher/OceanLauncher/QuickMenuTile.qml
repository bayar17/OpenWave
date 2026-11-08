import QtQuick

Item {
    id: tile
    property string title: ""
    property string iconSource: ""
    property color circleColor: "#777777"
    property bool available: true
    property bool active: true
    property bool interactive: true
    property bool reduceMotion: false
    property real renderScale: 1
    property string labelFont: ""
    property real topInset: 0
    property string stateDescription: ""
    signal clicked()

    width: 170
    height: 210
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: stateDescription !== "" ? stateDescription
        : !available ? qsTr("Unavailable") : active ? qsTr("On") : qsTr("Off")
    activeFocusOnTab: available && interactive
    Keys.onSpacePressed: if (available && interactive) clicked()
    Keys.onReturnPressed: if (available && interactive) clicked()

    Rectangle {
        id: circle
        width: 106
        height: 106
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: tile.topInset
        color: tile.active ? tile.circleColor : "#858585"
        opacity: tile.available ? 1 : 0.85
        scale: pressHandler.pressed ? 0.95 : 1
        Behavior on color {
            enabled: !tile.reduceMotion
            ColorAnimation { duration: 180 }
        }
        Behavior on scale {
            enabled: !tile.reduceMotion
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }

        IconImage {
            anchors.centerIn: parent
            width: 75
            height: 75
            renderScale: tile.renderScale
            source: tile.iconSource
        }
    }

    Text {
        anchors.top: circle.bottom
        anchors.topMargin: 15
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        text: tile.title
        color: "white"
        font.family: tile.labelFont
        font.pixelSize: 35
        font.weight: Font.Light
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        lineHeight: 1.2
    }

    TapHandler {
        id: pressHandler
        enabled: tile.available && tile.interactive
        onTapped: tile.clicked()
    }
}
