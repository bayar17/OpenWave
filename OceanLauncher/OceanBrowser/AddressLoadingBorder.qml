import QtQuick

Item {
    id: border
    property bool loading: false
    property var navigationSession: null
    property bool windowPresented: true
    property bool reducedMotion: false
    property real cornerRadius: 32
    property real renderScale: 1
    property real reveal: 0
    property real phase: 0
    property bool componentReady: false
    readonly property bool rotating: spin.running
    enabled: false

    function updateRotation() {
        if (!componentReady) return
        const animate = windowPresented && visible && !reducedMotion && (loading || reveal > 0)
        if (animate && !spin.running) spin.start()
        else if (!animate && spin.running) spin.stop()
    }
    function synchronize(reset) {
        if (!componentReady) return
        fade.stop()
        if (reset || !windowPresented || !visible || reducedMotion) {
            spin.stop()
            phase = 0
            reveal = 0
        }
        if (!windowPresented || !visible) return
        if (reducedMotion) reveal = loading ? 1 : 0
        else if (reveal !== (loading ? 1 : 0)) {
            fade.to = loading ? 1 : 0
            fade.duration = loading ? 180 : 600
            fade.start()
        }
        updateRotation()
    }
    onLoadingChanged: synchronize(false)
    onNavigationSessionChanged: synchronize(true)
    onWindowPresentedChanged: synchronize(false)
    onReducedMotionChanged: synchronize(false)
    onVisibleChanged: synchronize(false)
    onRevealChanged: updateRotation()
    Component.onCompleted: { componentReady = true; synchronize(true) }

    NumberAnimation {
        id: fade
        target: border; property: "reveal"
        easing.type: Easing.Linear
        onFinished: border.updateRotation()
    }
    NumberAnimation {
        id: spin
        target: border; property: "phase"
        from: 0; to: 360; duration: 2200
        loops: Animation.Infinite; easing.type: Easing.Linear
    }
    ShaderEffect {
        anchors.fill: parent
        opacity: border.reveal
        visible: opacity > 0
        // The signed-distance outline stays fixed; only the color angle changes.
        readonly property size barSize: Qt.size(width, height)
        readonly property real cornerRadius: border.cornerRadius
        readonly property real borderWidth: 2 / Math.max(0.01, border.renderScale)
        readonly property real phase: border.phase
        readonly property real pixelSoftness: 1 / Math.max(0.01, border.renderScale)
        fragmentShader: "qrc:/OceanBrowser/shaders/address-loading.frag.qsb"
    }
}
