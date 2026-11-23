import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Flight Mode")
    icon: Qt.resolvedUrl("icon.svg")
    available: Applets.System.connected
    active: Applets.System.airplaneMode
    statusText: !available ? qsTr("Unavailable") : active ? qsTr("On") : qsTr("Off")
    onTriggered: Applets.System.setAirplaneMode(!Applets.System.airplaneMode)
}
