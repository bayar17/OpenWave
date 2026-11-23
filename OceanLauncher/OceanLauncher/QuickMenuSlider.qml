import QtQuick
import OpenWave.QuickMenu as Applets

Item {
    id: control
    property real value: 50
    property string iconSource: ""
    property real renderScale: 1
    property bool warningRange: false
    property bool interactive: true
    property bool reduceMotion: false
    property string accessibleName: ""
    property string labelFont: ""
    property real displayedValue: value
    property bool dragging: false
    readonly property int percent: Math.round(Math.max(0, Math.min(100, displayedValue)))
    signal edited(real value)

    Accessible.role: Accessible.Slider
    Accessible.name: accessibleName
    Accessible.description: qsTr("%1 percent").arg(percent)
    activeFocusOnTab: interactive
    Keys.onLeftPressed: if (interactive) edited(Math.max(0, Math.round(value) - 1))
    Keys.onRightPressed: if (interactive) edited(Math.min(100, Math.round(value) + 1))
    onValueChanged: if (!dragging) displayedValue = value
    Behavior on displayedValue {
        enabled: !dragging && !reduceMotion
        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
    }

    width: 655
    height: 126

    IconImage {
        x: 0
        y: 3
        width: 65
        height: 65
        source: control.iconSource
        renderScale: control.renderScale
    }

    Item {
        id: track
        objectName: "sliderTrack"
        x: 87
        y: 0
        width: parent.width - x
        height: 72

        Rectangle {
            x: 0
            y: 33
            width: parent.width
            height: 6
            radius: 3
            color: Applets.Appearance.sliderTrack
        }
        Rectangle {
            visible: control.warningRange
            x: parent.width * 0.68
            y: 33
            width: parent.width - x
            height: 6
            radius: 3
            color: Applets.Appearance.warningColor
        }
        Rectangle {
            x: 0
            y: 33
            width: parent.width * control.percent / 100
            height: 6
            radius: 3
            color: Applets.Appearance.sliderFill
        }
        Rectangle {
            x: Math.max(0, Math.min(parent.width, parent.width * control.displayedValue / 100)) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            radius: 15
            color: Applets.Appearance.sliderHandle
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

    Text {
        objectName: "sliderValueLabel"
        x: track.x
        y: 76
        width: track.width
        height: 44
        text: qsTr("%1: %%2").arg(control.accessibleName).arg(control.percent)
        color: Applets.Appearance.textColor
        font.family: control.labelFont
        font.pixelSize: 32
        font.weight: Font.Light
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
