import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Item {
    id: menuRoot
    property var page: null
    readonly property bool wanted: !!page && page.menuPreset !== null && !!page.menuCard
    readonly property bool reduceMotion: !!page && page.reduceMotion
    readonly property bool largeText: !!page && page.largeText
    property var preset: null
    property var sourceCard: null
    property real unit: 1
    property real originX: 0
    property real originY: 0
    property real cardWidth: 0
    property real cardHeight: 170
    property bool opensUp: false
    property real dim: 0
    property real drawer: 0
    property real itemsOpacity: 0
    readonly property int itemHeight: 78
    readonly property int bodyPadding: 12
    readonly property int bodyHeight: itemHeight * 4 + bodyPadding * 2
    readonly property int gap: 6
    readonly property var entries: preset ? [
        {key: "set", title: qsTr("Set Appearance"), available: true},
        {key: "edit", title: qsTr("Edit Appearance"), available: !!preset.user},
        {key: "favorite", title: preset.favorite ? qsTr("Remove from favorite") : qsTr("Add to favorite"), available: true},
        {key: "delete", title: qsTr("Delete Appearance"), available: !!preset.user}
    ] : []

    visible: preset !== null
    z: 998

    function place() {
        const overlay = Overlay.overlay
        const target = sourceCard
        if (!overlay || !target || target.width <= 0)
            return
        const topLeft = target.mapToItem(overlay, 0, 0)
        const bottomRight = target.mapToItem(overlay, target.width, target.height)
        cardWidth = target.width
        cardHeight = target.height
        unit = Math.max(0.01, (bottomRight.x - topLeft.x) / target.width)
        originX = topLeft.x
        originY = topLeft.y
        opensUp = bottomRight.y + (gap + bodyHeight) * unit > overlay.height - 12 * unit
    }

    function finishClose() {
        preset = null
        sourceCard = null
    }

    Binding {
        target: menuRoot
        property: "preset"
        value: menuRoot.page ? menuRoot.page.menuPreset : null
        when: menuRoot.wanted
        restoreMode: Binding.RestoreNone
    }

    onWantedChanged: {
        if (wanted) {
            closeAnimation.stop()
            preset = page.menuPreset
            sourceCard = page.menuCard
            place()
            dim = 0
            drawer = 0
            itemsOpacity = 0
            openAnimation.restart()
        } else if (preset !== null) {
            openAnimation.stop()
            closeAnimation.restart()
        }
    }

    SequentialAnimation {
        id: openAnimation
        NumberAnimation { target: menuRoot; property: "dim"; to: 1; duration: menuRoot.reduceMotion ? 0 : 200; easing.type: Easing.OutCubic }
        NumberAnimation { target: menuRoot; property: "drawer"; to: 1; duration: menuRoot.reduceMotion ? 0 : 280; easing.type: Easing.OutCubic }
        NumberAnimation { target: menuRoot; property: "itemsOpacity"; to: 1; duration: menuRoot.reduceMotion ? 0 : 200; easing.type: Easing.OutCubic }
    }

    SequentialAnimation {
        id: closeAnimation
        NumberAnimation { target: menuRoot; property: "itemsOpacity"; to: 0; duration: menuRoot.reduceMotion ? 0 : 110; easing.type: Easing.InCubic }
        NumberAnimation { target: menuRoot; property: "drawer"; to: 0; duration: menuRoot.reduceMotion ? 0 : 200; easing.type: Easing.InCubic }
        NumberAnimation { target: menuRoot; property: "dim"; to: 0; duration: menuRoot.reduceMotion ? 0 : 160; easing.type: Easing.OutCubic }
        ScriptAction { script: menuRoot.finishClose() }
    }

    Rectangle {
        objectName: "settingsAppearanceDim"
        anchors.fill: parent
        color: "black"
        opacity: menuRoot.dim * 0.55
    }

    MouseArea {
        objectName: "settingsAppearanceMenuDismiss"
        anchors.fill: parent
        enabled: menuRoot.wanted
        onClicked: if (menuRoot.page) menuRoot.page.closeMenu()
    }

    Item {
        id: stage
        x: menuRoot.originX
        y: menuRoot.originY
        width: menuRoot.cardWidth
        height: menuRoot.cardHeight
        scale: menuRoot.unit
        transformOrigin: Item.TopLeft

        AppearanceCard {
            id: replica
            objectName: "settingsAppearanceMenuCard"
            width: parent.width
            height: parent.height
            interactive: false
            preset: menuRoot.preset ? menuRoot.preset : ({})
            selected: !!menuRoot.page && !!menuRoot.preset && menuRoot.preset.id === menuRoot.page.current
            largeText: menuRoot.largeText
            reduceMotion: true
        }

        Item {
            id: drawerClip
            objectName: "settingsAppearanceMenu"
            x: 0
            width: parent.width
            height: menuRoot.bodyHeight * menuRoot.drawer
            y: menuRoot.opensUp ? -menuRoot.gap - height : parent.height + menuRoot.gap
            clip: true

            Rectangle {
                id: body
                width: parent.width
                height: menuRoot.bodyHeight
                y: menuRoot.opensUp ? menuRoot.bodyHeight * (1 - menuRoot.drawer) : -menuRoot.bodyHeight * (1 - menuRoot.drawer)
                radius: 36
                color: "#d4757a7e"
                border.color: "#52ffffff"
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: menuRoot.bodyPadding

                    Repeater {
                        model: menuRoot.entries
                        delegate: AbstractButton {
                            id: entry
                            required property var modelData
                            required property int index
                            objectName: "settingsAppearanceMenu_" + modelData.key
                            width: body.width - menuRoot.bodyPadding * 2
                            height: menuRoot.itemHeight
                            enabled: menuRoot.wanted && modelData.available && menuRoot.itemsOpacity > 0.5
                            opacity: (modelData.available ? 1 : 0.35)
                                * Math.max(0, Math.min(1, menuRoot.itemsOpacity * 1.8 - index * 0.2))
                            Accessible.name: modelData.title
                            onClicked: if (menuRoot.page) menuRoot.page.menuAction(modelData.key)
                            background: Rectangle {
                                color: entry.down ? "#26ffffff" : "transparent"
                                radius: 24
                            }
                            contentItem: Text {
                                text: entry.modelData.title
                                textFormat: Text.PlainText
                                color: "white"
                                font.family: Fonts.family
                                font.weight: Font.ExtraLight
                                font.pixelSize: menuRoot.largeText ? 40 : 34
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
