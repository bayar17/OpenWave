import QtQuick
import QtQuick.Effects

Item {
    id: card

    required property string appId
    required property string title
    required property url iconSource
    required property url previewUrl
    required property bool revealed
    property Item viewportItem: null
    property real renderScale: 1
    property string fontFamily: "Inter"
    property bool reduceMotion: false
    property bool interactionEnabled: true
    property bool armed: false
    property bool terminating: false
    property bool moved: false
    property real pressX: 0
    property real pressY: 0
    property real dragProgress: 0
    property real dismissalTargetX: 0
    signal activated(string appId, var cardItem)
    signal edgeProximity(real strength)
    signal terminationRequested(string appId, var cardItem)

    objectName: "recentCard_" + appId
    width: 265
    height: 445
    opacity: revealed ? 1 : 0
    scale: revealed ? 1 : 0.76
    transformOrigin: Item.Center
    Behavior on opacity {
        NumberAnimation { duration: card.reduceMotion ? 0 : 240; easing.type: Easing.OutCubic }
    }
    Behavior on scale {
        NumberAnimation { duration: card.reduceMotion ? 0 : 360; easing.type: Easing.OutBack }
    }
    Accessible.role: Accessible.Button
    Accessible.name: "Return to " + title
    Accessible.onPressAction: {
        if (card.interactionEnabled)
            card.activated(card.appId, card)
    }
    Component.onDestruction: card.edgeProximity(0)

    function updateDrag(pointerX) {
        const startX = viewportItem ? mapToItem(viewportItem, 0, 0).x : x
        moved = true
        visual.x = Math.max(-startX, Math.min(0, pointerX - pressX))
        const leftEdge = startX + visual.x
        dragProgress = Math.max(0, Math.min(1, (220 - leftEdge) / 220))
        edgeProximity(dragProgress)
        if (leftEdge <= 18 && !terminating) {
            terminating = true
            dragProgress = 1
            edgeProximity(1)
            dismissalTargetX = -startX - width - 48
            dismissAnimation.start()
        }
    }

    function cancelDismissal() {
        dismissAnimation.stop()
        holdTimer.stop()
        armed = false
        terminating = false
        pulse.stop()
        visual.scale = 1
        dragProgress = 0
        edgeProximity(0)
        returnAnimation.start()
    }

    Timer {
        id: holdTimer
        interval: 3000
        repeat: false
        onTriggered: {
            if (gesture.pressed && !card.moved && card.interactionEnabled) {
                card.armed = true
                pulse.start()
            }
        }
    }

    NumberAnimation {
        id: returnAnimation
        target: visual
        property: "x"
        to: 0
        duration: card.reduceMotion ? 0 : 220
        easing.type: Easing.OutCubic
    }

    SequentialAnimation {
        id: dismissAnimation
        PauseAnimation { duration: card.reduceMotion ? 0 : 120 }
        NumberAnimation {
            target: visual
            property: "x"
            to: card.dismissalTargetX
            duration: card.reduceMotion ? 0 : 340
            easing.type: Easing.InOutCubic
        }
        onFinished: {
            if (card.terminating)
                card.terminationRequested(card.appId, card)
        }
    }

    SequentialAnimation {
        id: pulse
        loops: Animation.Infinite
        NumberAnimation {
            target: visual
            property: "scale"
            from: 1
            to: 1.045
            duration: 390
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            target: visual
            property: "scale"
            from: 1.045
            to: 1
            duration: 390
            easing.type: Easing.InOutSine
        }
    }

    Item {
        id: visual
        objectName: "recentCardVisual"
        width: card.width
        height: card.height
        transformOrigin: Item.Center

        Rectangle {
            anchors.fill: parent
            anchors.margins: -14
            radius: 12
            color: "#ff3048"
            opacity: card.dragProgress * (0.52 + (visual.scale - 1) * 5)
            visible: opacity > 0
            layer.enabled: visible && !card.reduceMotion
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1
                blurMax: 32
            }
        }

        Rectangle {
            anchors.fill: previewFrame
            anchors.leftMargin: 7
            anchors.topMargin: 9
            color: "#55000000"
        }

        Rectangle {
            id: previewFrame
            anchors.fill: parent
            color: "#606060"
            clip: true

            Image {
                id: previewImage
                anchors.fill: parent
                source: card.previewUrl
                sourceSize: Qt.size(265, 445)
                fillMode: Image.PreserveAspectCrop
                smooth: true
                cache: false
                visible: status === Image.Ready
            }

            MultiEffect {
                anchors.fill: parent
                source: previewImage
                visible: previewImage.status === Image.Ready
                blurEnabled: true
                blur: 0.6
                blurMax: 64
                autoPaddingEnabled: false
            }

            Rectangle { anchors.fill: parent; color: "#58000000" }

            IconImage {
                source: card.iconSource
                renderScale: card.renderScale
                width: 130
                height: 130
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 129 / 445
            }

            Text {
                text: card.title
                color: "white"
                font.family: card.fontFamily
                font.weight: Font.Light
                font.pixelSize: 32
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 279 / 445
            }
        }
    }

    MouseArea {
        id: gesture
        anchors.fill: parent
        enabled: card.revealed && card.interactionEnabled && !card.terminating
        preventStealing: card.armed
        onPressed: mouse => {
            returnAnimation.stop()
            card.pressX = mouse.x
            card.pressY = mouse.y
            card.moved = false
            holdTimer.restart()
        }
        onPositionChanged: mouse => {
            if (!card.armed) {
                if (Math.abs(mouse.x - card.pressX) > 12
                    || Math.abs(mouse.y - card.pressY) > 12) {
                    card.moved = true
                    holdTimer.stop()
                }
                return
            }
            card.updateDrag(mouse.x)
        }
        onReleased: mouse => {
            holdTimer.stop()
            if (card.armed && !card.terminating)
                card.cancelDismissal()
            else if (!card.moved && !card.terminating)
                card.activated(card.appId, card)
        }
        onCanceled: {
            if (!card.terminating)
                card.cancelDismissal()
        }
    }
}
