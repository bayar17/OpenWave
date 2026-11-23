import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Bluetooth")
    icon: Qt.resolvedUrl("icon.svg")
    available: Applets.System.bluetoothAvailable
    active: Applets.System.bluetoothEnabled
    statusText: !available ? qsTr("No Bluetooth adapter") : active ? qsTr("On") : qsTr("Off")
    onTriggered: Applets.System.setBluetoothEnabled(!Applets.System.bluetoothEnabled)
}
