import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0
import OpenWave.Theme 1.0

Item {
    id: chooser
    property var handlers: []
    property bool busy: false
    property string selectedId: ""
    signal openRequested(string handlerId, bool always)
    readonly property real listHeight: Math.min(350, handlers.length * 70)
    implicitHeight: listHeight + 66
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
        y: chooser.listHeight + 20
        anchors.right: parent.right; spacing: 36
        AbstractButton {
            objectName: "filesJustOnce"
            enabled: chooser.selectedId.length > 0 && !chooser.busy
            opacity: enabled ? 1 : 0.4
            implicitWidth: justOnceLabel.implicitWidth; implicitHeight: 46
            Accessible.name: qsTr("Just Once")
            contentItem: Text {
                id: justOnceLabel
                text: qsTr("Just Once"); color: Theme.accentColor; font.family: Fonts.family; font.pixelSize: 27
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: chooser.openRequested(chooser.selectedId, false)
        }
        AbstractButton {
            objectName: "filesAlways"
            enabled: chooser.selectedId.length > 0 && !chooser.busy
            opacity: enabled ? 1 : 0.4
            implicitWidth: alwaysLabel.implicitWidth; implicitHeight: 46
            Accessible.name: qsTr("Always")
            contentItem: Text {
                id: alwaysLabel
                text: qsTr("Always"); color: Theme.accentColor; font.family: Fonts.family; font.pixelSize: 27
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: chooser.openRequested(chooser.selectedId, true)
        }
    }
}
