import QtQuick
import OpenWave.QuickMenu as Applets
import "parts"

Applets.Applet {
    title: Label.text
    icon: Qt.resolvedUrl("parts/glyph.svg")
    active: Label.on
}
