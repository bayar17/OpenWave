import QtQuick
import QtQuick.Controls
import QtQuick.Effects

AbstractButton {
    id: tile
    required property url photoSource
    property string label: ""
    property bool selecting: false
    property bool selected: false
    property real renderScale: 1
    Accessible.name: label
    Accessible.role: Accessible.Button
    background: Rectangle { color: "#24454e53"; radius: 10 }
    contentItem: Item {
        clip: true
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect { maskEnabled: true; maskSource: roundedMask }
        Image {
            id: image
            anchors.fill: parent
            source: tile.photoSource
            sourceSize: Qt.size(Math.ceil(tile.width * tile.renderScale * 2), Math.ceil(tile.height * tile.renderScale * 2))
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; smooth: true; autoTransform: true
            opacity: tile.down ? 0.75 : 1
            Behavior on opacity { NumberAnimation { duration: 100 } }
        }
        Rectangle {
            anchors.fill: parent; color: "transparent"; radius: 10
            border.width: tile.selected ? 3 : 0; border.color: "#00b7f0"
        }
        Text {
            anchors.centerIn: parent; width: parent.width - 20
            visible: image.status === Image.Error
            text: qsTr("Image unavailable"); color: "white"; wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter; font.pixelSize: 22
        }
        Rectangle {
            x: 12; y: 12; width: 32; height: 32; radius: 16
            visible: tile.selecting; color: tile.selected ? "#00b7f0" : "#50454e53"
            border.color: "white"; border.width: 1.5
            Text { anchors.centerIn: parent; text: tile.selected ? "✓" : ""; color: "white"; font.pixelSize: 24 }
        }
    }
    Rectangle {
        id: roundedMask
        width: tile.width; height: tile.height; radius: 10
        color: "white"; visible: false; layer.enabled: true
    }
}
