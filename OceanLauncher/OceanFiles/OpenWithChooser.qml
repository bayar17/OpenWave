import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Item {
    id: chooser
    property var handlers: []
    property bool busy: false
    property string selectedId: ""
    signal openRequested(string handlerId, bool always)
    implicitHeight: Math.min(350, handlers.length * 70) + 68
    onHandlersChanged: if (!handlers.some(h => h.id === selectedId)) selectedId = ""
    Flickable {
        width: parent.width; height: Math.min(350, chooser.handlers.length * 70)
        contentHeight: choices.height; clip: true
        Column {
            id: choices; width: parent.width
            Repeater {
                model: chooser.handlers
                AbstractButton {
                    required property var modelData
                    objectName: "filesHandler_" + modelData.id
                    width: choices.width; height: 70
                    enabled: !chooser.busy
                    Accessible.name: modelData.name
                    contentItem: Text {
                        text: (chooser.selectedId === modelData.id ? "✓ " : "") + modelData.name
                        textFormat: Text.PlainText
                        color: "white"; font.family: Fonts.family; font.pixelSize: 30
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    onClicked: chooser.selectedId = modelData.id
                }
            }
        }
    }
    Row {
        anchors.bottom: parent.bottom; spacing: 32
        Button {
            objectName: "filesJustOnce"; text: qsTr("Just Once")
            enabled: chooser.selectedId.length > 0 && !chooser.busy
            onClicked: chooser.openRequested(chooser.selectedId, false)
        }
        Button {
            objectName: "filesAlways"; text: qsTr("Always")
            enabled: chooser.selectedId.length > 0 && !chooser.busy
            onClicked: chooser.openRequested(chooser.selectedId, true)
        }
    }
}
