pragma Singleton
import QtQuick

QtObject {
    property string palette: "ocean"
    readonly property color accentColor: "#00d9e7"
    readonly property color highlightColor: "#00d9e7"
    readonly property color pressedColor: "#18ffffff"
    readonly property color focusColor: "#ffffff"
    function selectPalette(key) { palette = key }
}
