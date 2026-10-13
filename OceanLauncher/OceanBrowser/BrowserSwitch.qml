import QtQuick
import QtQuick.Controls

Switch {
    id: control
    property bool reduceMotion: false
    implicitWidth: 100
    implicitHeight: 70
    padding: 0
    indicator: Rectangle {
        anchors.centerIn: parent
        width: 100; height: 50; radius: height / 2
        color: control.checked ? "#707070" : "#d8d8d8"
        border.width: control.visualFocus ? 2 : 0
        border.color: "#008dff"
        Rectangle {
            x: control.checked ? parent.width - width : 0
            width: 50; height: 50; radius: height / 2
            color: "white"
            Behavior on x {
                NumberAnimation { duration: control.reduceMotion ? 0 : 160; easing.type: Easing.OutCubic }
            }
        }
    }
    contentItem: Item {}
    background: Item {}
}
