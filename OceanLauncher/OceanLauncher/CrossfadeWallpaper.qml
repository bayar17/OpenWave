import QtQuick

Item {
    id: root

    property url source
    property bool reduceMotion: false
    property int fadeDuration: 900
    property int fillMode: Image.PreserveAspectCrop
    readonly property int status: front.status
    readonly property real progress: front.progress
    readonly property size sourceSize: front.sourceSize
    readonly property bool fading: fade.running
    readonly property url settledSource: settled
    property bool showingB: false
    property url settled
    property url pending
    readonly property Image front: showingB ? imageB : imageA
    readonly property Image back: showingB ? imageA : imageB

    signal crossfadeStarted(url source)
    signal crossfadeFinished(url source)

    function begin() {
        if (fade.running)
            fade.complete()
        if (source.toString() === "")
            return
        if (source === settled) {
            pending = ""
            return
        }
        if (front.source.toString() === "") {
            front.source = source
            settled = source
            return
        }
        pending = source
        back.opacity = 0
        back.source = source
        if (back.status === Image.Ready)
            startFade()
    }

    function startFade() {
        if (fade.running || pending.toString() === "" || back.source !== pending || pending === settled)
            return
        crossfadeStarted(pending)
        fade.target = back
        fade.from = 0
        fade.to = 1
        fade.duration = reduceMotion ? 0 : fadeDuration
        fade.restart()
    }

    function finishFade() {
        const finished = pending
        front.opacity = 0
        showingB = !showingB
        front.opacity = 1
        settled = finished
        pending = ""
        crossfadeFinished(finished)
    }

    Connections {
        target: root
        function onSourceChanged() { root.begin() }
    }

    Component.onCompleted: begin()

    Image {
        id: imageA
        objectName: "wallpaperA"
        anchors.fill: parent
        fillMode: root.fillMode
        asynchronous: true
        smooth: true
        mipmap: true
        z: root.front === imageA ? 0 : 1
        onStatusChanged: if (status === Image.Ready && root.back === imageA) root.startFade()
    }

    Image {
        id: imageB
        objectName: "wallpaperB"
        anchors.fill: parent
        fillMode: root.fillMode
        asynchronous: true
        smooth: true
        mipmap: true
        opacity: 0
        z: root.front === imageB ? 0 : 1
        onStatusChanged: if (status === Image.Ready && root.back === imageB) root.startFade()
    }

    NumberAnimation {
        id: fade
        property: "opacity"
        easing.type: Easing.InOutQuad
        onFinished: root.finishFade()
    }
}
