import QtQuick

Item {
    id: control
    property real value: 50
    property string iconSource: ""
    property real renderScale: 1
    property bool warningRange: false
    signal edited(real value)

    width: 655
    height: 72

    IconImage {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: 65
        height: 65
        source: control.iconSource
        renderScale: control.renderScale
    }

    Item {
        id: track
        x: 87
        y: (parent.height - height) / 2
        width: parent.width - x
        height: 48

        Rectangle {
            x: 0
            y: 21
            width: parent.width
            height: 6
            radius: 3
            color: "#8f9293"
        }
        Rectangle {
            visible: control.warningRange
            x: parent.width * 0.68
            y: 21
            width: parent.width - x
            height: 6
            radius: 3
            color: "#bb4b56"
        }
        Rectangle {
            x: 0
            y: 21
            width: parent.width * Math.max(0, Math.min(100, control.value)) / 100
            height: 6
            radius: 3
            color: "white"
        }
        Rectangle {
            x: Math.max(0, Math.min(parent.width, parent.width * control.value / 100)) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            radius: 15
            color: "#e2e2e2"
        }

        MouseArea {
            anchors.fill: parent
            function updateValue(mouseX) {
                control.edited(Math.round(100 * Math.max(0, Math.min(1, mouseX / track.width))))
            }
            onPressed: mouse => updateValue(mouse.x)
            onPositionChanged: mouse => {
                if (pressed)
                    updateValue(mouse.x)
            }
        }
    }
}
