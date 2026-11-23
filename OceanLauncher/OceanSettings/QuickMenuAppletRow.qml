import QtQuick
import OpenWave.Fonts 1.0

Item {
    id: root
    required property var model
    required property int index
    property bool largeText: false
    property bool canMoveUp: true
    property bool canMoveDown: true
    readonly property string appletId: model.id
    readonly property bool enabledInMenu: model.enabled
    readonly property string kind: model.kind
    readonly property string loadError: model.loadError
    readonly property bool configurable: model.settingsUrl.toString().length > 0 && model.enabled
    readonly property string summary: loadError.length > 0 ? qsTr("Could not load: %1").arg(loadError)
        : kind === "needed" ? qsTr("Required by the system")
        : kind === "system" ? qsTr("System applet")
        : qsTr("Added by you. Runs inside the Quick Menu.")
    readonly property int nameHeight: largeText ? 60 : 52
    signal toggled(bool enabled)
    signal moveUpRequested()
    signal moveDownRequested()
    signal configureRequested()

    height: 12 + nameHeight + summaryText.implicitHeight + 14 + (configurable ? 56 : 0)
    Accessible.role: Accessible.CheckBox
    Accessible.name: model.name
    Accessible.description: summary
    Accessible.checkable: true
    Accessible.checked: enabledInMenu
    Accessible.onToggleAction: root.toggled(!root.enabledInMenu)
    activeFocusOnTab: true
    Keys.onSpacePressed: root.toggled(!root.enabledInMenu)

    Text {
        objectName: "quickMenuAppletName"
        x: 0
        y: 12
        width: parent.width - controls.width - 16
        height: root.nameHeight
        text: root.model.name
        textFormat: Text.PlainText
        font.family: Fonts.family
        font.pixelSize: root.largeText ? 40 : 34
        font.weight: Font.DemiBold
        color: "white"
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    Row {
        id: controls
        anchors.right: parent.right
        y: 12 + (root.nameHeight - 48) / 2
        spacing: 8

        Item {
            objectName: "quickMenuAppletUp"
            width: 48
            height: 48
            opacity: root.canMoveUp ? 1 : 0.3
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Move %1 earlier").arg(root.model.name)
            Accessible.onPressAction: if (root.canMoveUp) root.moveUpRequested()
            SettingsSymbol {
                anchors.centerIn: parent
                width: 36
                height: 36
                kind: "chevron"
                rotation: -90
                color: "#cbd0d3"
            }
            TapHandler {
                enabled: root.canMoveUp
                onTapped: root.moveUpRequested()
            }
        }

        Item {
            objectName: "quickMenuAppletDown"
            width: 48
            height: 48
            opacity: root.canMoveDown ? 1 : 0.3
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Move %1 later").arg(root.model.name)
            Accessible.onPressAction: if (root.canMoveDown) root.moveDownRequested()
            SettingsSymbol {
                anchors.centerIn: parent
                width: 36
                height: 36
                kind: "chevron"
                rotation: 90
                color: "#cbd0d3"
            }
            TapHandler {
                enabled: root.canMoveDown
                onTapped: root.moveDownRequested()
            }
        }

        Item {
            width: 88
            height: 48
            Rectangle {
                id: track
                objectName: "quickMenuAppletSwitch"
                anchors.verticalCenter: parent.verticalCenter
                width: 88
                height: 44
                radius: 22
                color: root.enabledInMenu ? "#0759fa" : "#777e83"
                Rectangle {
                    x: root.enabledInMenu ? 44 : 0
                    width: 44
                    height: 44
                    radius: 22
                    color: "white"
                    Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }
                TapHandler {
                    onTapped: root.toggled(!root.enabledInMenu)
                }
            }
        }
    }

    Text {
        id: summaryText
        objectName: "quickMenuAppletSummary"
        x: 0
        y: 12 + root.nameHeight
        width: parent.width
        text: root.summary
        textFormat: Text.PlainText
        font.family: Fonts.family
        font.pixelSize: root.largeText ? 30 : 26
        font.weight: Font.Light
        color: root.loadError.length > 0 ? "#ff9e9e" : "#d4dadd"
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        objectName: "quickMenuAppletConfigure"
        visible: root.configurable
        x: 0
        y: summaryText.y + summaryText.implicitHeight + 8
        width: parent.width
        height: 44
        text: qsTr("Applet settings")
        font.family: Fonts.family
        font.pixelSize: root.largeText ? 34 : 30
        font.weight: Font.Light
        color: "#7fb0ff"
        verticalAlignment: Text.AlignVCenter
        TapHandler {
            onTapped: root.configureRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: "#33ffffff"
    }
}
