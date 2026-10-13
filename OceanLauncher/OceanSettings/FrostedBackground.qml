import QtQuick
import QtQuick.Effects

Item {
    property Item source
    MultiEffect {
        anchors.fill: parent
        source: parent.source
        blurEnabled: true
        blurMax: 64
        blur: 1
        autoPaddingEnabled: false
    }
    Rectangle { anchors.fill: parent; color: "#b3454e53" }
    Rectangle { anchors.fill: parent; color: "black"; opacity: 0.3 }
}
