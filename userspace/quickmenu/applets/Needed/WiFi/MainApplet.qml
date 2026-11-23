import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Wi-Fi")
    icon: Qt.resolvedUrl("icon.svg")
    available: Applets.System.wifiAvailable
    active: Applets.System.wifiEnabled
    statusText: !available ? qsTr("No Wi-Fi adapter")
        : !active ? qsTr("Off") : Applets.System.wifiConnected ? qsTr("Connected") : qsTr("On")
    onTriggered: Applets.System.setWifiEnabled(!Applets.System.wifiEnabled)
}
