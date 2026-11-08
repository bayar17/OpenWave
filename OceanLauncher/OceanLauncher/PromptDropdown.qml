import QtQuick
import QtQuick.Controls
import OpenWave.Controls 1.0
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0

WRComboBox {
    id: control
    property var choices: ({})
    property string value: ""
    property string label: ""
    property real uiScale: 1
    reduceMotion: WaterRouge.reducedMotion
    property real reveal: 0
    property real sceneScale: 1
    signal selected(string value)
    model: Object.keys(choices).map(key => ({value: key, label: choices[key]}))
    textRole: "label"
    valueRole: "value"
    currentIndex: Math.max(0, Object.keys(choices).indexOf(value))
    onActivated: index => selected(model[index].value)
    Accessible.name: label
    fontPixelSize: Math.max(1, Math.round(26 * uiScale))
    itemTextColor: WaterRouge.textColor
    menuColor: WaterRouge.raisedSurfaceColor
    leftPadding: 0; rightPadding: 38 * uiScale
    contentItem: Text {
        text: (control.label.length ? control.label + ": " : "") + control.currentText
        textFormat: Text.PlainText; color: "white"
        font: Fonts.font(Math.round(control.fontPixelSize * WaterRouge.textScale), Font.ExtraLight)
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        text: "⌄"; color: "white"
        font: Fonts.font(Math.round(control.fontPixelSize * WaterRouge.textScale), Font.ExtraLight)
    }
    background: Item {}
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2 * control.uiScale; color: "#aaffffff"; antialiasing: true }
    popup: Popup {
        id: menu
        objectName: "systemPromptDropdownMenu"
        y: control.height
        width: control.width
        height: Math.min(control.count, 5) * 58 * control.uiScale
        padding: 0; modal: true; dim: false; focus: true
        scale: control.sceneScale
        transformOrigin: Item.TopLeft
        onAboutToShow: {
            const left = control.mapToItem(null, 0, 0)
            const right = control.mapToItem(null, control.width, 0)
            control.sceneScale = control.width > 0 ? Math.abs(right.x - left.x) / control.width : 1
        }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Item {}
        enter: Transition {
            NumberAnimation { target: control; property: "reveal"; from: 0; to: 1
                duration: control.reduceMotion ? 0 : 180; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { target: control; property: "reveal"; to: 0
                duration: control.reduceMotion ? 0 : 140; easing.type: Easing.InCubic }
        }
        contentItem: Item {
            clip: true
            Item {
                objectName: "systemPromptDropdownSurface"
                y: control.reveal >= 1 ? 0 : -height * (1 - control.reveal)
                width: parent.width; height: parent.height
                Rectangle { anchors.fill: parent; radius: WaterRouge.cornerRadiusMedium * control.uiScale; color: WaterRouge.raisedSurfaceColor }
                ListView {
                    anchors.fill: parent; clip: true
                    model: control.delegateModel
                    currentIndex: control.currentIndex
                    boundsBehavior: Flickable.StopAtBounds
                }
            }
        }
    }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        objectName: "systemPromptChoice_" + modelData.value
        width: control.width; height: 58 * control.uiScale
        highlighted: control.highlightedIndex === index
        font: control.font
        contentItem: Text {
            text: modelData.label; textFormat: Text.PlainText
            color: WaterRouge.textColor
            font: Fonts.font(Math.round(control.fontPixelSize * WaterRouge.textScale), Font.ExtraLight)
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.down || parent.highlighted ? "#24ffffff" : "transparent"
            radius: WaterRouge.cornerRadiusMedium * control.uiScale
        }
    }
}
