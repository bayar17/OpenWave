import QtQuick

// Shared motion only: callers retain their layout, input shield and lifecycle.
QtObject {
    id: motion
    property bool reduceMotion: false
    property int openDuration: 220
    property int closeDuration: 160
    property int reducedDuration: 80
    property real closedScale: 0.02
    property bool fadeEnabled: true
    property bool proportionalClose: false
    property real progress: closedScale
    property real alpha: 0
    readonly property real verticalScale: reduceMotion ? 1 : progress
    property int closingDuration: closeDuration
    signal opened()
    signal closed()

    function open() {
        opening.stop()
        closing.stop()
        progress = closedScale
        alpha = fadeEnabled ? 0 : 1
        opening.start()
    }
    function close() {
        opening.stop()
        closing.stop()
        closingDuration = proportionalClose ? Math.max(1, Math.round(closeDuration * progress)) : closeDuration
        closing.start()
    }
    onReduceMotionChanged: {
        // Switch to opacity-only motion, including when the preference changes
        // during an animation. Continue from the current alpha without flashing.
        if (opening && opening.running) { opening.stop(); opening.start() }
        else if (closing && closing.running) { closing.stop(); closing.start() }
    }
    property Animation opening: ParallelAnimation {
        NumberAnimation {
            target: motion; property: "progress"; to: 1
            duration: motion.reduceMotion ? 0 : motion.openDuration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: motion; property: "alpha"; to: 1
            duration: !motion.fadeEnabled ? 0 : motion.reduceMotion ? motion.reducedDuration : motion.openDuration
            easing.type: Easing.OutCubic
        }
        onFinished: motion.opened()
    }
    property Animation closing: ParallelAnimation {
        NumberAnimation {
            target: motion; property: "progress"; to: motion.closedScale
            duration: motion.reduceMotion ? 0 : motion.closingDuration
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: motion; property: "alpha"; to: motion.fadeEnabled ? 0 : 1
            duration: !motion.fadeEnabled ? 0 : motion.reduceMotion ? motion.reducedDuration : motion.closingDuration
            easing.type: Easing.InCubic
        }
        onFinished: motion.closed()
    }
}
