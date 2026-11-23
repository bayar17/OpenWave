import QtQuick

Item {
    id: backdropLayer
    property Item source

    FrostedBackground {
        anchors.fill: parent
        source: backdropLayer.source
        visible: backdropLayer.source !== null
    }

    Rectangle {
        anchors.fill: parent
        color: "#360e1920"
    }
}
