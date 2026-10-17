import QtQuick

Item {
    id: menu
    property Item wallpaper
    property url assetBase: "qrc:/OceanTopbard/assets/QuickMenu/"
    property var backend: null
    property real renderScale: 1
    property string regularFont: ""
    property string lightFont: ""
    property string timeText: ""
    property string dateText: ""
    property bool reduceMotion: false
    property bool canOpen: true
    property bool showDisabledHandle: false
    property bool appVisible: false
    property Item handleLayer: null
    property color handleColor: "white"
    property bool open: false
    property real progress: 0
    readonly property real bottomY: 1145
    // The handle centers its 16px chevron 8px below this anchor.
    readonly property real chevronStartY: appVisible ? 27 : 94
    readonly property real chevronEndY: 1104
    readonly property real arrival: {
        const t = Math.max(0, Math.min(1, (progress - 0.82) / 0.18))
        return t * t * (3 - 2 * t)
    }
    readonly property real contentOpacity:
        Math.max(0, Math.min(1, (progress - 0.69) / 0.31))

    function settle(toOpen) {
        if (toOpen && !canOpen)
            return
        if (toOpen && !open)
            console.info("Ocean Quick Menu opening over " + (appVisible ? "an app" : "Home"))
        open = toOpen
        if (toOpen && backend)
            backend.setPolling(true)
        else if (backend)
            backend.setPolling(false)
        settleAnimation.stop()
        if (reduceMotion) {
            progress = toOpen ? 1 : 0
            return
        }
        settleAnimation.from = progress
        settleAnimation.to = toOpen ? 1 : 0
        settleAnimation.duration = Math.max(180, Math.round(420 * Math.abs(settleAnimation.to - progress)))
        settleAnimation.start()
    }

    function toggle() { settle(!open) }

    NumberAnimation {
        id: settleAnimation
        target: menu
        property: "progress"
        easing.type: Easing.OutCubic
    }

    Item {
        id: panel
        objectName: "quickMenuPanel"
        x: 0
        y: 0
        width: parent.width
        height: Math.round(menu.bottomY * menu.progress)
        clip: true
        visible: menu.progress > 0

        FrostedBackground {
            width: panel.width
            height: menu.height
            source: menu.wallpaper
        }

        MouseArea {
            anchors.fill: parent
        }

        Item {
            id: contents
            objectName: "quickMenuContents"
            width: panel.width
            height: menu.bottomY
            opacity: menu.contentOpacity
            enabled: menu.progress > 0.88
            Behavior on opacity {
                NumberAnimation { duration: menu.reduceMotion ? 0 : 460; easing.type: Easing.OutQuad }
            }

            Text {
                objectName: "quickModemLabel"
                x: 24
                y: 99
                width: parent.width - 48
                height: 48
                text: menu.backend ? menu.backend.modemLabel : "NO MODEM"
                color: "white"
                font.family: menu.lightFont
                font.pixelSize: 32
                font.weight: Font.Light
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                x: 0
                y: 137
                width: parent.width
                height: 137
                text: menu.timeText
                color: "white"
                font.family: menu.regularFont
                font.weight: Font.Light
                font.pixelSize: 116
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                objectName: "quickDate"
                x: 0
                y: 271
                width: parent.width
                height: 46
                text: menu.dateText
                color: "white"
                font.family: menu.lightFont
                font.pixelSize: 32
                font.weight: Font.Light
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Item {
                x: 0
                y: 355
                width: parent.width
                height: 210

                QuickMenuTile {
                    x: 12
                    objectName: "quickWifi"
                    title: "Wi-Fi"
                    circleColor: "#00d9e7"
                    active: menu.backend ? menu.backend.wifiEnabled : true
                    available: menu.backend ? menu.backend.wifiAvailable : true
                    iconSource: menu.assetBase + "wifi.svg"
                    renderScale: menu.renderScale
                    labelFont: menu.lightFont
                    onClicked: if (menu.backend) menu.backend.setWifiEnabled(!menu.backend.wifiEnabled)
                }
                QuickMenuTile {
                    x: 201
                    objectName: "quickBluetooth"
                    title: "Bluetooth"
                    circleColor: "#0759fa"
                    active: menu.backend ? menu.backend.bluetoothEnabled : true
                    available: menu.backend ? menu.backend.bluetoothAvailable : true
                    iconSource: menu.assetBase + "bluetooth.svg"
                    renderScale: menu.renderScale
                    labelFont: menu.lightFont
                    onClicked: if (menu.backend) menu.backend.setBluetoothEnabled(!menu.backend.bluetoothEnabled)
                }
                QuickMenuTile {
                    x: 396
                    objectName: "quickCellular"
                    title: "Cellular"
                    circleColor: "#00e91d"
                    available: false
                    iconSource: menu.assetBase + "cellular.svg"
                    renderScale: menu.renderScale
                    labelFont: menu.lightFont
                }
                QuickMenuTile {
                    x: 575
                    objectName: "quickAirplane"
                    title: "Airplane\nMode"
                    circleColor: "#777777"
                    active: menu.backend ? menu.backend.airplaneMode : false
                    available: menu.backend !== null
                    iconSource: menu.assetBase + "airplane.svg"
                    renderScale: menu.renderScale
                    labelFont: menu.lightFont
                    onClicked: if (menu.backend) menu.backend.setAirplaneMode(!menu.backend.airplaneMode)
                }
            }

            QuickMenuSlider {
                objectName: "quickVolume"
                x: 41
                y: 620
                value: menu.backend ? menu.backend.volume : 40
                iconSource: menu.assetBase + "volume.svg"
                renderScale: menu.renderScale
                onEdited: value => { if (menu.backend) menu.backend.setVolume(value) }
            }
            QuickMenuSlider {
                objectName: "quickBrightness"
                x: 41
                y: 746
                value: menu.backend ? menu.backend.brightness : 60
                iconSource: menu.assetBase + "brightness.svg"
                renderScale: menu.renderScale
                warningRange: true
                onEdited: value => { if (menu.backend) menu.backend.setBrightness(value) }
            }
        }
    }

    Item {
        id: chevronHandle
        objectName: "quickMenuHandle"
        parent: menu.handleLayer || menu
        width: 170
        height: 76
        x: (menu.width - width) / 2
        y: menu.chevronStartY - (height - 16) / 2
           + (menu.chevronEndY - menu.chevronStartY) * menu.progress
        visible: menu.canOpen || menu.showDisabledHandle || menu.progress > 0
        enabled: menu.canOpen || menu.progress > 0

        Chevron {
            objectName: "topChevron"
            color: menu.handleColor
            anchors.centerIn: parent
            rotation: 180 * (1 - menu.arrival)
        }

        DragHandler {
            id: pullGesture
            target: null
            xAxis.enabled: false
            property real initialProgress: 0
            onActiveChanged: {
                if (active) {
                    settleAnimation.stop()
                    initialProgress = menu.progress
                } else {
                    menu.settle(menu.progress > (menu.open ? 0.65 : 0.25))
                }
            }
            onTranslationChanged: {
                if (active)
                    menu.progress = Math.max(0, Math.min(1,
                        initialProgress + translation.y / (menu.chevronEndY - menu.chevronStartY)))
            }
        }
        TapHandler { onTapped: menu.toggle() }
    }
}
