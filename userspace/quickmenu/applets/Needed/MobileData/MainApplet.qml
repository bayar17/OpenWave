import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Mobile Data")
    icon: Qt.resolvedUrl("icon.svg")
    available: Applets.System.cellularAvailable
    active: false
    statusText: !available ? qsTr("No modem")
        : !Applets.System.mobileDataSupported ? qsTr("Cannot be controlled on this system") : qsTr("Off")
    onTriggered: Applets.System.setMobileDataEnabled(!active)
}
