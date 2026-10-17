import QtQuick

Item {
    id: root

    property alias source: icon.source
    property string title: ""
    property string appId: title.toLowerCase()
    property bool launching: false
    property bool draggable: false
    property bool scrollable: false
    property bool touchDragArmed: false
    readonly property bool dragging: dragHandler.active || touchDragHandler.active
    property real lastDragX: width / 2
    property real lastDragY: height / 2
    readonly property Item imageItem: icon
    readonly property Item titleItem: label
    property real renderScale: 1
    readonly property bool pressed: tap.pressed
    signal clicked(var app)
    signal dragStarted(var app, real x, real y)
    signal dragMoved(var app, real x, real y)
    signal dragFinished(var app, real x, real y)

    width: 150
    height: 175
    scale: pressed ? 0.9 : 1
    opacity: launching ? 0 : dragging ? 0.3 : (pressed ? 0.7 : 1)
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.onPressAction: clicked(root)

    TapHandler {
        id: tap
        onTapped: if (!root.dragging) root.clicked(root)
        onLongPressed: root.touchDragArmed = true
        onPressedChanged: if (!pressed && !touchDragHandler.active) root.touchDragArmed = false
    }
    DragHandler {
        id: dragHandler
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus | PointerDevice.Airbrush
        enabled: root.draggable
        target: null
        onActiveChanged: {
            if (active) {
                root.lastDragX = centroid.position.x
                root.lastDragY = centroid.position.y
                root.dragStarted(root, root.lastDragX, root.lastDragY)
            } else root.dragFinished(root, root.lastDragX, root.lastDragY)
        }
        onTranslationChanged: {
            if (!active) return
            root.lastDragX = centroid.position.x
            root.lastDragY = centroid.position.y
            root.dragMoved(root, root.lastDragX, root.lastDragY)
        }
    }

    DragHandler {
        id: touchDragHandler
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        enabled: root.draggable
        dragThreshold: root.scrollable && !root.touchDragArmed
                       ? 32767 : Qt.styleHints.startDragDistance
        target: null
        onActiveChanged: {
            if (active) {
                root.lastDragX = centroid.position.x
                root.lastDragY = centroid.position.y
                root.dragStarted(root, root.lastDragX, root.lastDragY)
            } else {
                root.dragFinished(root, root.lastDragX, root.lastDragY)
                root.touchDragArmed = false
            }
        }
        onTranslationChanged: {
            if (active) {
                root.lastDragX = centroid.position.x
                root.lastDragY = centroid.position.y
                root.dragMoved(root, root.lastDragX, root.lastDragY)
            }
        }
    }

    IconImage {
        id: icon

        width: 130
        height: 130

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter

        renderScale: root.renderScale
    }

    Text {
        id: label
        text: root.title

        color: "white"
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight

        font.pixelSize: 30
        font.family: "Inter"
        font.weight: Font.Light

        anchors.top: icon.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
