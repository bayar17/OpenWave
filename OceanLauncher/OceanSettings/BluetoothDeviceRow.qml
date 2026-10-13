import QtQuick

Item {
    id: root
    required property var device
    property bool largeText: false
    property string actionState: ""
    readonly property string statusText: actionState === "pairing" ? qsTr("Pairing…")
        : actionState === "connecting" ? qsTr("Connecting…")
        : device.connected ? qsTr("Connected") : ""
    readonly property string deviceLabel: device.alias || device.name || device.address
    readonly property bool remembered: !!(device.connected || device.paired || device.trusted)
    signal clicked()
    signal longPressed()
    height: statusText.length ? (largeText ? 124 : 110) : (largeText ? 104 : 90)
    Accessible.role: Accessible.Button
    Accessible.name: deviceLabel + (statusText.length ? ", " + statusText : "")
    Accessible.onPressAction: clicked()
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Rectangle { anchors.fill: parent; color: pointer.pressed ? "#18ffffff" : "transparent" }
    SettingsSymbol {
        x: 0; anchors.verticalCenter: parent.verticalCenter
        width: 68; height: 68; kind: "bluetooth"
    }
    Column {
        x: 82; width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        Text {
            objectName: "bluetoothDeviceTitle"
            width: parent.width; text: root.deviceLabel; textFormat: Text.PlainText
            font.family: "Inter"; font.pixelSize: root.largeText ? 42 : 36
            font.weight: Font.DemiBold; color: "white"; elide: Text.ElideRight
        }
        Text {
            objectName: "bluetoothConnectedStatus"
            visible: root.statusText.length > 0
            width: parent.width; text: root.statusText
            font.family: "Inter"; font.pixelSize: root.largeText ? 34 : 30
            font.weight: Font.DemiBold; color: "white"
        }
    }
    TapHandler {
        id: pointer
        property bool held: false
        longPressThreshold: 0.5
        onPressedChanged: if (pressed) held = false
        onLongPressed: if (root.remembered) { held = true; root.longPressed() }
        onTapped: if (!held) root.clicked()
    }
}
