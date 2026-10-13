import QtQuick

Item {
    id: tile
    property string title: ""
    property string iconSource: ""
    property color circleColor: "#777777"
    property bool available: true
    property bool active: true
    property real renderScale: 1
    property string labelFont: ""
    signal clicked()

    width: 170
    height: 210

    Rectangle {
        id: circle
        width: 106
        height: 106
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        color: tile.active ? tile.circleColor : "#858585"
        opacity: tile.available ? 1 : 0.85
        Behavior on color { ColorAnimation { duration: 180 } }

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
        enabled: tile.available
        onTapped: tile.clicked()
    }
}
