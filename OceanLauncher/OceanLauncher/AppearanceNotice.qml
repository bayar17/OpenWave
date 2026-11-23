import QtQuick

Item {
    id: root

    property string text: ""
    property string fontFamily: ""
    property bool reduceMotion: false
    property int fadeInDuration: 320
    property int holdDuration: 800
    property int fadeOutDuration: 520
    readonly property bool running: sequence.running
    signal shown()
    signal finished()

    function show(name) {
        text = name
        sequence.stop()
        root.opacity = 0
        label.scale = reduceMotion ? 1 : 0.94
        label.yOffset = reduceMotion ? 0 : 10
        sequence.restart()
    }

    function cancel() {
        sequence.stop()
        root.opacity = 0
    }

    opacity: 0
    visible: opacity > 0
    enabled: false

    Text {
        id: label
        objectName: "appearanceNoticeText"
        property real yOffset: 0
        anchors.centerIn: parent
        anchors.verticalCenterOffset: yOffset
        width: Math.min(parent.width - 80, implicitWidth)
        text: root.text
        textFormat: Text.PlainText
        color: "white"
        style: Text.Outline
        styleColor: "#66000000"
        font.family: root.fontFamily
        font.pixelSize: 64
        font.weight: Font.Light
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    SequentialAnimation {
        id: sequence
        ScriptAction { script: root.shown() }
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 0
                to: 1
                duration: root.reduceMotion ? 0 : root.fadeInDuration
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: label
                property: "scale"
                to: 1
                duration: root.reduceMotion ? 0 : root.fadeInDuration
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: label
                property: "yOffset"
                to: 0
                duration: root.reduceMotion ? 0 : root.fadeInDuration
                easing.type: Easing.OutCubic
            }
        }
        PauseAnimation { duration: root.holdDuration }
        NumberAnimation {
            target: root
            property: "opacity"
            to: 0
            duration: root.reduceMotion ? 0 : root.fadeOutDuration
            easing.type: Easing.InCubic
        }
        ScriptAction { script: root.finished() }
    }
}
