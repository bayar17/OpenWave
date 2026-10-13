import QtQuick

Item {
    id: root

    property alias source: icon.source
    property string title: ""
    property string appId: title.toLowerCase()
    property bool launching: false
    property bool draggable: false
    readonly property bool dragging: dragHandler.active
    property real lastDragX: width / 2
    property real lastDragY: height / 2
    readonly property Item imageItem: icon
    readonly property Item titleItem: null
    property real renderScale: 1
    readonly property bool pressed: tap.pressed
    signal clicked(var app)
    signal dragStarted(var app, real x, real y)
    signal dragMoved(var app, real x, real y)
    signal dragFinished(var app, real x, real y)

    width: 130
    height: 130
    scale: pressed ? 0.9 : 1
    opacity: launching ? 0 : dragging ? 0.3 : (pressed ? 0.7 : 1)
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.onPressAction: clicked(root)

    IconImage {
        id: icon

        anchors.fill: parent

        renderScale: root.renderScale
    }

    TapHandler {
        id: tap
        onTapped: if (!root.dragging) root.clicked(root)
    }
    DragHandler {
        id: dragHandler
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
}
