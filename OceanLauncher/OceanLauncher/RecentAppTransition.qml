import QtQuick
import QtQuick.Effects

Item {
    id: root

    property string appId: ""
    property url previewUrl
    property rect originRect: Qt.rect(0, 0, 265, 445)
    property real expansion: 0
    property real liveOpacity: 0
    property bool active: false
    property bool handingOff: false
    property bool reduceMotion: false
    signal expanded()
    signal finished()

    visible: active

    function start(targetAppId, url, rect) {
        expandAnimation.stop()
        handoffAnimation.stop()
        appId = targetAppId
        previewUrl = url
        originRect = rect
        expansion = 0
        liveOpacity = 0
        handingOff = false
        active = true
        expandAnimation.start()
    }

    function beginHandoff() {
        if (!active || handingOff)
            return
        handingOff = true
        handoffAnimation.start()
    }

    function reset() {
        expandAnimation.stop()
        handoffAnimation.stop()
        active = false
        handingOff = false
        expansion = 0
        liveOpacity = 0
        previewUrl = ""
        appId = ""
    }

    NumberAnimation {
        id: expandAnimation
        target: root
        property: "expansion"
        from: 0
        to: 1
        duration: root.reduceMotion ? 0 : 460
        easing.type: Easing.InOutCubic
        onFinished: root.expanded()
    }

    NumberAnimation {
        id: handoffAnimation
        target: root
        property: "liveOpacity"
        from: 0
        to: 1
        duration: root.reduceMotion ? 0 : 380
        easing.type: Easing.InOutCubic
        onFinished: root.finished()
    }

    // The saved full-resolution app frame expands before the live surface fades in.
    Item {
        id: preview
        x: root.originRect.x * (1 - root.expansion)
        y: root.originRect.y * (1 - root.expansion)
        width: root.originRect.width + (root.width - root.originRect.width) * root.expansion
        height: root.originRect.height + (root.height - root.originRect.height) * root.expansion
        opacity: 1 - root.liveOpacity
        clip: true

        Rectangle { anchors.fill: parent; color: "#454e53" }

        Image {
            id: snapshot
            anchors.fill: parent
            source: root.previewUrl
            sourceSize: Qt.size(768, 1366)
            fillMode: Image.PreserveAspectCrop
            smooth: true
            visible: status === Image.Ready
        }

        MultiEffect {
            anchors.fill: parent
            source: snapshot
            visible: snapshot.status === Image.Ready
            blurEnabled: true
            blur: 0.6
            blurMax: 64
            autoPaddingEnabled: false
        }

        Rectangle { anchors.fill: parent; color: "#58000000" }
    }

    // Block input while the snapshot grows and while it crossfades to the app.
    MouseArea { anchors.fill: parent }
}
