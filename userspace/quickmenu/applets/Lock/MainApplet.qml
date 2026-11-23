import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Lock Phone")
    icon: Qt.resolvedUrl("icon.svg")
    available: true
    active: false
    statusText: qsTr("Lock the current OpenWave session")
    onTriggered: Applets.System.lock()
}
