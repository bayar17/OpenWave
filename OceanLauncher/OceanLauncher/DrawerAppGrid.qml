import QtQuick

Flickable {
    id: root

    property var apps: []
    property var artwork: ({})
    property real renderScale: 1
    property bool draggable: false
    property bool appDragActive: false
    property real appDragY: 0
    property real cellWidth: 150
    property real cellHeight: 175
    property real columnSpacing: 35
    property real rowSpacing: 32
    property real topPadding: 45
    property real bottomPadding: rowSpacing
    readonly property int count: apps.length
    readonly property int columns: Math.max(1, Math.floor((width + columnSpacing)
                                                        / (cellWidth + columnSpacing)))
    readonly property int rowCount: Math.ceil(count / columns)

    signal clicked(var app)
    signal appDragStarted(var app, real x, real y)
    signal appDragMoved(var app, real x, real y)
    signal appDragFinished(var app, real x, real y)

    clip: true
    contentWidth: width
    contentHeight: Math.max(height, topPadding + rows.implicitHeight + bottomPadding)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: !appDragActive && contentHeight > height

    function itemAt(index) {
        const row = rowRepeater.itemAt(Math.floor(index / columns))
        return row ? row.itemAt(index % columns) : null
    }

    function itemsInRow(row) { return Math.min(columns, count - row * columns) }
    function rowLeft(row) {
        const items = itemsInRow(row)
        return (width - (items * cellWidth + Math.max(0, items - 1) * columnSpacing)) / 2
    }

    // Hit testing follows the actual centered row, including the last partial
    // row, and uses content coordinates after a flick or drag autoscroll.
    function insertionIndex(x, y) {
        if (!count) return 0
        const contentY = y + root.contentY - topPadding
        if (contentY < 0) return 0
        if (contentY >= rows.implicitHeight) return count
        const row = Math.min(rowCount - 1,
            Math.max(0, Math.floor((contentY + rowSpacing / 2) / (cellHeight + rowSpacing))))
        const column = Math.max(0, Math.min(itemsInRow(row),
            Math.floor((x - rowLeft(row) + cellWidth / 2 + columnSpacing)
                       / (cellWidth + columnSpacing))))
        return Math.min(count, row * columns + column)
    }

    function insertionPoint(index) {
        if (!count) return Qt.point((width - cellWidth) / 2, topPadding - contentY)
        index = Math.max(0, Math.min(count, index))
        const row = index === count ? Math.floor((count - 1) / columns)
                                    : Math.floor(index / columns)
        const column = index - row * columns
        return Qt.point(rowLeft(row) + column * (cellWidth + columnSpacing) - columnSpacing / 2,
                        topPadding + row * (cellHeight + rowSpacing) - contentY)
    }

    function constrainScroll() {
        contentX = 0
        contentY = Math.max(0, Math.min(contentY, contentHeight - height))
    }
    onContentHeightChanged: Qt.callLater(constrainScroll)
    onHeightChanged: Qt.callLater(constrainScroll)

    Column {
        id: rows
        width: root.width
        y: root.topPadding
        spacing: root.rowSpacing
        Repeater {
            id: rowRepeater
            model: root.rowCount
            delegate: Row {
                id: row
                required property int index
                readonly property var rowApps: root.apps.slice(index * root.columns,
                                                               (index + 1) * root.columns)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.alignWhenCentered: false
                spacing: root.columnSpacing
                function itemAt(index) { return icons.itemAt(index) }
                Repeater {
                    id: icons
                    model: row.rowApps
                    delegate: DrawerAppIcon {
                        required property string modelData
                        objectName: "drawerApp_" + modelData
                        appId: modelData
                        title: root.artwork[modelData] ? root.artwork[modelData][0] : modelData
                        source: root.artwork[modelData] ? root.artwork[modelData][1] : ""
                        width: root.cellWidth
                        height: root.cellHeight
                        renderScale: root.renderScale
                        draggable: root.draggable
                        scrollable: true
                        onClicked: app => root.clicked(app)
                        onDragStarted: (app, x, y) => root.appDragStarted(app, x, y)
                        onDragMoved: (app, x, y) => root.appDragMoved(app, x, y)
                        onDragFinished: (app, x, y) => root.appDragFinished(app, x, y)
                    }
                }
            }
        }
    }

    // Keep a held app drag usable beyond the first screen of applications.
    Timer {
        interval: 16
        repeat: true
        running: root.appDragActive && root.contentHeight > root.height
                 && root.appDragY >= 0 && root.appDragY <= root.height
        onTriggered: {
            const edge = Math.min(root.cellHeight / 3, root.height / 4)
            const direction = root.appDragY < edge ? -1
                : root.appDragY > root.height - edge ? 1 : 0
            if (direction) {
                root.contentY += direction * root.cellHeight / 20
                root.constrainScroll()
            }
        }
    }
}
