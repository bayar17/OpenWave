import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string heading
    property string detail
    property bool largeText: false
    property bool toggle: false
    property bool longPressEnabled: false
    property bool longPressTriggered: false
    signal longPressed()
    width: parent ? parent.width : 668
    implicitHeight: Math.max(108, labels.implicitHeight + 34)
    checkable: toggle
    Accessible.name: heading + (detail ? ", " + detail : "")
    Accessible.role: toggle ? Accessible.CheckBox : Accessible.Button
    Accessible.checked: checked
    onPressedChanged: {
        if (pressed && longPressEnabled) {
            longPressTriggered = false
            holdTimer.restart()
        } else {
            holdTimer.stop()
        }
    }
    Timer {
        id: holdTimer
        interval: 700
        onTriggered: {
            if (root.pressed && root.longPressEnabled) {
                root.longPressTriggered = true
                root.longPressed()
            }
        }
    }
    background: Rectangle {
        color: root.down ? "#18ffffff" : "transparent"
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#80ffffff"
        }
    }
    contentItem: Item {
        Column {
            id: labels
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (root.toggle ? 110 : 20)
            spacing: 7
            Text {
                text: root.heading
                textFormat: Text.PlainText
                width: parent.width
                wrapMode: Text.Wrap
                color: "white"
                font.family: "Inter"
                font.pixelSize: root.largeText ? 38 : 32
            }
            Text {
                text: root.detail
                textFormat: Text.PlainText
                visible: text.length > 0
                width: parent.width
                wrapMode: Text.Wrap
                color: "#d4dadd"
                font.family: "Inter"
                font.pixelSize: root.largeText ? 30 : 25
            }
        }
        Rectangle {
            visible: root.toggle
            width: 72
            height: 38
            radius: 19
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? "#00b7f0" : "#647078"
            Rectangle {
                x: root.checked ? 38 : 4
                y: 4
                width: 30
                height: 30
                radius: 15
                color: "white"
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: root.visualFocus
            color: "transparent"
            border.color: "#00b7f0"
            border.width: 2
        }
    }
}
