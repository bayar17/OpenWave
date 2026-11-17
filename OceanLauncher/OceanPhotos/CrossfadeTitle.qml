import QtQuick

Item {
    id: root
    property string text: ""
    property font font
    property color color: "white"
    property bool animate: true
    property string displayedText: ""
    property string outgoingText: ""
    property real progress: 1
    readonly property bool transitioning: fade.running
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    onTextChanged: {
        if (text === displayedText) return
        const previous = progress < 0.5 ? outgoingText : displayedText
        fade.stop()
        outgoingText = previous
        displayedText = text
        progress = animate && visible && previous.length > 0 ? 0 : 1
        if (progress === 0) fade.start()
    }
    onAnimateChanged: if (!animate) { fade.stop(); progress = 1 }
    onVisibleChanged: if (!visible) { fade.stop(); progress = 1 }
    Text {
        objectName: "outgoingTitle"
        anchors.fill: parent
        text: root.outgoingText; font: root.font; color: root.color
        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
        opacity: 1 - root.progress
        Accessible.ignored: true
    }
    Text {
        objectName: "currentTitle"
        anchors.fill: parent
        text: root.displayedText; font: root.font; color: root.color
        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
        opacity: root.progress
        Accessible.ignored: true
    }
    NumberAnimation { id: fade; target: root; property: "progress"; to: 1; duration: 220; easing.type: Easing.InOutQuad }
}
