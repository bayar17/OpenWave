import QtQuick

Item {
    id: root
    property var photo: ({})
    property var previousPhoto: ({})
    property var nextPhoto: ({})
    property bool active: false
    property bool canNavigate: false
    property bool reducedMotion: false
    property bool captureMode: false
    property real zoom: 1
    property real offset: 0
    property int pendingDirection: 0
    readonly property bool settling: settle.running
    signal stepRequested(int direction)
    signal zoomRequested(real value)
    clip: true

    function reset() {
        settle.stop(); pendingDirection = 0; offset = 0
        canvas.cancelFlick(); canvas.contentX = 0; canvas.contentY = 0
    }
    function releaseSwipe(cancelled) {
        if (!active || zoom > 1 || cancelled) { reset(); return }
        const threshold = Math.min(80, width * 0.15)
        pendingDirection = canNavigate && Math.abs(offset) >= threshold ? (offset < 0 ? 1 : -1) : 0
        settle.to = pendingDirection ? -pendingDirection * width : 0
        settle.start()
    }
    onPhotoChanged: reset()
    onActiveChanged: if (!active) reset()
    onZoomChanged: if (zoom > 1) { settle.stop(); pendingDirection = 0; offset = 0 }
    NumberAnimation {
        id: settle
        target: root; property: "offset"
        duration: root.captureMode || root.reducedMotion ? 0 : 220
        easing.type: Easing.OutCubic
        onFinished: {
            const direction = root.pendingDirection
            root.pendingDirection = 0; root.offset = 0
            if (direction) root.stepRequested(direction)
        }
    }
    Item {
        x: root.reducedMotion ? 0 : root.offset
        width: root.width; height: root.height
        Image {
            x: -root.width; width: root.width; height: root.height
            source: root.active && root.canNavigate ? root.previousPhoto.url || "" : ""
            sourceSize: Qt.size(2560, 2560); autoTransform: true
            fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
        }
        Image {
            x: root.width; width: root.width; height: root.height
            source: root.active && root.canNavigate ? root.nextPhoto.url || "" : ""
            sourceSize: Qt.size(2560, 2560); autoTransform: true
            fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
        }
        Flickable {
            id: canvas
            objectName: "galleryPhotoCanvas"
            anchors.fill: parent
            contentWidth: width * root.zoom; contentHeight: height * root.zoom
            interactive: root.zoom > 1; clip: true; boundsBehavior: Flickable.StopAtBounds
            Image {
                id: fullImage
                width: canvas.contentWidth; height: canvas.contentHeight
                source: root.active && !root.photo.animated ? root.photo.url || "" : ""
                sourceSize: Qt.size(2560, 2560); autoTransform: true
                fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
                retainWhileLoading: true
            }
            AnimatedImage {
                anchors.fill: fullImage
                source: root.active && root.photo.animated ? root.photo.url || "" : ""
                fillMode: Image.PreserveAspectFit; asynchronous: true
                playing: root.active && root.visible
            }
            Text {
                anchors.centerIn: parent; width: parent.width - 80
                visible: fullImage.status === Image.Error
                text: qsTr("This image could not be decoded."); color: "white"; font.pixelSize: 30
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            }
        }
    }
    PinchHandler {
        id: pinch
        objectName: "galleryPinchHandler"
        target: null; enabled: root.active && !root.settling
        property real startingZoom: 1
        onActiveChanged: if (active) { root.reset(); startingZoom = root.zoom }
        onActiveScaleChanged: root.zoomRequested(Math.max(1, Math.min(4, startingZoom * activeScale)))
    }
    TapHandler {
        enabled: root.active && !root.settling
        onDoubleTapped: root.zoomRequested(root.zoom > 1 ? 1 : 2)
    }
    DragHandler {
        id: swipe
        objectName: "gallerySwipeHandler"
        target: null
        enabled: root.active && root.canNavigate && root.zoom === 1 && !root.settling
        acceptedButtons: Qt.LeftButton
        minimumPointCount: 1; maximumPointCount: 1
        property bool cancelled: false
        onActiveTranslationChanged: if (active) {
            root.offset = Math.abs(activeTranslation.x) > Math.abs(activeTranslation.y) * 1.2
                ? Math.max(-root.width, Math.min(root.width, activeTranslation.x)) : 0
        }
        onCanceled: { cancelled = true; root.reset() }
        onActiveChanged: {
            if (active) { cancelled = false; root.offset = 0 }
            else Qt.callLater(() => {
                if (!swipe.active) root.releaseSwipe(swipe.cancelled || pinch.active || root.zoom > 1)
            })
        }
    }
}
