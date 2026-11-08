import QtQuick

Item {
    id: root

    property alias source: icon.source
    property string title: ""
    property string appId: title.toLowerCase()
    property bool launching: false
    property bool draggable: false
    property bool qaDiagnostics: false
    property bool qaTargetReady: false
    readonly property string qaTargetColor: appId === "com.openwave.ocean" ? "#00d7ff"
        : appId === "com.openwave.photos" ? "#ff00d7"
        : appId === "com.openwave.waterrouge-gallery" ? "#ffd700"
        : appId === "com.openwave.settings" ? "#70ff00" : "transparent"
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
    Behavior on scale {
        NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
    }
    Behavior on opacity {
        NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
    }
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.onPressAction: clicked(root)

    function logQaTarget() {
        if (!qaDiagnostics || !qaTargetReady || qaTargetColor === "transparent") return
        const a = mapToItem(null, 0, 0)
        const b = mapToItem(null, width, height)
        console.info("OCEAN_QA delegate-ready appId=" + appId
            + " windowBounds=" + a.x + "," + a.y + "," + (b.x - a.x) + "," + (b.y - a.y))
    }
    onQaTargetReadyChanged: if (qaTargetReady) Qt.callLater(logQaTarget)

    // Opaque QA borders identify live clickable bounds in the rendered frame.
    // They accept no input and are absent from ordinary sessions.
    Rectangle {
        anchors.fill: parent
        z: 100
        color: "transparent"
        border.width: 5
        border.color: root.qaTargetColor
        visible: root.qaDiagnostics && root.qaTargetReady
                 && root.qaTargetColor !== "transparent"
    }

    IconImage {
        id: icon

        anchors.fill: parent

        renderScale: root.renderScale
    }

    TapHandler {
        id: tap
        onTapped: {
            if (root.dragging)
                return
            if (root.qaDiagnostics) {
                const topLeft = root.mapToItem(null, 0, 0)
                const bottomRight = root.mapToItem(null, root.width, root.height)
                console.info("OCEAN_QA delegate-activated appId=" + root.appId
                    + " windowBounds=" + topLeft.x + "," + topLeft.y + ","
                    + (bottomRight.x - topLeft.x) + "," + (bottomRight.y - topLeft.y)
                    + " windowCenter=" + ((topLeft.x + bottomRight.x) / 2)
                    + "," + ((topLeft.y + bottomRight.y) / 2))
            }
            root.clicked(root)
        }
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
