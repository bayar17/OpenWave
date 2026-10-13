import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    required property var network
    property bool largeText: false
    property bool longPressTriggered: false
    signal longPressed()

    readonly property bool connected: !!network.connected
    readonly property bool connecting: !!network.connecting
    readonly property bool openNetwork: network.security === "Open"
                                       || network.security === "Enhanced Open"
    x: 47
    width: parent ? parent.width - 94 : 580
    height: connected || connecting ? 94 : (largeText ? 82 : 66)
    Accessible.name: network.ssid + ", " + (connected ? "Connected" :
                                               connecting ? "Connecting" : network.security)
    Accessible.role: Accessible.Button

    onPressedChanged: {
        if (pressed && network.saved) {
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
            if (root.pressed && root.network.saved) {
                root.longPressTriggered = true
                root.longPressed()
            }
        }
    }
    background: Item {
        Rectangle {
            anchors.fill: parent
            color: root.down ? "#18ffffff" : "transparent"
        }
        Rectangle {
            anchors.fill: parent
            visible: root.visualFocus
            color: "transparent"
            border.color: "#00b7f0"
            border.width: 2
        }
    }
    contentItem: Item {
        Text {
            x: 8
            y: 11
            width: parent.width - 150
            text: root.network.ssid
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.family: "Inter"
            font.weight: Font.DemiBold
            font.pixelSize: root.largeText ? 38 : 32
            color: "white"
        }
        Text {
            visible: root.connected || root.connecting
            x: 8
            y: 49
            width: parent.width - 150
            text: root.connected ? "Connected" : "Connecting…"
            font.family: "Inter"
            font.weight: Font.Light
            font.pixelSize: root.largeText ? 34 : 30
            color: "white"
        }
        SettingsSymbol {
            x: parent.width - 119
            y: root.connected || root.connecting ? 31 : 7
            width: 48
            height: 48
            kind: root.openNetwork ? "unlock" : "lock"
        }
        SettingsSymbol {
            x: parent.width - 59
            y: root.connected || root.connecting ? 31 : 7
            width: 48
            height: 48
            kind: "wifi"
        }
    }
}
