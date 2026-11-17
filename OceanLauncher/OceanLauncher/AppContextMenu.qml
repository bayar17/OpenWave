import QtQuick

Item {
    id: root

    property bool open: false
    property string appId: ""
    property bool canUninstall: false
    property rect anchorRect: Qt.rect(0, 0, 0, 0)
    property bool reduceMotion: false
    property real safeTop: 0
    readonly property int rowHeight: 92
    readonly property var entries: canUninstall
        ? [{key: "uninstall", label: qsTr("Uninstall")}, {key: "details", label: qsTr("App details")}, {key: "move", label: qsTr("Move icon")}]
        : [{key: "details", label: qsTr("App details")}, {key: "move", label: qsTr("Move icon")}]

    signal uninstallRequested(string appId)
    signal detailsRequested(string appId)
    signal moveRequested(string appId)

    visible: open || panel.opacity > 0.01
    function show(id, uninstallable, rect) {
        appId = id; canUninstall = uninstallable; anchorRect = rect; open = true
    }
    function close() { open = false }

    MouseArea {
        anchors.fill: parent
        enabled: root.open
        onPressed: root.close()
    }

    Rectangle {
        id: panel
        objectName: "appContextMenu"
        width: 380
        height: root.entries.length * root.rowHeight
        radius: 30
        color: "#F2394148"
        border.width: 1
        border.color: "#40ffffff"
        x: Math.max(20, Math.min(root.width - width - 20, root.anchorRect.x + root.anchorRect.width / 2 - width / 2))
        y: {
            const above = root.anchorRect.y - height - 14
            return above >= root.safeTop + 12 ? above : Math.min(root.height - height - 20, root.anchorRect.y + root.anchorRect.height + 14)
        }
        opacity: root.open ? 1 : 0
        scale: root.open ? 1 : 0.92
        transformOrigin: Item.Bottom
        Behavior on opacity { NumberAnimation { duration: root.reduceMotion ? 0 : 140; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: root.reduceMotion ? 0 : 140; easing.type: Easing.OutCubic } }

        Column {
            anchors.fill: parent
            Repeater {
                model: root.entries
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: panel.width
                    height: root.rowHeight
                    objectName: "appMenu_" + modelData.key
                    Rectangle {
                        anchors.fill: parent
                        radius: panel.radius
                        color: "white"
                        opacity: rowTap.pressed ? 0.16 : 0
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 34
                        text: row.modelData.label
                        color: row.modelData.key === "uninstall" ? "#ff8f8a" : "white"
                        font.pixelSize: 34
                        font.family: "Inter"
                        font.weight: Font.Light
                    }
                    Rectangle {
                        visible: row.index < root.entries.length - 1
                        anchors.bottom: parent.bottom
                        x: 24; width: parent.width - 48; height: 1
                        color: "#30ffffff"
                    }
                    TapHandler {
                        id: rowTap
                        onTapped: {
                            const id = root.appId
                            root.close()
                            if (row.modelData.key === "uninstall") root.uninstallRequested(id)
                            else if (row.modelData.key === "details") root.detailsRequested(id)
                            else root.moveRequested(id)
                        }
                    }
                }
            }
        }
    }
}
