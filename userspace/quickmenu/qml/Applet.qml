import QtQuick

Item {
    id: applet

    readonly property int appletApi: 1
    property string title: ""
    property url icon
    property bool available: true
    property bool active: false
    property bool busy: false
    property string statusText: ""
    property bool interactive: true
    property bool reduceMotion: false
    property real renderScale: 1
    property string labelFont: ""
    readonly property bool usable: available && interactive
    readonly property real discSize: Math.round(Math.min(width * 0.64, height * 0.5))

    signal triggered()

    implicitWidth: 154
    implicitHeight: 210
    opacity: available ? 1 : 0.55

    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: statusText !== "" ? statusText
        : !available ? qsTr("Unavailable") : active ? qsTr("On") : qsTr("Off")
    Accessible.checkable: true
    Accessible.checked: active
    Accessible.onPressAction: if (usable) triggered()
    activeFocusOnTab: usable
    Keys.onSpacePressed: if (usable) triggered()
    Keys.onReturnPressed: if (usable) triggered()

    Rectangle {
        id: disc
        objectName: "appletDisc"
        width: applet.discSize
        height: applet.discSize
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(applet.height * 0.08)
        color: applet.active && applet.available ? Appearance.activeColor : Appearance.inactiveColor
        scale: press.pressed ? 0.95 : 1
        Behavior on color {
            enabled: !applet.reduceMotion
            ColorAnimation { duration: 180 }
        }
        Behavior on scale {
            enabled: !applet.reduceMotion
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
        SequentialAnimation on opacity {
            running: applet.busy && !applet.reduceMotion
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 450 }
            NumberAnimation { to: 1; duration: 450 }
            onRunningChanged: if (!running) disc.opacity = 1
        }

        Image {
            objectName: "appletIcon"
            anchors.centerIn: parent
            width: Math.round(disc.width * 0.62)
            height: width
            source: applet.icon
            sourceSize: Qt.size(Math.ceil(width * 2 * applet.renderScale), Math.ceil(height * 2 * applet.renderScale))
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
    }

    Text {
        objectName: "appletLabel"
        anchors.top: disc.bottom
        anchors.topMargin: Math.round(applet.height * 0.05)
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        text: applet.title
        color: Appearance.textColor
        font.family: applet.labelFont
        font.pixelSize: Math.max(12, Math.round(applet.width * 0.19))
        font.weight: Font.Light
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        lineHeight: 1.1
    }

    TapHandler {
        id: press
        enabled: applet.usable
        onTapped: applet.triggered()
    }
}
