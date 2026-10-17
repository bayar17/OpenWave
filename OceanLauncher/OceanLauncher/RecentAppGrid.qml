import QtQuick
import QtQuick.Controls

GridView {
    id: grid

    property Item viewportItem: null
    property var appController: null
    property real renderScale: 1
    property string fontFamily: "Inter"
    property bool reduceMotion: false
    property bool interactionEnabled: true
    readonly property real cardWidth: Math.max(1, Math.min(265, cellWidth - 34))
    readonly property real cardHeight: cardWidth * 445 / 265
    signal activated(string appId, var cardItem)
    signal edgeProximity(real strength)
    signal terminationRequested(string appId, var cardItem)

    cellWidth: width / 2
    cellHeight: cardHeight + 32
    flow: GridView.FlowLeftToRight
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    ScrollBar.vertical: ScrollBar { }

    delegate: Item {
        id: cell
        required property string appId
        required property string title
        required property url iconSource
        required property url previewUrl
        required property bool revealed
        width: grid.cellWidth
        height: grid.cellHeight

        RecentAppCard {
            id: card
            anchors.horizontalCenter: parent.horizontalCenter
            width: grid.cardWidth
            height: grid.cardHeight
            appId: cell.appId
            title: cell.title
            iconSource: cell.iconSource
            previewUrl: cell.previewUrl
            revealed: cell.revealed
            viewportItem: grid.viewportItem
            renderScale: grid.renderScale
            fontFamily: grid.fontFamily
            reduceMotion: grid.reduceMotion
            interactionEnabled: grid.interactionEnabled
            onActivated: (appId, cardItem) => grid.activated(appId, cardItem)
            onEdgeProximity: strength => grid.edgeProximity(strength)
            onTerminationRequested: (appId, cardItem) => grid.terminationRequested(appId, cardItem)
            Connections {
                target: grid.appController
                function onTerminationFailed(appId, message) {
                    if (appId === card.appId) card.cancelDismissal()
                }
            }
        }
    }
}
