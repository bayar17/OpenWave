import QtQuick

Item {
    id: control
    property real value: 50
    property string iconSource: ""
    property real renderScale: 1
    property bool warningRange: false
    property bool interactive: true
    property bool reduceMotion: false
    property string accessibleName: ""
    property real displayedValue: value
    property bool dragging: false
    signal edited(real value)

    Accessible.role: Accessible.Slider
    Accessible.name: accessibleName
    Accessible.description: qsTr("%1 percent").arg(Math.round(displayedValue))
    activeFocusOnTab: interactive
    Keys.onLeftPressed: if (interactive) edited(Math.max(0, Math.round(value) - 1))
    Keys.onRightPressed: if (interactive) edited(Math.min(100, Math.round(value) + 1))
    onValueChanged: if (!dragging) displayedValue = value
    Behavior on displayedValue {
        enabled: !dragging && !reduceMotion
        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
    }

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
            width: parent.width * Math.max(0, Math.min(100, control.displayedValue)) / 100
            height: 6
            radius: 3
            color: "white"
        }
        Rectangle {
            x: Math.max(0, Math.min(parent.width, parent.width * control.displayedValue / 100)) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            radius: 15
            color: "#e2e2e2"
        }

        MouseArea {
            anchors.fill: parent
            enabled: control.interactive
            hoverEnabled: true
            property real pressX: 0
            property bool moved: false
            Accessible.ignored: true
            function updateValue(mouseX) {
                const next = Math.round(100 * Math.max(0, Math.min(1, mouseX / track.width)))
                control.displayedValue = next
                control.edited(next)
            }
            onPressed: mouse => {
                control.forceActiveFocus()
                pressX = mouse.x
                moved = false
                control.dragging = true
            }
            onPositionChanged: mouse => {
                if (pressed && (moved || Math.abs(mouse.x - pressX) > 3)) {
                    moved = true
                    updateValue(mouse.x)
                }
            }
            onReleased: mouse => {
                if (!moved) {
                    control.dragging = false
                    updateValue(mouse.x)
                } else {
                    control.dragging = false
                }
            }
        }
    }
}
