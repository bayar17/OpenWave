import QtQuick

Item {
    id: root

    property var preset: ({})
    property bool compact: false
    readonly property bool imageReady: previewImage.status === Image.Ready
    readonly property color topColor: preset.topColor !== undefined ? preset.topColor : "#202830"
    readonly property color bottomColor: preset.bottomColor !== undefined ? preset.bottomColor : "#3a6ea5"
    readonly property color scrimColor: preset.scrim !== undefined ? preset.scrim : "#b3101418"
    readonly property color activeColor: preset.activeColor !== undefined ? preset.activeColor : "#3a6ea5"
    readonly property color inactiveColor: preset.inactiveColor !== undefined ? preset.inactiveColor : "#998a8d94"
    readonly property color trackColor: preset.sliderTrack !== undefined ? preset.sliderTrack : "#59ffffff"
    readonly property color fillColor: preset.sliderFill !== undefined ? preset.sliderFill : "white"
    readonly property color handleColor: preset.sliderHandle !== undefined ? preset.sliderHandle : "#e2e2e2"
    readonly property color warningColor: preset.warningColor !== undefined ? preset.warningColor : "#bb4b56"
    readonly property bool showControls: !compact || !imageReady

    implicitWidth: 192
    implicitHeight: 192
    clip: true

    Rectangle {
        objectName: "previewBackdrop"
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: root.topColor }
            GradientStop { position: 1; color: root.bottomColor }
        }
    }

    Image {
        id: previewImage
        objectName: "previewImage"
        anchors.fill: parent
        source: preset.thumbnail !== undefined ? preset.thumbnail : (preset.preview !== undefined ? preset.preview : "")
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: status === Image.Ready
        sourceSize: Qt.size(Math.max(1, Math.ceil(width * 2)), Math.max(1, Math.ceil(height * 2)))
    }

    Rectangle {
        objectName: "previewScrim"
        anchors.fill: parent
        color: root.scrimColor
        visible: !root.compact
    }

    Item {
        objectName: "previewControls"
        anchors.fill: parent
        visible: root.showControls

        Repeater {
            model: 4
            delegate: Rectangle {
                required property int index
                objectName: "previewDisc_" + index
                width: Math.round(root.width * 0.17)
                height: width
                radius: width / 2
                x: Math.round(root.width * 0.1 + index * (root.width * 0.8 - width) / 3)
                y: Math.round(root.height * 0.2)
                color: index < 2 ? root.activeColor : root.inactiveColor
            }
        }

        Repeater {
            model: [0.62, 0.35]
            delegate: Item {
                required property int index
                required property real modelData
                x: Math.round(root.width * 0.1)
                y: Math.round(root.height * (0.55 + index * 0.14))
                width: Math.round(root.width * 0.8)
                height: Math.max(4, Math.round(root.height * 0.04))

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: root.trackColor
                }
                Rectangle {
                    visible: index === 1
                    x: parent.width * 0.68
                    width: parent.width - x
                    height: parent.height
                    radius: height / 2
                    color: root.warningColor
                }
                Rectangle {
                    objectName: "previewSliderFill_" + index
                    width: parent.width * modelData
                    height: parent.height
                    radius: height / 2
                    color: root.fillColor
                }
                Rectangle {
                    width: Math.round(parent.height * 2.6)
                    height: width
                    radius: width / 2
                    x: parent.width * modelData - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.handleColor
                }
            }
        }
    }
}
