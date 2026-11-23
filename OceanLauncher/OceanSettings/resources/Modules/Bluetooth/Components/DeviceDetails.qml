import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0
import OceanSettings 1.0

Flickable {
    id: root
    property var device: ({})
    property string deviceName: ""
    property bool connecting: false
    property bool busy: false
    property bool largeText: false
    readonly property bool hasDevice: !!device.path
    signal backRequested()
    signal nicknameRequested()
    signal disconnectRequested()
    signal connectRequested()
    signal forgetRequested()

    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    Column {
        id: column
        width: parent.width

        AbstractButton {
            objectName: "bluetoothInfoBack"
            width: parent.width; height: 86
            Accessible.name: qsTr("Back to Bluetooth devices")
            onClicked: root.backRequested()
            background: Item {}
            contentItem: Text {
                text: "‹ " + qsTr("Bluetooth")
                color: "white"; font.family: Fonts.family; font.pixelSize: 34
                verticalAlignment: Text.AlignVCenter
            }
        }
        Text {
            objectName: "bluetoothInfoName"
            width: parent.width; bottomPadding: 22
            text: root.deviceName; textFormat: Text.PlainText
            wrapMode: Text.Wrap; color: "white"; font.family: Fonts.family
            font.weight: Font.DemiBold; font.pixelSize: root.largeText ? 44 : 40
        }
        SettingsRow {
            objectName: "bluetoothInfoStatus"
            heading: qsTr("Status")
            detail: root.connecting ? qsTr("Connecting…")
                  : root.device.connected ? qsTr("Connected") : qsTr("Previously connected")
            largeText: root.largeText; enabled: false
        }
        SettingsRow {
            objectName: "bluetoothNickname"
            heading: qsTr("Edit nickname")
            detail: root.deviceName
            largeText: root.largeText
            enabled: root.hasDevice && !root.busy
            onClicked: root.nicknameRequested()
        }
        SettingsRow {
            objectName: "bluetoothDisconnect"
            visible: !!root.device.connected
            heading: qsTr("Disconnect")
            detail: qsTr("Keep this device paired")
            largeText: root.largeText
            enabled: !root.busy
            onClicked: root.disconnectRequested()
        }
        SettingsRow {
            objectName: "bluetoothConnect"
            visible: root.hasDevice && !root.device.connected
            heading: qsTr("Connect")
            largeText: root.largeText
            enabled: !root.busy
            onClicked: root.connectRequested()
        }
        SettingsRow {
            objectName: "bluetoothForget"
            heading: qsTr("Forget this device")
            detail: qsTr("Remove its saved pairing")
            largeText: root.largeText
            enabled: root.hasDevice && !root.busy
            onClicked: root.forgetRequested()
        }
    }
}
