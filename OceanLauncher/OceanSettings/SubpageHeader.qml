import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Item {
    id: header
    property string title: ""
    property string actionText: ""
    property bool actionEnabled: true
    property bool largeText: false
    property string backObjectName: ""
    property string titleObjectName: ""
    property string actionObjectName: ""
    property string backLabel: qsTr("Back")
    signal backClicked()
    signal actionClicked()

    width: parent ? parent.width : 0
    height: 90

    AbstractButton {
        id: backButton
        objectName: header.backObjectName
        width: 60
        height: parent.height
        Accessible.name: header.backLabel
        onClicked: header.backClicked()
        background: Item {}
        contentItem: Item {
            SettingsSymbol {
                x: -12
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                height: 44
                kind: "chevron"
                rotation: 180
                color: "white"
            }
        }
    }

    Text {
        objectName: header.titleObjectName
        anchors.left: backButton.right
        anchors.right: actionButton.visible ? actionButton.left : parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        text: header.title
        textFormat: Text.PlainText
        color: "white"
        font.family: Fonts.family
        font.weight: Font.ExtraLight
        font.pixelSize: header.largeText ? 52 : 46
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    AbstractButton {
        id: actionButton
        objectName: header.actionObjectName
        visible: header.actionText.length > 0
        anchors.right: parent.right
        width: 170
        height: parent.height
        enabled: header.actionEnabled
        opacity: enabled ? 1 : 0.4
        Accessible.name: header.actionText
        onClicked: header.actionClicked()
        background: Item {}
        contentItem: Text {
            text: header.actionText
            color: "white"
            font.family: Fonts.family
            font.weight: Font.ExtraLight
            font.pixelSize: 36
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}
