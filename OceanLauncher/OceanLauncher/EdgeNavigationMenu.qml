import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root

    property Item backdrop: null
    property bool canOpen: true
    property bool backAvailable: false
    property bool reduceMotion: false
    property real renderScale: 1
    property real safeTop: 70
    property real safeBottom: 0
    property int swipeDirection: -1
    property bool open: false
    property real anchorY: height / 2
    readonly property real edgeWidth: 32
    property real progress: open ? 1 : 0
    signal backRequested()
    signal homeRequested()

    onCanOpenChanged: if (!canOpen) close()
    onVisibleChanged: if (!visible) close()

    function reveal(y) {
        if (!canOpen) return
        anchorY = y
        open = true
    }
    function close() { open = false }

    Behavior on progress {
        NumberAnimation { duration: root.reduceMotion ? 90 : 180; easing.type: Easing.OutCubic }
    }

    MouseArea {
        id: outside
        objectName: "edgeNavigationDismiss"
        anchors.fill: parent
        visible: root.progress > 0
        onClicked: root.close()
        onWheel: wheel => { root.close(); wheel.accepted = true }
    }

    MouseArea {
        id: edge
        objectName: "edgeNavigationTrigger"
        anchors.right: parent.right
        width: root.edgeWidth
        height: parent.height
        enabled: root.canOpen && (!root.open || pressed)
        preventStealing: true
        property real pressX: 0
        property real pressY: 0
        property bool rejected: false
        onPressed: mouse => {
            pressX = mouse.x
            pressY = mouse.y
            rejected = false
        }
        onPositionChanged: mouse => {
            if (!pressed || rejected || root.open) return
            const dx = mouse.x - pressX
            const dy = mouse.y - pressY
            if (Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(dx)) {
                rejected = true
                return
            }
            if (dx * root.swipeDirection >= 12 && Math.abs(dx) > Math.abs(dy))
                root.reveal(pressY)
        }
    }

    Item {
        id: panel
        objectName: "edgeNavigationPanel"
        width: 82
        height: 176
        x: root.width - width - 16 + (root.reduceMotion ? 0 : (width + 16) * (1 - root.progress))
        y: Math.max(root.safeTop + 12,
                    Math.min(root.height - root.safeBottom - height - 12, root.anchorY - height / 2))
        opacity: root.progress
        visible: root.progress > 0
        enabled: root.open

        MouseArea { anchors.fill: parent }

        Item {
            anchors.fill: parent
            visible: root.backdrop !== null
            layer.enabled: visible && panel.visible
            layer.effect: MultiEffect { maskEnabled: true; maskSource: roundedMask }

            ShaderEffectSource {
                id: frostSource
                width: panel.width + 64
                height: panel.height + 64
                sourceItem: panel.visible ? root.backdrop : null
                sourceRect: {
                    const origin = root.backdrop ? panel.mapToItem(root.backdrop, -32, -32) : Qt.point(0, 0)
                    return Qt.rect(origin.x, origin.y, width, height)
                }
                live: panel.visible
                hideSource: false
                visible: false
            }
            MultiEffect {
                x: -32; y: -32
                width: frostSource.width; height: frostSource.height
                source: frostSource
                blurEnabled: true
                blur: 1
                blurMax: 32
                autoPaddingEnabled: false
            }
        }
        Rectangle {
            id: roundedMask
            anchors.fill: parent
            radius: 20
            color: "white"
            visible: false
            layer.enabled: panel.visible
        }
        Rectangle { anchors.fill: parent; radius: 20; color: "#997b8386" }

        Column {
            anchors.fill: parent
            AbstractButton {
                objectName: "edgeNavigationBack"
                width: parent.width; height: parent.height / 2
                enabled: root.backAvailable
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Go back")
                background: Rectangle {
                    radius: 20
                    color: parent.down ? "#26ffffff" : "transparent"
                }
                contentItem: Item {
                    IconImage {
                        anchors.centerIn: parent
                        width: 48; height: 48
                        source: "assets/navigation/back.svg"
                        renderScale: root.renderScale
                        opacity: root.backAvailable ? 1 : 0.35
                    }
                }
                onClicked: { root.close(); root.backRequested() }
            }
            AbstractButton {
                objectName: "edgeNavigationHome"
                width: parent.width; height: parent.height / 2
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Go to home screen")
                background: Rectangle {
                    radius: 20
                    color: parent.down ? "#26ffffff" : "transparent"
                }
                contentItem: Item {
                    IconImage {
                        anchors.centerIn: parent
                        width: 48; height: 48
                        source: "assets/navigation/home.svg"
                        renderScale: root.renderScale
                    }
                }
                onClicked: { root.close(); root.homeRequested() }
            }
        }
    }

    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: root.open
        onActivated: root.close()
    }
}
