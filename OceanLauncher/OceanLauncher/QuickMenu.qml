import QtQuick
import QtQuick.Controls

Item {
    id: menu
    property Item wallpaper
    property url assetBase: "qrc:/OceanTopbard/assets/QuickMenu/"
    property var backend: null
    property var appController: null
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
    property bool editMode: false
    property var defaultOrder: ["wifi", "bluetooth", "airplane", "lock", "volume", "brightness"]
    property var fallbackOrder: defaultOrder.slice(0)
    property var fallbackHidden: []
    signal lockRequested()
    readonly property real bottomY: 1145
    readonly property real chevronStartY: appVisible ? 27 : 94
    readonly property real chevronEndY: 1104
    readonly property real arrival: smoothProgress(0.82, 1)
    readonly property real contentOpacity: smoothProgress(0.14, 0.34)
    readonly property var controlOrder: appController
        ? appController.quickMenuOrder : fallbackOrder
    readonly property var hiddenControls: appController
        ? appController.quickMenuHiddenControls : fallbackHidden
    readonly property var visibleControlOrder: controlOrder.filter(id => !hiddenControls.includes(id))
    readonly property var displayedControlOrder: editMode ? controlOrder : visibleControlOrder
    readonly property var labels: ({
        wifi: qsTr("Wi-Fi"), bluetooth: qsTr("Bluetooth"),
        airplane: qsTr("Airplane Mode"), volume: qsTr("Volume"),
        brightness: qsTr("Brightness"), lock: qsTr("Lock Screen")
    })

    function smoothProgress(start, end) {
        let t = Math.max(0, Math.min(1, (progress - start) / (end - start)))
        return t * t * (3 - 2 * t)
    }

    function revealFor(index) {
        if (reduceMotion)
            return progress >= 0.22 ? 1 : 0
        const start = 0.25 + index * 0.10
        return smoothProgress(start, Math.min(1, start + 0.19))
    }

    function slotMetrics(controlId, totalWidth) {
        const tile = ["wifi", "bluetooth", "airplane", "lock"].includes(controlId)
        return {
            width: tile ? totalWidth / 4 : totalWidth,
            height: tile ? 260 : 126
        }
    }

    function slotPosition(index) {
        let x = 0
        let y = 0
        let rowHeight = 0
        for (let i = 0; i < index; ++i) {
            const metrics = slotMetrics(displayedControlOrder[i], controlFlow.width)
            if (x > 0 && x + metrics.width > controlFlow.width) {
                x = 0
                y += rowHeight
                rowHeight = 0
            }
            x += metrics.width
            rowHeight = Math.max(rowHeight, metrics.height)
        }
        const current = slotMetrics(displayedControlOrder[index], controlFlow.width)
        if (x > 0 && x + current.width > controlFlow.width) {
            x = 0
            y += rowHeight
        }
        return {x: x, y: y}
    }

    function controlsHeight() {
        let x = 0
        let y = 0
        let rowHeight = 0
        for (let i = 0; i < displayedControlOrder.length; ++i) {
            const metrics = slotMetrics(displayedControlOrder[i], controlFlow.width)
            if (x > 0 && x + metrics.width > controlFlow.width) {
                x = 0
                y += rowHeight
                rowHeight = 0
            }
            x += metrics.width
            rowHeight = Math.max(rowHeight, metrics.height)
        }
        return y + rowHeight
    }

    function orderForVisibleIndex(index) {
        const id = visibleControlOrder[index]
        const originalIndex = controlOrder.indexOf(id)
        return originalIndex < 0 ? index : originalIndex
    }

    function settle(toOpen, releaseVelocity) {
        if (toOpen && !canOpen) return
        open = toOpen
        if (!toOpen) endEditing()
        if (toOpen && backend) backend.setPolling(true)
        else if (backend) backend.setPolling(false)
        settleAnimation.stop()
        if (reduceMotion) {
            progress = toOpen ? 1 : 0
            return
        }
        settleAnimation.from = progress
        settleAnimation.to = toOpen ? 1 : 0
        const distance = Math.abs(settleAnimation.to - progress)
        const velocity = Math.abs(releaseVelocity || 0)
        const durationByDistance = 220 + 200 * distance
        const durationByVelocity = velocity > 1 ? 1000 * distance / velocity : durationByDistance
        settleAnimation.duration = Math.max(180, Math.min(420,
            Math.round(Math.min(durationByDistance, durationByVelocity))))
        settleAnimation.start()
    }

    function toggle() { settle(!open, 0) }

    function beginEditing() {
        if (open && progress > 0.96)
            editMode = true
    }

    function endEditing() { editMode = false }

    function setControlHidden(controlId, hidden) {
        if (hidden && visibleControlOrder.length <= 1)
            return
        const next = hiddenControls.filter(id => id !== controlId)
        if (hidden)
            next.push(controlId)
        if (appController)
            appController.setQuickMenuHiddenControls(next)
        else
            fallbackHidden = next
    }

    function moveControl(controlId, targetIndex) {
        const next = controlOrder.slice(0)
        const sourceIndex = next.indexOf(controlId)
        if (sourceIndex < 0 || targetIndex < 0 || targetIndex >= next.length || sourceIndex === targetIndex)
            return
        next.splice(sourceIndex, 1)
        next.splice(targetIndex, 0, controlId)
        if (appController)
            appController.setQuickMenuOrder(next)
        else
            fallbackOrder = next
    }

    function dropControl(controlId, dragX, dragY, slot) {
        if (Math.hypot(dragX, dragY) < 16)
            return
        const point = Qt.point(slot.x + slot.width / 2 + dragX,
                               slot.y + slot.height / 2 + dragY)
        let target = null
        let closestDistance = Number.MAX_VALUE
        for (let i = 0; i < controlRepeater.count; ++i) {
            const candidate = controlRepeater.itemAt(i)
            if (!candidate || candidate.controlId === controlId)
                continue
            const topLeft = candidate.mapToItem(controlFlow, 0, 0)
            const right = topLeft.x + candidate.width
            const bottom = topLeft.y + candidate.height
            const dx = Math.max(topLeft.x - point.x, 0, point.x - right)
            const dy = Math.max(topLeft.y - point.y, 0, point.y - bottom)
            const distance = dx * dx + dy * dy
            if (distance < closestDistance) {
                closestDistance = distance
                target = candidate
            }
        }
        if (!target)
            return

        const sourceIndex = controlOrder.indexOf(controlId)
        let targetIndex = target.visualIndex
        if (closestDistance > 0) {
            const topLeft = target.mapToItem(controlFlow, 0, 0)
            const horizontalTarget = target.width < controlFlow.width * 0.7
            const afterTarget = horizontalTarget
                ? point.x >= topLeft.x + target.width / 2
                : point.y >= topLeft.y + target.height / 2
            if (afterTarget)
                targetIndex += sourceIndex < target.visualIndex ? 0 : 1
            else if (sourceIndex < target.visualIndex)
                targetIndex -= 1
        }
        targetIndex = Math.max(0, Math.min(controlOrder.length - 1, targetIndex))
        moveControl(controlId, targetIndex)
    }

    NumberAnimation {
        id: settleAnimation
        target: menu
        property: "progress"
        easing.type: Easing.OutCubic
    }

    Item {
        id: panel
        objectName: "quickMenuPanel"
        width: parent.width
        height: Math.round(menu.bottomY * menu.progress)
        clip: true
        visible: menu.progress > 0

        FrostedBackground {
            width: panel.width
            height: menu.height
            source: menu.wallpaper
        }

        MouseArea { anchors.fill: parent }

        Item {
            id: contents
            objectName: "quickMenuContents"
            width: panel.width
            height: menu.bottomY

            Text {
                objectName: "quickModemLabel"
                x: 24; width: parent.width - 48; height: 48
                text: menu.editMode ? qsTr("Edit controls")
                    : menu.backend ? menu.backend.modemLabel : "NO MODEM"
                color: "white"; font.family: menu.lightFont
                font.pixelSize: 32; font.weight: Font.Light
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: menu.contentOpacity
                y: 99 + (1 - menu.contentOpacity) * (menu.reduceMotion ? 0 : 12)
            }

            AbstractButton {
                objectName: "quickMenuDoneEditing"
                visible: menu.editMode
                x: parent.width - 126; y: 101; width: 100; height: 44
                text: qsTr("Done")
                Accessible.name: qsTr("Finish arranging Quick Menu controls")
                onClicked: menu.endEditing()
                contentItem: Text {
                    text: parent.text; color: "white"
                    font.family: menu.lightFont; font.pixelSize: 24
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? "#30ffffff" : "transparent"
                    radius: 10
                }
            }

            Text {
                objectName: "quickMenuClock"
                x: 0; y: 137; width: parent.width; height: 137
                text: menu.timeText; color: "white"
                font.family: menu.regularFont; font.weight: Font.Light
                font.pixelSize: 116
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: menu.contentOpacity
                transform: Translate { y: (1 - menu.contentOpacity) * (menu.reduceMotion ? 0 : 14) }
            }

            Text {
                objectName: "quickDate"
                x: 0; y: 271; width: parent.width; height: 46
                text: menu.dateText; color: "white"
                font.family: menu.lightFont; font.pixelSize: 32; font.weight: Font.Light
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: menu.smoothProgress(0.20, 0.39)
                transform: Translate { y: (1 - parent.opacity) * (menu.reduceMotion ? 0 : 14) }
            }

            Flickable {
                id: controlsViewport
                objectName: "quickMenuControlsViewport"
                x: 24; y: 335; width: parent.width - 48
                height: menu.bottomY - y - 170
                contentWidth: width
                contentHeight: controlFlow.height + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: menu.open && menu.progress > 0.98
                    && !menu.editMode && controlFlow.height + 24 > height

                Item {
                    id: controlFlow
                    objectName: "quickMenuControlFlow"
                    width: controlsViewport.width
                    height: menu.controlsHeight()

                    Repeater {
                        id: controlRepeater
                        model: menu.displayedControlOrder
                        delegate: Item {
                            id: controlSlot
                            required property int index
                            required property string modelData
                            objectName: "quickMenuSlot_" + modelData
                            property string controlId: modelData
                            property int visualIndex: index
                            property real reveal: menu.revealFor(visualIndex)
                            property var placement: menu.slotPosition(index)
                            x: placement.x
                            y: placement.y
                            width: menu.slotMetrics(controlId, controlFlow.width).width
                            height: menu.slotMetrics(controlId, controlFlow.width).height
                            opacity: reveal
                            enabled: menu.editMode || (menu.open && menu.progress > 0.96 && reveal > 0.96)
                            transform: [
                                Translate {
                                    x: 0
                                    y: -(1 - controlSlot.reveal) * (menu.reduceMotion ? 0 : 16)
                                },
                                Scale {
                                    origin.x: controlSlot.width / 2
                                    origin.y: controlSlot.height / 2
                                    xScale: menu.reduceMotion ? 1 : 0.96 + controlSlot.reveal * 0.04
                                    yScale: menu.reduceMotion ? 1 : 0.96 + controlSlot.reveal * 0.04
                                },
                                Translate {
                                    x: menu.editMode && editDrag.active ? editDrag.translation.x : 0
                                    y: menu.editMode && editDrag.active ? editDrag.translation.y : 0
                                }
                            ]

                            TapHandler {
                                objectName: "quickMenuLongPress_" + controlSlot.controlId
                                enabled: menu.open && menu.progress > 0.96
                                onLongPressed: menu.beginEditing()
                            }

                            DragHandler {
                                id: editDrag
                                objectName: "quickMenuReorder_" + controlSlot.controlId
                                enabled: menu.editMode
                                target: null
                                property point releaseTranslation: Qt.point(0, 0)
                                onActiveChanged: {
                                    if (active)
                                        releaseTranslation = Qt.point(0, 0)
                                    else if (menu.editMode)
                                        menu.dropControl(controlSlot.controlId,
                                            releaseTranslation.x, releaseTranslation.y, controlSlot)
                                }
                                onTranslationChanged: if (active)
                                    releaseTranslation = translation
                            }

                            Item {
                                objectName: "quickMenuVisibility_" + controlSlot.controlId
                                visible: menu.editMode
                                z: 12
                                x: controlSlot.width - width - 12
                                y: 10
                                width: 48; height: 48
                                Accessible.name: menu.hiddenControls.includes(controlSlot.controlId)
                                    ? qsTr("Show %1").arg(menu.labels[controlSlot.controlId])
                                    : qsTr("Hide %1").arg(menu.labels[controlSlot.controlId])
                                Accessible.role: Accessible.Button
                                Rectangle {
                                    anchors.fill: parent
                                    radius: width / 2
                                    color: "#80333333"
                                    border.color: "#bfffffff"; border.width: 1
                                }
                                Text {
                                    anchors.fill: parent
                                    text: menu.hiddenControls.includes(controlSlot.controlId) ? "+" : "−"
                                    color: "white"; font.pixelSize: 36
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                MouseArea {
                                    objectName: "quickMenuToggleVisibility_" + controlSlot.controlId
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    preventStealing: true
                                    onClicked: menu.setControlHidden(controlSlot.controlId,
                                        !menu.hiddenControls.includes(controlSlot.controlId))
                                }
                            }

                            QuickMenuTile {
                                anchors.fill: parent
                                objectName: controlSlot.controlId === "wifi" ? "quickWifi" : ""
                                visible: controlSlot.controlId === "wifi"
                                topInset: 42
                                title: menu.labels.wifi
                                circleColor: "#00d9e7"
                                active: menu.backend ? menu.backend.wifiEnabled : false
                                available: menu.backend ? menu.backend.wifiAvailable : false
                                interactive: controlSlot.enabled && !menu.editMode
                                reduceMotion: menu.reduceMotion
                                iconSource: menu.assetBase + "wifi.svg"
                                renderScale: menu.renderScale
                                labelFont: menu.lightFont
                                onClicked: if (menu.backend) menu.backend.setWifiEnabled(!menu.backend.wifiEnabled)
                            }
                            QuickMenuTile {
                                anchors.fill: parent
                                objectName: controlSlot.controlId === "bluetooth" ? "quickBluetooth" : ""
                                visible: controlSlot.controlId === "bluetooth"
                                topInset: 42
                                title: menu.labels.bluetooth
                                circleColor: "#0759fa"
                                active: menu.backend ? menu.backend.bluetoothEnabled : false
                                available: menu.backend ? menu.backend.bluetoothAvailable : false
                                interactive: controlSlot.enabled && !menu.editMode
                                reduceMotion: menu.reduceMotion
                                iconSource: menu.assetBase + "bluetooth.svg"
                                renderScale: menu.renderScale
                                labelFont: menu.lightFont
                                onClicked: if (menu.backend) menu.backend.setBluetoothEnabled(!menu.backend.bluetoothEnabled)
                            }
                            QuickMenuTile {
                                anchors.fill: parent
                                objectName: controlSlot.controlId === "airplane" ? "quickAirplane" : ""
                                visible: controlSlot.controlId === "airplane"
                                topInset: 42
                                title: menu.labels.airplane
                                circleColor: "#777777"
                                active: menu.backend ? menu.backend.airplaneMode : false
                                available: menu.backend !== null
                                interactive: controlSlot.enabled && !menu.editMode
                                reduceMotion: menu.reduceMotion
                                iconSource: menu.assetBase + "airplane.svg"
                                renderScale: menu.renderScale
                                labelFont: menu.lightFont
                                onClicked: if (menu.backend) menu.backend.setAirplaneMode(!menu.backend.airplaneMode)
                            }
                            QuickMenuSlider {
                                anchors.fill: parent
                                objectName: controlSlot.controlId === "volume" ? "quickVolume" : ""
                                visible: controlSlot.controlId === "volume"
                                accessibleName: menu.labels.volume
                                value: menu.backend ? menu.backend.volume : 40
                                iconSource: menu.assetBase + "volume.svg"
                                renderScale: menu.renderScale
                                interactive: controlSlot.enabled && !menu.editMode
                                reduceMotion: menu.reduceMotion
                                onEdited: value => { if (menu.backend) menu.backend.setVolume(value) }
                            }
                            QuickMenuSlider {
                                anchors.fill: parent
                                objectName: controlSlot.controlId === "brightness" ? "quickBrightness" : ""
                                visible: controlSlot.controlId === "brightness"
                                accessibleName: menu.labels.brightness
                                value: menu.backend ? menu.backend.brightness : 60
                                iconSource: menu.assetBase + "brightness.svg"
                                renderScale: menu.renderScale
                                warningRange: true
                                interactive: controlSlot.enabled && !menu.editMode
                                reduceMotion: menu.reduceMotion
                                onEdited: value => { if (menu.backend) menu.backend.setBrightness(value) }
                            }
                            QuickMenuTile {
                                objectName: controlSlot.controlId === "lock" ? "quickMenuLockAction" : ""
                                visible: controlSlot.controlId === "lock"
                                anchors.fill: parent
                                topInset: 42
                                title: menu.labels.lock
                                circleColor: "#858585"
                                active: false
                                available: true
                                interactive: controlSlot.enabled && !menu.editMode
                                stateDescription: qsTr("Lock the current OpenWave session")
                                reduceMotion: menu.reduceMotion
                                iconSource: menu.assetBase + "lock.svg"
                                renderScale: menu.renderScale
                                labelFont: menu.lightFont
                                onClicked: menu.lockRequested()
                            }

                        }
                    }
                }
            }

        }
    }

    Item {
        id: chevronHandle
        objectName: "quickMenuHandle"
        parent: menu.handleLayer || menu
        width: 170; height: 76
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
            property real previousY: 0
            property double previousTime: 0
            property real releaseVelocity: 0
            onActiveChanged: {
                if (active) {
                    settleAnimation.stop()
                    initialProgress = menu.progress
                    previousY = translation.y
                    previousTime = Date.now()
                    releaseVelocity = 0
                } else {
                    const velocity = releaseVelocity
                    const opening = velocity > 780 ? true
                        : velocity < -780 ? false : menu.progress >= 0.5
                    menu.settle(opening, velocity)
                }
            }
            onTranslationChanged: {
                if (!active) return
                const now = Date.now()
                const elapsed = Math.max(1, now - previousTime)
                const delta = translation.y - previousY
                releaseVelocity = delta * 1000 / elapsed
                previousY = translation.y
                previousTime = now
                menu.progress = Math.max(0, Math.min(1,
                    initialProgress + translation.y / (menu.chevronEndY - menu.chevronStartY)))
            }
        }
        TapHandler { onTapped: menu.toggle() }
    }
}
