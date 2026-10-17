import QtQuick

Item {
    id: root
    property Item wallpaper
    property real renderScale: 1
    property string fontFamily: "Inter"
    property bool reduceMotion: false
    property var selectedIcon: null
    property string appId: ""
    property string appTitle: ""
    property url iconSource
    property point origin
    property point titleOrigin
    property bool hadTitle: false
    property real chevronOriginY: 0
    property real chevronOriginBlend: 0
    property real progress: 0
    property real handoffProgress: 0
    property bool handingOff: false
    property bool crossfade: false
    readonly property bool active: selectedIcon !== null || crossfade
    signal ready(string appId)
    signal cancelled()

    visible: active
    opacity: crossfade ? progress : 1
    layer.enabled: crossfade && active

    function start(app, chevron) {
        if (active)
            return
        origin = app.imageItem.mapToItem(root, 0, 0)
        hadTitle = app.titleItem !== null
        titleOrigin = hadTitle
            ? app.titleItem.mapToItem(root, app.titleItem.width / 2, 0)
            : Qt.point(origin.x + 65, origin.y + 138)
        chevronOriginY = chevron.mapToItem(root, chevron.width / 2,
                                         chevron.height / 2).y - 8
        chevronOriginBlend = chevron.rotation / 180
        appId = app.appId
        appTitle = app.title
        iconSource = app.source
        progress = 0
        handoffProgress = 0
        selectedIcon = app
        app.launching = true
        transition.start()
    }

    function startCrossfade(targetAppId, title, source) {
        if (active) return
        appId = targetAppId
        appTitle = title
        iconSource = source
        origin = Qt.point((width - 130) / 2, 430)
        titleOrigin = Qt.point(width / 2, 578)
        chevronOriginY = 27
        chevronOriginBlend = 1
        hadTitle = true
        progress = 0
        handoffProgress = 0
        handingOff = false
        crossfade = true
        transition.start()
    }

    function reset() {
        transition.stop()
        handoffAnimation.stop()
        if (selectedIcon)
            selectedIcon.launching = false
        selectedIcon = null
        crossfade = false
        progress = 0
        appId = ""
        handingOff = false
        handoffProgress = 0
    }

    function beginHandoff() {
        if (!active || handingOff)
            return
        handingOff = true
        handoffAnimation.start()
    }

    NumberAnimation {
        id: transition
        target: root
        property: "progress"
        from: 0
        to: 1
        duration: root.reduceMotion ? 0 : (root.crossfade ? 320 : 600)
        easing.type: Easing.InOutCubic
        onFinished: root.ready(root.appId)
    }

    NumberAnimation {
        id: handoffAnimation
        target: root
        property: "handoffProgress"
        from: 0
        to: 1
        duration: root.reduceMotion ? 0 : 420
        easing.type: Easing.InOutCubic
        onFinished: root.reset()
    }

    // Capture only the wallpaper: status indicators and the moving icon stay crisp.
    FrostedBackground {
        anchors.fill: parent
        source: root.wallpaper
        opacity: root.crossfade ? 1 : root.progress
    }

    // Consume taps behind the loading view, including repeated app taps.
    MouseArea { anchors.fill: parent }

    IconImage {
        objectName: "loadingIcon"
        opacity: root.crossfade ? 1 : 1 - root.handoffProgress
        source: root.iconSource
        renderScale: root.renderScale
        width: 130
        height: 130
        x: root.origin.x + ((root.width - width) / 2 - root.origin.x) * root.progress
        y: root.origin.y + (430 - root.origin.y) * root.progress
    }

    Text {
        objectName: "loadingTitle"
        text: root.appTitle
        color: "white"
        font.family: root.fontFamily
        font.weight: Font.Light
        font.pixelSize: root.crossfade ? 34 : 30 + 4 * root.progress
        x: root.titleOrigin.x + (root.width / 2 - root.titleOrigin.x) * root.progress - width / 2
        y: root.titleOrigin.y + (578 - root.titleOrigin.y) * root.progress
        opacity: root.crossfade ? 1 : (root.hadTitle ? 1 : root.progress) * (1 - root.handoffProgress)
    }

    Item {
        id: spinner
        objectName: "loadingSpinner"
        width: 80
        height: 80
        anchors.horizontalCenter: parent.horizontalCenter
        y: 711
        opacity: root.crossfade ? 1 : Math.max(0, (root.progress - 0.65) / 0.35)
                 * (1 - root.handoffProgress)
        readonly property bool spinning: root.active && opacity > 0 && !root.reduceMotion
        Canvas {
            width: Math.ceil(parent.width * root.renderScale * 2)
            height: width
            scale: parent.width / width
            transformOrigin: Item.TopLeft
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.scale(width / 80, height / 80)
                ctx.lineWidth = 12
                ctx.strokeStyle = "#80817f"
                ctx.beginPath()
                ctx.arc(40, 40, 34, 0, Math.PI * 2)
                ctx.stroke()
                ctx.lineCap = "round"
                for (let i = 0; i < 64; ++i) {
                    const shade = Math.round(128 * (1 - i / 63))
                    ctx.strokeStyle = Qt.rgba(shade / 255, shade / 255, shade / 255, 1)
                    ctx.beginPath()
                    ctx.arc(40, 40, 34, (-120 + i * 1.5) * Math.PI / 180,
                            (-118.5 + i * 1.5) * Math.PI / 180)
                    ctx.stroke()
                }
            }
        }
        NumberAnimation on rotation {
            from: 0
            to: 360
            duration: 1000
            loops: Animation.Infinite
            running: spinner.spinning
        }
    }

    Item {
        width: 180
        height: 70
        opacity: root.crossfade ? 1 : 1 - root.handoffProgress
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.chevronOriginY + (27 - root.chevronOriginY) * root.progress - 27
        objectName: "loadingBackControl"
        Accessible.role: Accessible.Button
        Accessible.name: "Cancel app loading"
        Accessible.onPressAction: root.cancelled()
        Item {
            anchors.centerIn: parent
            width: 28
            height: 16
            readonly property real blend: root.chevronOriginBlend
                + (1 - root.chevronOriginBlend) * root.progress
            rotation: 180 * blend
            Chevron { color: "#202020"; opacity: 1 - parent.blend }
            Chevron { opacity: parent.blend }
        }
        TapHandler { onTapped: root.cancelled() }
    }
}
