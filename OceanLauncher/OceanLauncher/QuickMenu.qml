import QtQuick
import QtQuick.Effects
import OpenWave.QuickMenu as Applets

Item {
    id: menu
    property Item wallpaper
    property url assetBase: "qrc:/OceanTopbard/assets/QuickMenu/"
    property var backend: null
    property real renderScale: 1
    property string regularFont: ""
    property string lightFont: ""
    property bool reduceMotion: false
    property bool canOpen: true
    property bool showDisabledHandle: false
    property bool appVisible: false
    property Item handleLayer: null
    property color handleColor: "white"
    property bool open: false
    property real progress: 0
    property int columns: 5
    signal lockRequested()
    signal addAppletsRequested()
    signal appearanceApplied(string id, string name, url wallpaper)
    readonly property real bottomY: Math.round(height * 1145 / 1366)
    readonly property real unit: Math.max(0.1, Math.min(width / 768, bottomY / 1145))
    readonly property real chevronStartY: appVisible ? 27 : 94
    readonly property real chevronEndY: bottomY - 41
    readonly property real arrival: smoothProgress(0.82, 1)
    readonly property real contentOpacity: smoothProgress(0.14, 0.34)
    readonly property var entries: Applets.AppletRegistry.panelApplets
    readonly property int pageCount: Math.max(1, Math.ceil((entries.length + 1) / columns))
    readonly property real columnWidth: width / columns
    readonly property real stripTop: Math.round(134 * unit)
    readonly property bool hasAppearances: Applets.Appearance.presets.length > 0
    readonly property real stripSize: Math.round(Math.min(width / 4, 196 * unit))
    readonly property real appletsTop: hasAppearances ? stripTop + stripSize + Math.round(74 * unit)
                                                      : stripTop + Math.round(24 * unit)
    readonly property real appletRowHeight: Math.round(210 * unit)
    readonly property real dotsTop: appletsTop + appletRowHeight + Math.round(6 * unit)
    readonly property real volumeTop: dotsTop + Math.round(54 * unit)
    readonly property real brightnessTop: volumeTop + Math.round(140 * unit)

    function smoothProgress(start, end) {
        let t = Math.max(0, Math.min(1, (progress - start) / (end - start)))
        return t * t * (3 - 2 * t)
    }

    function revealFor(index) {
        if (reduceMotion)
            return progress >= 0.22 ? 1 : 0
        const start = 0.25 + Math.min(index, 7) * 0.08
        return smoothProgress(start, Math.min(1, start + 0.19))
    }

    function settle(toOpen, releaseVelocity) {
        if (toOpen && !canOpen) return
        open = toOpen
        if (toOpen) {
            Applets.Appearance.refresh()
            Applets.System.refreshBattery()
            pager.contentX = 0
        }
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

    function chooseAppearance(preset) {
        if (!preset || !Applets.Appearance.select(preset.id))
            return
        settle(false, 0)
        appearanceApplied(preset.id, preset.name, preset.wallpaper)
    }

    Connections {
        target: Applets.System
        function onLockRequested() { menu.lockRequested() }
    }

    Timer {
        interval: 30000
        repeat: true
        running: menu.open
        onTriggered: Applets.System.refreshBattery()
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

        MultiEffect {
            width: panel.width
            height: menu.height
            source: menu.wallpaper
            blurEnabled: true
            blurMax: 64
            blur: 1
            autoPaddingEnabled: false
        }
        Rectangle {
            objectName: "quickMenuScrim"
            width: panel.width
            height: menu.height
            color: Applets.Appearance.scrim
            Behavior on color {
                enabled: !menu.reduceMotion
                ColorAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }
        }

        MouseArea { anchors.fill: parent }

        Item {
            id: contents
            objectName: "quickMenuContents"
            width: panel.width
            height: menu.bottomY

            Item {
                id: statusRow
                objectName: "quickMenuStatus"
                x: Math.round(24 * menu.unit)
                y: Math.round(82 * menu.unit)
                width: parent.width - 2 * x
                height: Math.round(44 * menu.unit)
                opacity: menu.contentOpacity

                Text {
                    objectName: "quickBattery"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Applets.System.batteryAvailable
                    text: Applets.System.batteryCharging
                        ? qsTr("Battery %1% - Charging").arg(Applets.System.batteryPercent)
                        : qsTr("Battery %1%").arg(Applets.System.batteryPercent)
                    color: Applets.Appearance.textColor
                    font.family: menu.lightFont
                    font.pixelSize: Math.round(28 * menu.unit)
                    font.weight: Font.Light
                }

                Text {
                    objectName: "quickModemLabel"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.55
                    text: Applets.System.modemLabel
                    color: Applets.Appearance.secondaryTextColor
                    font.family: menu.lightFont
                    font.pixelSize: Math.round(28 * menu.unit)
                    font.weight: Font.Light
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                }
            }

            Flickable {
                id: appearanceStrip
                objectName: "quickMenuAppearances"
                x: 0
                y: menu.stripTop
                width: parent.width
                height: menu.stripSize
                contentWidth: appearanceRow.width
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                interactive: menu.open && menu.progress > 0.98 && contentWidth > width
                visible: menu.hasAppearances
                opacity: menu.smoothProgress(0.18, 0.38)

                Row {
                    id: appearanceRow
                    spacing: Math.round(6 * menu.unit)

                    Repeater {
                        model: Applets.Appearance.presets
                        delegate: Item {
                            id: tile
                            required property var modelData
                            objectName: "quickAppearance_" + modelData.id
                            width: menu.stripSize - Math.round(6 * menu.unit)
                            height: menu.stripSize
                            readonly property bool selected: modelData.id === Applets.Appearance.current
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.name
                            scale: tilePress.pressed ? 0.95 : 1
                            Behavior on scale {
                                enabled: !menu.reduceMotion
                                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                            }
                            Accessible.checkable: true
                            Accessible.checked: selected
                            Accessible.onPressAction: Applets.Appearance.select(modelData.id)

                            Applets.AppearancePreview {
                                objectName: "quickAppearancePreviewTile"
                                anchors.fill: parent
                                preset: tile.modelData
                                compact: true
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: parent.height * 0.4
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: "#99000000" }
                                }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Math.round(12 * menu.unit)
                                text: tile.modelData.name
                                color: Applets.Appearance.textColor
                                font.family: menu.lightFont
                                font.pixelSize: Math.round(34 * menu.unit)
                                font.weight: Font.Light
                            }
                            Rectangle {
                                objectName: "quickAppearanceSelection"
                                anchors.fill: parent
                                color: "transparent"
                                border.width: Math.max(3, Math.round(4 * menu.unit))
                                border.color: Applets.Appearance.selectionColor
                                opacity: tile.selected ? 1 : 0
                                Behavior on opacity {
                                    enabled: !menu.reduceMotion
                                    NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                                }
                            }
                            TapHandler {
                                id: tilePress
                                enabled: menu.open && menu.progress > 0.96
                                onTapped: menu.chooseAppearance(tile.modelData)
                            }
                        }
                    }
                }
            }

            Rectangle {
                objectName: "quickAppearanceScroll"
                visible: menu.hasAppearances && appearanceStrip.contentWidth > appearanceStrip.width
                x: appearanceStrip.contentWidth > 0
                    ? appearanceStrip.width * appearanceStrip.contentX / appearanceStrip.contentWidth : 0
                y: menu.stripTop + menu.stripSize + Math.round(6 * menu.unit)
                width: appearanceStrip.contentWidth > 0
                    ? appearanceStrip.width * appearanceStrip.width / appearanceStrip.contentWidth : 0
                height: Math.max(2, Math.round(3 * menu.unit))
                color: Applets.Appearance.secondaryTextColor
                opacity: appearanceStrip.opacity
            }

            Flickable {
                id: pager
                objectName: "quickMenuPager"
                x: 0
                y: menu.appletsTop
                width: parent.width
                height: menu.appletRowHeight
                contentWidth: width * menu.pageCount
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                interactive: menu.open && menu.progress > 0.98 && menu.pageCount > 1
                readonly property int page: Math.max(0, Math.min(menu.pageCount - 1,
                    Math.round(contentX / Math.max(1, width))))

                NumberAnimation {
                    id: pageSnap
                    target: pager
                    property: "contentX"
                    duration: menu.reduceMotion ? 0 : 200
                    easing.type: Easing.OutCubic
                }

                function snapToNearest() {
                    pageSnap.stop()
                    pageSnap.from = contentX
                    pageSnap.to = page * width
                    pageSnap.start()
                }

                onMovementEnded: snapToNearest()

                Repeater {
                    model: menu.entries
                    delegate: Applets.AppletHost {
                        required property var modelData
                        required property int index
                        objectName: "quickApplet_" + modelData.id
                        x: Math.floor(index / menu.columns) * pager.width
                           + (index % menu.columns) * menu.columnWidth
                        width: menu.columnWidth
                        height: pager.height
                        appletId: modelData.id
                        opacity: menu.revealFor(index)
                        interactive: menu.open && menu.progress > 0.96 && opacity > 0.96
                        reduceMotion: menu.reduceMotion
                        renderScale: menu.renderScale
                        labelFont: menu.lightFont
                    }
                }

                Item {
                    id: addSlot
                    objectName: "quickMenuAddApplets"
                    readonly property int slotIndex: menu.entries.length
                    x: Math.floor(slotIndex / menu.columns) * pager.width
                       + (slotIndex % menu.columns) * menu.columnWidth
                    width: menu.columnWidth
                    height: pager.height
                    opacity: menu.revealFor(slotIndex)

                    Applets.Applet {
                        anchors.fill: parent
                        title: qsTr("Add Applets")
                        icon: menu.assetBase + "plus.svg"
                        statusText: qsTr("Choose which applets appear in the Quick Menu")
                        available: true
                        active: false
                        interactive: menu.open && menu.progress > 0.96 && addSlot.opacity > 0.96
                        reduceMotion: menu.reduceMotion
                        renderScale: menu.renderScale
                        labelFont: menu.lightFont
                        onTriggered: menu.addAppletsRequested()
                    }
                }
            }

            Row {
                id: pageDots
                objectName: "quickMenuPageDots"
                visible: menu.pageCount > 1
                spacing: Math.round(14 * menu.unit)
                anchors.horizontalCenter: parent.horizontalCenter
                y: menu.dotsTop
                opacity: menu.smoothProgress(0.3, 0.5)

                Repeater {
                    model: menu.pageCount
                    delegate: Rectangle {
                        required property int index
                        width: Math.round(20 * menu.unit)
                        height: width
                        radius: width / 2
                        color: Applets.Appearance.textColor
                        opacity: index === pager.page ? 0.9 : 0.35
                    }
                }
            }

            Repeater {
                model: Applets.AppletRegistry.residentApplets
                delegate: Applets.AppletHost {
                    required property var modelData
                    visible: false
                    width: 0
                    height: 0
                    appletId: modelData.id
                    interactive: false
                }
            }

            QuickMenuSlider {
                objectName: "quickVolume"
                transformOrigin: Item.TopLeft
                scale: menu.unit
                x: Math.round(56 * menu.unit)
                y: menu.volumeTop
                width: (parent.width - Math.round(112 * menu.unit)) / menu.unit
                accessibleName: qsTr("Volume")
                labelFont: menu.lightFont
                value: Applets.System.volume
                iconSource: menu.assetBase + "volume.svg"
                renderScale: menu.renderScale
                interactive: menu.open && menu.progress > 0.96
                reduceMotion: menu.reduceMotion
                opacity: menu.smoothProgress(0.4, 0.6)
                onEdited: value => Applets.System.setVolume(value)
            }

            QuickMenuSlider {
                objectName: "quickBrightness"
                transformOrigin: Item.TopLeft
                scale: menu.unit
                x: Math.round(56 * menu.unit)
                y: menu.brightnessTop
                width: (parent.width - Math.round(112 * menu.unit)) / menu.unit
                accessibleName: qsTr("Device Brightness")
                labelFont: menu.lightFont
                value: Applets.System.brightness
                iconSource: menu.assetBase + "brightness.svg"
                renderScale: menu.renderScale
                warningRange: true
                interactive: menu.open && menu.progress > 0.96
                reduceMotion: menu.reduceMotion
                opacity: menu.smoothProgress(0.46, 0.66)
                onEdited: value => Applets.System.setBrightness(value)
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
