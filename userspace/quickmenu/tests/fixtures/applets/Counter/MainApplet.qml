import QtQuick
import OpenWave.QuickMenu as Applets

Applets.Applet {
    id: root
    property int taps: 0
    title: "Counter"
    available: true
    active: taps % 2 === 1
    onTriggered: taps += 1
}
