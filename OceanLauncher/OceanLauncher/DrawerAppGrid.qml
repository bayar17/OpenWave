import QtQuick

Flickable {
    id: root

    property var apps: []
    property var artwork: ({})
    property real renderScale: 1
    property bool reduceMotion: false
    property bool draggable: false
    property bool qaDiagnostics: false
    property bool qaTargetReady: false
    property bool appDragActive: false
    property string movableAppId: ""
    property real appDragY: 0
    property int favoriteCount: 0
    property real revealProgress: 1
    property real iconSize: 115
    property real labelHeight: 40
    property real cellWidth: iconSize
    property real cellHeight: iconSize + 8 + labelHeight
    property real columnSpacing: 35
    property real rowSpacing: 46
    property real topPadding: 51
    property real bottomPadding: rowSpacing
    readonly property real iconReveal: Math.max(0, Math.min(1, (revealProgress - 0.05) / 0.5))
    readonly property real labelReveal: Math.max(0, Math.min(1, (revealProgress - 0.2) / 0.6))
    readonly property int count: apps.length
    readonly property int columns: Math.max(1, Math.floor((width + columnSpacing)
                                                        / (cellWidth + columnSpacing)))
    readonly property int rowCount: Math.ceil(count / columns)
    readonly property real gridLeft: Math.max(0, (width - columns * cellWidth
                                                  - (columns - 1) * columnSpacing) / 2)
    readonly property int dockCount: Math.min(favoriteCount, columns)

    onCountChanged: if (qaDiagnostics)
        console.info("OCEAN_QA QML drawer-model count=" + count + " rows=" + rowCount)

    signal clicked(var app)
    signal appLongPressed(var app)
    signal appDragStarted(var app, real x, real y)
    signal appDragMoved(var app, real x, real y)
    signal appDragFinished(var app, real x, real y)

    clip: true
    contentWidth: width
    contentHeight: Math.max(height, topPadding + rows.implicitHeight + bottomPadding)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: !appDragActive && contentHeight > height && revealProgress > 0.95

    function scrollToTop() {
        returnAnimation.stop()
        if (contentY <= 0)
            return
        if (reduceMotion) {
            contentY = 0
            return
        }
        returnAnimation.from = contentY
        returnAnimation.start()
    }

    function dropTarget(index, appId) {
        const current = apps.indexOf(appId)
        if (current >= 0 && (index === current || index === current + 1))
            return current < favoriteCount
                ? {favorites: true, index: current}
                : {favorites: false, index: current - favoriteCount}
        if (index < favoriteCount || (index === favoriteCount && favoriteCount < columns))
            return {favorites: true, index: Math.min(index, favoriteCount)}
        return {favorites: false, index: index - favoriteCount}
    }

    function itemAt(index) {
        const row = rowRepeater.itemAt(Math.floor(index / columns))
        return row ? row.itemAt(index % columns) : null
    }

    function itemsInRow(row) { return Math.min(columns, count - row * columns) }
    function rowLeft(row) { return gridLeft }

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

    NumberAnimation {
        id: returnAnimation
        target: root
        property: "contentY"
        to: 0
        duration: 220
        easing.type: Easing.OutCubic
    }

    Column {
        id: rows
        width: root.width
        y: root.topPadding
        spacing: root.rowSpacing
        move: Transition {
            NumberAnimation {
                properties: "x,y"
                duration: root.reduceMotion ? 0 : 180
                easing.type: Easing.OutCubic
            }
        }
        Repeater {
            id: rowRepeater
            model: root.rowCount
            onItemAdded: (index, item) => {
                if (root.qaDiagnostics)
                    console.info("OCEAN_QA QML drawer-row-created index=" + index)
            }
            delegate: Row {
                id: row
                required property int index
                readonly property var rowApps: root.apps.slice(index * root.columns,
                                                               (index + 1) * root.columns)
                x: root.gridLeft
                spacing: root.columnSpacing
                move: Transition {
                    NumberAnimation {
                        properties: "x,y"
                        duration: root.reduceMotion ? 0 : 180
                        easing.type: Easing.OutCubic
                    }
                }
                function itemAt(index) { return icons.itemAt(index) }
                Repeater {
                    id: icons
                    model: row.rowApps
                    onItemAdded: (index, item) => {
                        if (root.qaDiagnostics)
                            console.info("OCEAN_QA QML drawer-delegate-created appId=" + item.appId)
                    }
                    delegate: DrawerAppIcon {
                        required property string modelData
                        required property int index
                        objectName: "drawerApp_" + modelData
                        appId: modelData
                        title: root.artwork[modelData] ? root.artwork[modelData][0] : modelData
                        source: root.artwork[modelData] ? root.artwork[modelData][1] : ""
                        width: root.cellWidth
                        height: root.cellHeight
                        iconSize: root.iconSize
                        labelWidth: root.cellWidth + root.columnSpacing
                        labelOpacity: root.labelReveal
                        reveal: row.index * root.columns + index < root.dockCount ? 1 : root.iconReveal
                        renderScale: root.renderScale
                        qaDiagnostics: root.qaDiagnostics
                        qaTargetReady: root.qaTargetReady
                        draggable: root.draggable
                        scrollable: true
                        movable: root.movableAppId === modelData
                        onClicked: app => root.clicked(app)
                        onLongPressed: app => root.appLongPressed(app)
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
