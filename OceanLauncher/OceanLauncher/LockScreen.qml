import QtQuick
import QtQuick.Effects
import OpenWave.Shell 1.0

FocusScope {
    id: root

    property var controller: null
    property Item wallpaper
    property string timeText: ""
    property string dateText: ""
    property string regularFont: ""
    property string lightFont: ""
    property url batteryIcon: ""
    property url signalAssetBase: "qrc:/OceanTopbard/assets/TopBar/"
    property bool cellularConnected: false
    property int cellularStrength: 0
    property bool wifiConnected: false
    property int wifiStrength: 0
    property bool reduceMotion: false
    property real safeTop: 70
    property real safeBottom: 0
    property real safeLeft: 0
    property real safeRight: 0
    property real dragOffset: 0
    readonly property real revealDistance: Math.max(1, height * 0.82)
    property real unlockReleaseVelocityY: 0
    property bool unlockPending: false
    property bool unlocking: false
    property bool gestureEnabled: false
    property bool introStarted: false
    readonly property real revealProgress: tileWallpaper.revealProgress
    readonly property bool revealActive: visible && revealProgress > 0

    signal unlockFlowRequested()

    visible: controller ? controller.locked : true
    enabled: visible
    clip: true

    Image {
        objectName: "lockWallpaperBase"
        anchors.fill: parent
        source: root.wallpaper ? root.wallpaper.source : ""
        fillMode: root.wallpaper ? root.wallpaper.fillMode : Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: root.unlocking ? 1 - root.revealProgress : 1
    }

    TileWallpaperItem {
        id: tileWallpaper
        objectName: "tileWallpaper"
        anchors.fill: parent
        source: root.wallpaper ? root.wallpaper.source : ""
        separation: 0
        active: root.visible && !root.unlocking && !root.unlockPending && !root.reduceMotion && separation >= 0.999
        reduceMotion: root.reduceMotion
        opacity: root.reduceMotion && root.unlocking ? 1 - revealProgress : 1
        z: 1
        onTileCountChanged: if (tileCount > 0 && root.visible) root.startLockIntro()
    }

    Item {
        id: lockShading
        objectName: "lockShading"
        anchors.fill: parent
        y: root.dragOffset
        z: 2
        opacity: 1 - Math.max(0, Math.min(1, (root.revealProgress - 0.067) / 0.71))

        Rectangle {
            objectName: "lockTopGradient"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.height * 0.46
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#42000000" }
                GradientStop { position: 0.18; color: "#38000000" }
                GradientStop { position: 0.48; color: "#1f000000" }
                GradientStop { position: 0.76; color: "#0a000000" }
                GradientStop { position: 1.0; color: "#00000000" }
            }
        }

        Rectangle {
            objectName: "lockBottomGradient"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.height * 0.32
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.28; color: "#08000000" }
                GradientStop { position: 0.58; color: "#1f000000" }
                GradientStop { position: 1.0; color: "#47000000" }
            }
        }
    }

    Item {
        id: lockContent
        anchors.fill: parent
        z: 3
        y: root.dragOffset
        opacity: 1 - Math.max(0, Math.min(1, (root.revealProgress - 0.067) / 0.56))

        Text {
            objectName: "lockClock"
            x: root.safeLeft + root.width * 0.075
            y: root.safeTop + root.height * 0.09
            width: root.width - x - root.safeRight - 20
            height: Math.min(root.height * 0.11, root.width * 0.22)
            text: root.timeText
            color: "white"
            font.family: root.regularFont
            font.pixelSize: Math.min(root.height * 0.095, root.width * 0.17)
            font.weight: Font.Light
            minimumPixelSize: 30
            fontSizeMode: Text.Fit
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Text {
            objectName: "lockDate"
            x: root.safeLeft + root.width * 0.085
            y: root.safeTop + root.height * 0.20
            width: root.width - x - root.safeRight - 24
            height: Math.min(58, root.height * 0.052)
            text: root.dateText
            color: "white"
            font.family: root.regularFont
            font.pixelSize: Math.min(root.height * 0.034, root.width * 0.055)
            font.weight: Font.Normal
            minimumPixelSize: 18
            fontSizeMode: Text.Fit
            elide: Text.ElideRight
        }

        Row {
            objectName: "lockStatusIndicators"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: root.safeLeft + 22
            anchors.right: parent.right
            anchors.rightMargin: root.safeRight + 18
            height: root.safeTop
            spacing: 12

            Image {
                source: root.batteryIcon
                width: 54
                height: 30
                anchors.verticalCenter: parent.verticalCenter
                fillMode: Image.PreserveAspectFit
                layer.enabled: true
                layer.effect: MultiEffect {
                    colorization: 1
                    colorizationColor: "white"
                }
            }

            Item { width: Math.max(0, parent.width - 54 - 82 - 45 - 36); height: 1 }

            SignalIcon {
                assetBase: root.signalAssetBase
                kind: "cellular"
                connected: root.cellularConnected
                strength: root.cellularStrength
                width: 82
                height: 20
                anchors.verticalCenter: parent.verticalCenter
            }

            SignalIcon {
                assetBase: root.signalAssetBase
                kind: "wifi"
                connected: root.wifiConnected
                strength: root.wifiStrength
                width: 45
                height: 34
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Chevron {
            objectName: "lockTopChevron"
            anchors.horizontalCenter: parent.horizontalCenter
            y: (root.safeTop - height) / 2
            color: "white"
            rotation: 180
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.safeBottom + 28
            spacing: 18

            Chevron {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "white"
                rotation: 180
            }

            Text {
                objectName: "developmentPreviewLabel"
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Lock Screen UI / Development Preview"
                color: "#e8ffffff"
                font.family: root.lightFont
                font.pixelSize: Math.max(13, Math.min(18, root.width * 0.023))
            }
        }
    }

    MouseArea {
        z: 4
        anchors.fill: parent
        enabled: false
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => mouse.accepted = true
        onClicked: mouse => mouse.accepted = true
    }

    MultiPointTouchArea {
        id: multiTouch
        objectName: "lockMultiTouch"
        z: 5
        anchors.fill: parent
        enabled: root.visible && !root.unlocking && !root.unlockPending && !root.reduceMotion
        mouseEnabled: false
        minimumTouchPoints: 2
        maximumTouchPoints: 5
        onTouchUpdated: {
            for (let i = 0; i < touchPoints.length; ++i) {
                const point = touchPoints[i]
                tileWallpaper.applyGesture(point.x, point.y,
                    (point.x - point.previousX) * 60,
                    (point.y - point.previousY) * 60)
            }
        }
    }

    DragHandler {
        id: swipeHandler
        target: null
        enabled: root.visible && !root.unlocking && !root.unlockPending
        xAxis.enabled: true
        yAxis.enabled: true

        onActiveChanged: {
            if (active) {
                root.gestureEnabled = centroid.pressPosition.y >= root.height * 0.68
                if (root.gestureEnabled) {
                    root.dragOffset = 0
                    root.unlockReleaseVelocityY = 0
                    restoreAnimation.stop()
                    tileWallpaper.beginReveal()
                    root.dragOffset = Math.max(-root.revealDistance,
                                                Math.min(0, centroid.position.y - centroid.pressPosition.y))
                    tileWallpaper.revealProgress = Math.max(0, Math.min(1, -root.dragOffset / root.revealDistance))
                }
                else tileWallpaper.applyGesture(centroid.position.x, centroid.position.y, 0, 0)
            } else if (root.gestureEnabled && !root.unlocking) {
                const distance = Math.max(0, -root.dragOffset)
                const fastEnough = centroid.velocity.y < -1200 && distance > root.height * 0.08
                const farEnough = distance > root.height * 0.18
                root.unlockReleaseVelocityY = centroid.velocity.y
                if (farEnough || fastEnough)
                    root.requestUnlock()
                else
                    root.restorePosition()
                root.gestureEnabled = false
            }
        }

        onTranslationChanged: {
            if (!active)
                return
            if (root.gestureEnabled) {
                root.dragOffset = Math.max(-root.revealDistance, Math.min(0, translation.y))
                tileWallpaper.revealProgress = Math.max(0, Math.min(1, -root.dragOffset / root.revealDistance))
            } else {
                tileWallpaper.applyGesture(centroid.position.x, centroid.position.y,
                    centroid.velocity.x, centroid.velocity.y)
            }
        }
    }

    onVisibleChanged: {
        if (visible && tileWallpaper.tileCount > 0)
            startLockIntro()
        else if (!visible) {
            lockIntro.stop()
            introStarted = false
        }
    }

    onReduceMotionChanged: {
        if (!reduceMotion && visible && tileWallpaper.tileCount > 0)
            startLockIntro()
    }

    function startLockIntro() {
        if (!visible || unlocking || unlockPending || tileWallpaper.tileCount === 0 || reduceMotion || introStarted)
            return
        introStarted = true
        tileWallpaper.beginSession()
        tileWallpaper.separation = 0
        lockIntro.restart()
    }

    NumberAnimation {
        id: lockIntro
        target: tileWallpaper
        property: "separation"
        from: 0
        to: 1
        duration: root.reduceMotion ? 80 : 520
        easing.type: Easing.OutCubic
    }

    function requestUnlock() {
        if (!controller || unlocking || unlockPending)
            return
        unlockPending = true
        if (!controller.requestDevelopmentSwipeUnlock()) {
            unlockPending = false
            restorePosition()
            return
        }
        unlockFlowRequested()
        if (controller.unlockAuthorized)
            beginPuzzleReveal()
    }

    function beginPuzzleReveal() {
        if (!unlockPending || unlocking || !controller || !controller.unlockAuthorized)
            return
        unlockPending = false
        unlocking = true
        lockIntro.stop()
        tileWallpaper.beginReveal()
        revealAnimation.from = tileWallpaper.revealProgress
        const remaining = 1 - revealAnimation.from
        const velocityBoost = Math.min(60, Math.max(0, -root.unlockReleaseVelocityY / 30))
        revealAnimation.duration = Math.max(180, Math.min(300, 180 + remaining * 120 - velocityBoost))
        revealAnimation.start()
    }

    function cancelPuzzleReveal() {
        if (unlockPending) {
            unlockPending = false
            restorePosition()
        }
        if (!unlocking) {
            if (tileWallpaper.revealProgress > 0)
                restorePosition()
            return
        }
        revealAnimation.stop()
        revealCancelAnimation.from = tileWallpaper.revealProgress
        revealCancelAnimation.to = 0
        revealCancelAnimation.start()
    }

    Component.onCompleted: {
        if (visible && tileWallpaper.tileCount > 0)
            startLockIntro()
    }

    function restorePosition() {
        restoreAnimation.stop()
        revealAnimation.stop()
        dragReturn.from = dragOffset
        dragReturn.to = 0
        revealReturnAnimation.from = tileWallpaper.revealProgress
        revealReturnAnimation.to = 0
        restoreAnimation.start()
    }

    function finishUnlock() {
        if (!controller || !controller.finishDevelopmentSwipeUnlock()) {
            if (controller)
                controller.cancelUnlockRequest()
            cancelPuzzleReveal()
            return
        }
        unlockPending = false
        dragOffset = 0
        unlocking = false
    }

    ParallelAnimation {
        id: restoreAnimation
        NumberAnimation {
            id: dragReturn
            target: root
            property: "dragOffset"
            duration: root.reduceMotion ? 0 : 220
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: revealReturnAnimation
            target: tileWallpaper
            property: "revealProgress"
            duration: root.reduceMotion ? 0 : 220
            easing.type: Easing.OutCubic
        }
        onFinished: tileWallpaper.cancelReveal()
    }

    NumberAnimation {
        id: revealAnimation
        target: tileWallpaper
        property: "revealProgress"
        to: 1
        duration: root.reduceMotion ? 180 : 240
        easing.type: Easing.OutCubic
        onFinished: root.finishUnlock()
    }

    NumberAnimation {
        id: revealCancelAnimation
        target: tileWallpaper
        property: "revealProgress"
        duration: root.reduceMotion ? 120 : 240
        easing.type: Easing.OutCubic
        onFinished: {
            tileWallpaper.cancelReveal()
            root.unlocking = false
            root.restorePosition()
        }
    }

    Connections {
        target: root.controller
        function onStateChanged() {
            if (!root.controller)
                return
            if (root.controller.unlockAuthorized && root.unlockPending)
                root.beginPuzzleReveal()
            else if (root.controller.locked && !root.controller.unlockRequestActive)
                root.cancelPuzzleReveal()
        }
    }
}
