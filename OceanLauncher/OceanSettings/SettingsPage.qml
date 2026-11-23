import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import OpenWave.Theme 1.0
import OpenWave.Controls 1.0
import OpenWave.Fonts 1.0
import OpenWave.Settings 1.0

Item {
    id: root
    objectName: "settingsPage"
    property var backend: null
    property var connectivityBackend: null
    property var promptClient: null
    property var portalClient: null
    property var permissionClient: null
    property var applicationsBackend: null
    property Item wallpaper
    property real renderScale: 1
    property real topInset: 0
    property int currentTab: Math.min(1, tabEntries.length - 1)
    property bool searching: false
    property bool cropToolOpen: false
    property bool tabSwitching: false
    property bool tabCapturePending: false
    property int tabCaptureToken: 0
    property real tabIncomingOpacity: 1
    property var tabCapture: null
    property int pendingTab: -1
    property var pendingSearchEntry: null
    readonly property bool largeText: backend ? backend.largeText : false
    readonly property var builtinTabs: ["accessibility", "appearance", "apps"]
    readonly property var builtinTabOrders: ({accessibility: 300, appearance: 400, apps: 500})
    property var moduleEntries: SettingsModuleRegistry.modules()
    readonly property var tabEntries: builtinTabs.map(key => ({
            key: key, title: key, moduleId: "", order: builtinTabOrders[key]}))
        .concat(moduleEntries.map(entry => ({
            key: entry.id, title: entry.name, moduleId: entry.id, order: entry.order})))
        .sort((left, right) => left.order - right.order)
    readonly property var tabs: tabEntries.map(entry => entry.title)
    readonly property int accessibilityTab: tabEntries.findIndex(entry => entry.key === "accessibility")
    readonly property int appearanceTab: tabEntries.findIndex(entry => entry.key === "appearance")
    readonly property int appsTab: tabEntries.findIndex(entry => entry.key === "apps")
    readonly property bool moduleTabActive: currentTab >= 0 && currentTab < tabEntries.length
        && tabEntries[currentTab].moduleId.length > 0
    readonly property string activeModuleId: moduleTabActive ? tabEntries[currentTab].moduleId : ""
    readonly property var builtinSearchEntries: [
        {title: "Reduce motion", detail: "Launcher and drawer animations", tab: accessibilityTab, key: "motion"},
        {title: "Larger text", detail: "Increase text size in Settings", tab: accessibilityTab, key: "text"},
        {title: qsTr("Appearance"), detail: qsTr("Backgrounds, colors and wallpapers"), tab: appearanceTab, key: "appearance"},
        {title: qsTr("Apps"), detail: qsTr("Installed apps and application permissions"), tab: appsTab, key: "apps"}
    ]
    readonly property var moduleSearchEntries: moduleEntries.reduce((result, entry) => {
        const tab = tabEntries.findIndex(item => item.moduleId === entry.id)
        result.push({title: entry.name, detail: entry.description, tab: tab, key: entry.id, entryKey: ""})
        for (const item of entry.searchEntries)
            result.push({title: item.title, detail: item.detail, tab: tab, key: entry.id + ":" + item.key, entryKey: item.key})
        return result
    }, [])
    readonly property var searchEntries: builtinSearchEntries.concat(moduleSearchEntries)
    readonly property var searchResults: searchEntries.filter(entry =>
        (entry.title + " " + entry.detail).toLowerCase().includes(searchField.text.trim().toLowerCase()))
    signal closed()
    Connections {
        target: SettingsModuleRegistry
        function onReloaded() {
            const current = root.tabEntries[root.currentTab]
            const key = current ? current.key : ""
            root.moduleEntries = SettingsModuleRegistry.modules()
            const index = root.tabEntries.findIndex(entry => entry.key === key)
            root.currentTab = index >= 0 ? index : 0
        }
    }
    onCurrentTabChanged: {
        if (currentTab !== appearanceTab) { wallpaperSourceMenu.close(); appearancePage.closeMenu() }
    }
    onVisibleChanged: {
        if (!visible) {
            resetTabTransition()
        }
    }
    onSearchingChanged: {
    }
    component ActionButton: WRButton {
        fontPixelSize: 26
        textColor: "white"
        surfaceColor: "#263740"
        pressedSurfaceColor: Theme.pressedColor
        implicitWidth: contentItem.implicitWidth + 32
        implicitHeight: 56
    }

    function selectTab(index, animated) {
        const next = Math.max(0, Math.min(tabs.length - 1, index))
        if (animated && next !== currentTab && visible
                && !(backend && backend.reduceMotion) && paneViewport.width > 0) {
            pendingTab = next
            if (tabCapturePending)
                return
            tabCrossfade.stop()
            tabIncomingOpacity = 1
            outgoingTabFrame.opacity = 0
            tabCapture = null
            tabCapturePending = true
            const captureToken = ++tabCaptureToken
            if (paneViewport.grabToImage(function(result) {
                if (captureToken !== tabCaptureToken)
                    return
                tabCapturePending = false
                const target = pendingTab
                pendingTab = -1
                if (target < 0 || !result) {
                    if (target >= 0) commitTab(target)
                    return
                }
                tabCapture = result
                outgoingTabFrame.opacity = 1
                tabIncomingOpacity = 0
                tabSwitching = true
                commitTab(target)
                tabCrossfade.start()
            }))
                return
            tabCapturePending = false
            pendingTab = -1
        }
        ++tabCaptureToken
        tabCapturePending = false
        tabCrossfade.stop()
        pendingTab = -1
        outgoingTabFrame.opacity = 0
        tabCapture = null
        tabIncomingOpacity = 1
        tabSwitching = false
        commitTab(next)
    }

    function resetTabTransition() {
        pendingSearchEntry = null
        ++tabCaptureToken
        tabCapturePending = false
        pendingTab = -1
        tabCrossfade.stop()
        outgoingTabFrame.opacity = 0
        tabCapture = null
        tabIncomingOpacity = 1
        tabSwitching = false
    }

    function commitTab(index) {
        tabStrip.cancelFlick()
        currentTab = index
        searching = false
        searchField.text = ""
        const item = tabRepeater.itemAt(currentTab)
        if (item) {
            if (item.x < tabStrip.contentX)
                tabStrip.contentX = item.x
            else if (item.x + item.width > tabStrip.contentX + tabStrip.width)
                tabStrip.contentX = item.x + item.width - tabStrip.width
        }
        root.forceActiveFocus()
        const entry = pendingSearchEntry
        pendingSearchEntry = null
        if (entry && entry.tab === index && entry.entryKey)
            moduleHost.openEntry(entry.entryKey)
    }

    function openSearchResult(entry) {
        pendingSearchEntry = entry
        selectTab(entry.tab, true)
    }

    ParallelAnimation {
        id: tabCrossfade
        NumberAnimation {
            target: root; property: "tabIncomingOpacity"
            from: 0; to: 1; duration: 240; easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: outgoingTabFrame; property: "opacity"
            from: 1; to: 0; duration: 240; easing.type: Easing.InOutQuad
        }
        onFinished: {
            root.tabCapture = null
            root.tabSwitching = false
        }
    }

    function back() {
        if (wallpaperSourceMenu.visible) { wallpaperSourceMenu.close(); return true }
        if (currentTab === appsTab && appsPage.back()) return true
        if (moduleTabActive && !searching && moduleHost.back()) return true
        if (wallpaperCropDialog.visible) { wallpaperCropDialog.beginClose(); return true }
        if (currentTab === appearanceTab && appearancePage.back()) return true
        if (searching) { selectTab(currentTab); return true }
        return false
    }

    function chooseWallpaperFile(imageSource) {
        if (!portalClient || portalClient.busy) return
        wallpaperSourceMenu.close()
        const options = {
            title: "Choose image",
            mutedTitle: qsTr("Choose"),
            primaryTitle: qsTr("image"),
            requestIdentifier: "settings-wallpaper"
        }
        if (imageSource === "photos") portalClient.openPhoto(options)
        else portalClient.openFile(Object.assign(options, {
            mimeTypes: ["image/*"], imageSource: "files"
        }))
    }

    Connections {
        target: root.promptClient
        function onErrorOccurred(message) { root.showMessage(message) }
    }

    function showMessage(message) {
        notice.text = message
        noticeTimer.restart()
    }

    Connections {
        target: root.backend
        function onErrorOccurred(message) { root.showMessage(message) }
    }
    Connections {
        target: root.portalClient
        function onCompleted(grants) {
            if (!grants.length) { root.showMessage(qsTr("No image was selected.")); return }
            const url = grants[0].temporaryCopy()
            if (!url.toString().length) { root.showMessage(qsTr("The selected image could not be opened.")); return }
            wallpaperCropDialog.openImage(url)
        }
        function onErrorOccurred(message) { root.showMessage(message) }
    }

    FrostedBackground { anchors.fill: parent; source: root.wallpaper; visible: root.wallpaper && root.wallpaper.status === Image.Ready }
    Item {
        id: controls
        y: root.topInset
        width: parent.width
        height: parent.height - y

        MouseArea { anchors.fill: parent }

        AbstractButton {
            id: searchButton
            objectName: "settingsSearch"
            x: 32; y: 41; width: 70; height: 80
            Accessible.name: root.searching ? "Close search" : "Search settings"
            onClicked: {
                root.searching = !root.searching
                searchField.text = ""
                if (root.searching) searchField.forceActiveFocus()
            }
            contentItem: Item {
                SettingsSymbol { anchors.centerIn: parent; color: root.searching ? Theme.accentColor : "white" }
            }
        }

        Flickable {
            id: tabStrip
            objectName: "settingsTabs"
            visible: !root.searching
            x: 116; y: 43; width: parent.width - x; height: 80
            contentWidth: tabRow.width + 32
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            Row {
                id: tabRow
                spacing: 26
                Repeater {
                    id: tabRepeater
                    model: root.tabs
                    AbstractButton {
                        id: tabButton
                        required property string modelData
                        required property int index
                        objectName: "settingsTab_" + modelData
                        width: tabText.implicitWidth + 12
                        height: 70
                        Accessible.name: modelData
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: root.currentTab === index
                        onClicked: root.selectTab(index, true)
                        contentItem: Text {
                            id: tabText
                            text: tabButton.modelData.toLowerCase()
                            textFormat: Text.PlainText
                            font.family: Fonts.family
                            font.weight: Font.ExtraLight
                            font.pixelSize: 46
                            color: root.currentTab === tabButton.index ? Theme.highlightColor : "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Item {
                            Rectangle {
                                objectName: "settingsTabLine_" + tabButton.modelData
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 4
                                color: Theme.accentColor
                                opacity: root.currentTab === tabButton.index ? 1 : 0
                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: root.backend && root.backend.reduceMotion ? 0 : 240
                                        easing.type: Easing.InOutQuad
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        WRTextField {
            id: searchField
            objectName: "settingsSearchField"
            visible: root.searching
            x: 116; y: 48; width: parent.width - x - 42; height: 70
            placeholderText: "Search settings"
            accessibleLabel: "Search settings"
            fontPixelSize: 34
            color: "white"
            placeholderTextColor: "#b7c0c5"
            selectionColor: "#008ec0"
            background: Item {}
            onAccepted: {
                if (root.searchResults.length === 1)
                    root.openSearchResult(root.searchResults[0])
            }
        }

        Rectangle { x: 38; y: 140; width: parent.width - 82; height: 2; color: "#d4dadd" }

        Item {
            id: paneViewport
            objectName: "settingsPaneViewport"
            x: 48; y: 164; width: parent.width - 96; height: parent.height - y - 48
            clip: true
        Flickable {
            id: content
            objectName: "settingsMainPane"
            opacity: root.tabIncomingOpacity
            visible: (root.currentTab !== root.appsTab && !root.moduleTabActive) || root.searching
            x: 0; y: 0; width: paneViewport.width
            height: paneViewport.height
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            onVisibleChanged: contentY = 0
            Connections { target: root; function onCurrentTabChanged() { content.contentY = 0 } }
            Column {
                id: body
                width: parent.width
                spacing: 0

                Column {
                    visible: root.searching
                    width: parent.width
                    Repeater {
                        model: root.searchResults
                        SettingsRow {
                            required property var modelData
                            heading: modelData.title
                            detail: modelData.detail
                            largeText: root.largeText
                            onClicked: root.openSearchResult(modelData)
                        }
                    }
                    Text {
                        visible: root.searchResults.length === 0
                        text: "No matching settings"
                        color: "#d4dadd"
                        font.family: Fonts.family
                        font.pixelSize: 30
                    }
                }

                Column {
                    visible: !root.searching && root.currentTab === root.accessibilityTab
                    width: parent.width
                    SettingsRow {
                        objectName: "settingsReduceMotion"
                        heading: "Reduce motion"
                        detail: "Skip launcher and drawer movement"
                        largeText: root.largeText
                        toggle: true
                        checked: root.backend ? root.backend.reduceMotion : false
                        enabled: root.backend !== null
                        onClicked: {
                            root.backend.setReduceMotion(checked)
                            checked = Qt.binding(() => root.backend.reduceMotion)
                        }
                    }
                    SettingsRow {
                        objectName: "settingsLargeText"
                        heading: "Larger text"
                        detail: "Increase text size in Settings"
                        largeText: root.largeText
                        toggle: true
                        checked: root.largeText
                        enabled: root.backend !== null
                        onClicked: {
                            root.backend.setLargeText(checked)
                            checked = Qt.binding(() => root.backend.largeText)
                        }
                    }
                }

                AppearancePage {
                    id: appearancePage
                    objectName: "settingsAppearancePageHost"
                    visible: !root.searching && root.currentTab === root.appearanceTab
                    width: parent.width
                    prompts: root.promptClient
                    largeText: root.largeText
                    reduceMotion: root.backend ? root.backend.reduceMotion : false
                    x: root.cropToolOpen ? -root.width : 0
                    onChangeImageRequested: source => wallpaperSourceMenu.openAtItem(source)
                    onMessage: text => root.showMessage(text)
                }
            }
        }

        Connections {
            target: root.applicationsBackend
            function onDetailsRequested(appId) {
                root.selectTab(root.appsTab, false)
                appsPage.showApplication(appId)
            }
        }
        AppsPage {
            id: appsPage
            objectName: "settingsAppsPage"
            anchors.fill: parent
            visible: root.currentTab === root.appsTab && !root.searching
            opacity: root.tabIncomingOpacity
            backend: root.applicationsBackend
            permissions: root.permissionClient
            prompts: root.promptClient
            largeText: root.largeText
            reducedMotion: root.backend ? root.backend.reduceMotion : false
        }
        SettingsModuleHost {
            id: moduleHost
            width: paneViewport.width; height: paneViewport.height
            visible: root.moduleTabActive && !root.searching
            opacity: root.tabIncomingOpacity
            moduleId: root.moduleTabActive && !root.searching ? root.activeModuleId : ""
        }
        Image {
            id: outgoingTabFrame
            objectName: "settingsOutgoingTabFrame"
            anchors.fill: parent
            source: root.tabCapture ? root.tabCapture.url : ""
            fillMode: Image.Stretch
            smooth: true
            opacity: 0
            visible: opacity > 0
            z: 10
            MouseArea { anchors.fill: parent }
        }
        }

        AppearanceMenu {
            id: appearanceMenu
            objectName: "settingsAppearanceMenuLayer"
            parent: Overlay.overlay
            anchors.fill: parent
            page: appearancePage
        }

        MouseArea {
            parent: Overlay.overlay
            anchors.fill: parent
            z: 999
            visible: wallpaperSourceMenu.visible
            onClicked: wallpaperSourceMenu.close()
        }

        Item {
            id: wallpaperSourceMenu
            objectName: "settingsWallpaperSourceMenu"
            parent: Overlay.overlay
            z: 1000
            visible: false
            focus: visible
            property real revealProgress: 0
            property real sourceX: 0
            property real sourceY: 0
            width: Math.max(1, Math.min(444 * root.renderScale,
                                         paneViewport.width * root.renderScale - 24 * root.renderScale))
            height: 190 * root.renderScale
            function openAtItem(target) {
                if (!target || target.width <= 0)
                    return
                const overlay = Overlay.overlay
                const paneTopLeft = paneViewport.mapToItem(overlay, 0, 0)
                const paneBottomRight = paneViewport.mapToItem(overlay, paneViewport.width,
                                                               paneViewport.height)
                const buttonBottomRight = target.mapToItem(overlay,
                    target.width, target.height)
                const margin = 12 * root.renderScale
                const minimumX = paneTopLeft.x + margin
                const maximumX = paneBottomRight.x - width - margin
                const minimumY = paneTopLeft.y + margin
                const maximumY = paneBottomRight.y - height - margin
                const anchor = target.mapToItem(overlay,
                    target.pressX, target.pressY)
                const anchorX = anchor.x
                const anchorY = anchor.y
                x = Math.max(minimumX, Math.min(anchorX - width / 2, maximumX))
                y = Math.max(minimumY, Math.min(buttonBottomRight.y + 4 * root.renderScale, maximumY))
                sourceX = anchorX - x
                sourceY = anchorY - y
                revealProgress = 0
                visible = true
                menuRevealAnimation.restart()
            }
            function close() {
                menuRevealAnimation.stop()
                revealProgress = 0
                visible = false
            }
            Keys.onEscapePressed: close()
            NumberAnimation {
                id: menuRevealAnimation
                target: wallpaperSourceMenu
                property: "revealProgress"
                from: 0
                to: 1
                duration: root.backend && root.backend.reduceMotion ? 0 : 180
                easing.type: Easing.OutCubic
            }
            transform: Scale {
                origin.x: wallpaperSourceMenu.sourceX
                origin.y: wallpaperSourceMenu.sourceY
                xScale: wallpaperSourceMenu.revealProgress
                yScale: wallpaperSourceMenu.revealProgress
            }
            Rectangle {
                anchors.fill: parent
                radius: 28
                color: "#b38a8d8f"
                border.color: "#52ffffff"
                border.width: 1
                Column {
                    anchors.fill: parent
                    AbstractButton {
                        objectName: "settingsWallpaperChoosePhotos"
                        width: wallpaperSourceMenu.width
                        height: (wallpaperSourceMenu.height - 1) / 2
                        Accessible.name: qsTr("Choose from Photos")
                        onClicked: root.chooseWallpaperFile("photos")
                        background: Rectangle {
                            color: parent.down ? "#26ffffff" : "transparent"
                            radius: 24
                        }
                        contentItem: Text {
                            text: qsTr("Choose from Photos")
                            color: "white"
                            font.family: Fonts.family
                            font.weight: Font.ExtraLight
                            font.pixelSize: root.largeText ? 40 : 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Rectangle {
                        x: 24
                        width: wallpaperSourceMenu.width - 48
                        height: 1
                        color: "#7fffffff"
                    }
                    AbstractButton {
                        objectName: "settingsWallpaperChooseFiles"
                        width: wallpaperSourceMenu.width
                        height: (wallpaperSourceMenu.height - 1) / 2
                        Accessible.name: qsTr("Choose from Files")
                        onClicked: root.chooseWallpaperFile("files")
                        background: Rectangle {
                            color: parent.down ? "#26ffffff" : "transparent"
                            radius: 24
                        }
                        contentItem: Text {
                            text: qsTr("Choose from Files")
                            color: "white"
                            font.family: Fonts.family
                            font.weight: Font.ExtraLight
                            font.pixelSize: root.largeText ? 40 : 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        Popup {
            id: wallpaperCropDialog
            objectName: "settingsWallpaperCropper"
            parent: Overlay.overlay
            width: parent.width; height: parent.height
            x: 0; y: 0
            modal: true; dim: false; padding: 0
            closePolicy: Popup.NoAutoClose
            property url imageUrl
            property var imageDimensions: ({width: 0, height: 0})
            property real imageX: 0
            property real imageY: 0
            property real previousZoom: 1
            property bool cropReady: false
            readonly property bool saving: false
            readonly property real baseScale: imageDimensions.width > 0 && imageDimensions.height > 0
                ? Math.max(cropViewport.width / imageDimensions.width,
                           cropViewport.height / imageDimensions.height) : 1
            function constrain() {
                imageX = Math.max(cropViewport.width - cropImage.width, Math.min(0, imageX))
                imageY = Math.max(cropViewport.height - cropImage.height, Math.min(0, imageY))
            }
            function setZoom(value) {
                const next = Math.max(zoomSlider.from, Math.min(zoomSlider.to, value))
                const ratio = next / previousZoom
                zoomSlider.value = next
                imageX = cropViewport.width / 2 - (cropViewport.width / 2 - imageX) * ratio
                imageY = cropViewport.height / 2 - (cropViewport.height / 2 - imageY) * ratio
                previousZoom = next
                constrain()
            }
            function openImage(url) {
                const dimensions = Wallpaper.imageSize(url)
                if (dimensions.width <= 0 || dimensions.height <= 0) {
                    root.showMessage("This image cannot be opened.")
                    return
                }
                showCropImage(url, dimensions)
            }
            function showCropImage(url, dimensions) {
                imageUrl = url
                imageDimensions = dimensions
                zoomSlider.value = 1
                previousZoom = 1
                imageX = (cropViewport.width - cropImage.width) / 2
                imageY = (cropViewport.height - cropImage.height) / 2
                cropReady = false
                cropPage.x = root.width
                root.cropToolOpen = true
                open()
                if (root.backend && root.backend.reduceMotion) {
                    cropPage.x = 0
                    cropReady = true
                } else cropOpen.start()
            }
            function beginClose() {
                if (!visible || cropClose.running) return
                cropOpen.stop()
                cropReady = false
                root.cropToolOpen = false
                if (root.backend && root.backend.reduceMotion) close()
                else cropClose.start()
            }
            onClosed: {
                cropOpen.stop()
                cropClose.stop()
                cropReady = false
                root.cropToolOpen = false
                cropPage.x = root.width
            }
            NumberAnimation {
                id: cropOpen
                target: cropPage; property: "x"
                from: root.width; to: 0; duration: 300
                easing.type: Easing.OutCubic
                onFinished: wallpaperCropDialog.cropReady = true
            }
            NumberAnimation {
                id: cropClose
                target: cropPage; property: "x"
                to: root.width; duration: 260
                easing.type: Easing.InCubic
                onFinished: wallpaperCropDialog.close()
            }
            background: Item {}
            contentItem: Item {
                Rectangle {
                    id: cropPage
                    objectName: "settingsCropPage"
                    x: root.width; y: 0
                    width: parent.width; height: parent.height
                    color: "#dd27323b"
                    Item {
                        id: cropViewport
                        objectName: "settingsCropViewport"
                        height: Math.min(1108, cropPage.height - 188)
                        width: height * 768 / 1366
                        x: (parent.width - width) / 2; y: root.topInset + 47
                        clip: true
                        Rectangle { anchors.fill: parent; color: "black" }
                        Image {
                            id: cropImage
                            source: wallpaperCropDialog.imageUrl
                            x: wallpaperCropDialog.imageX; y: wallpaperCropDialog.imageY
                            width: wallpaperCropDialog.imageDimensions.width
                                   * wallpaperCropDialog.baseScale * zoomSlider.value
                            height: wallpaperCropDialog.imageDimensions.height
                                    * wallpaperCropDialog.baseScale * zoomSlider.value
                            fillMode: Image.Stretch; smooth: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: wallpaperCropDialog.cropReady
                            property real lastX: 0
                            property real lastY: 0
                            onPressed: mouse => { lastX = mouse.x; lastY = mouse.y }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                wallpaperCropDialog.imageX += mouse.x - lastX
                                wallpaperCropDialog.imageY += mouse.y - lastY
                                wallpaperCropDialog.constrain()
                                lastX = mouse.x; lastY = mouse.y
                            }
                        }
                        Repeater {
                            model: 2
                            Rectangle {
                                required property int index
                                objectName: "settingsCropGridLine"
                                x: (index + 1) * cropViewport.width / 3 - width / 2
                                width: 3; height: cropViewport.height
                                color: "white"; opacity: 0.9
                            }
                        }
                        Repeater {
                            model: 2
                            Rectangle {
                                required property int index
                                objectName: "settingsCropGridLine"
                                y: (index + 1) * cropViewport.height / 3 - height / 2
                                width: cropViewport.width; height: 3
                                color: "white"; opacity: 0.9
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"; border.color: "white"; border.width: 3
                        }
                    }
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            width: 14; height: 14; color: "#e5e5e5"
                            x: cropViewport.x + (index % 2 ? cropViewport.width : 0) - width / 2
                            y: cropViewport.y + (index > 1 ? cropViewport.height : 0) - height / 2
                        }
                    }
                    AbstractButton {
                        objectName: "settingsCropCancel"
                        x: 40; y: cropViewport.y + cropViewport.height + 31
                        width: 145; height: 55
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.beginClose()
                        background: Item {}
                        contentItem: Text {
                            text: "cancel"; color: "white"
                            font.family: Fonts.family; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Slider {
                        id: zoomSlider
                        objectName: "settingsCropZoom"
                        x: 205; y: cropViewport.y + cropViewport.height + 37
                        width: 365; height: 44
                        from: 1; to: 3; value: 1
                        enabled: wallpaperCropDialog.cropReady
                        onMoved: wallpaperCropDialog.setZoom(value)
                        background: Rectangle {
                            y: parent.height / 2 - height / 2
                            width: parent.width; height: 3; color: "white"
                        }
                        handle: Rectangle {
                            x: zoomSlider.leftPadding + zoomSlider.visualPosition
                                * (zoomSlider.availableWidth - width)
                            y: (zoomSlider.height - height) / 2
                            width: 20; height: 20; radius: 10; color: "white"
                        }
                    }
                    AbstractButton {
                        objectName: "settingsCropApply"
                        x: 614; y: cropViewport.y + cropViewport.height + 31
                        width: 115; height: 55
                        enabled: wallpaperCropDialog.cropReady && !wallpaperCropDialog.saving
                        onClicked: {
                            const factor = 768 / cropViewport.width
                            appearancePage.cropChosen(wallpaperCropDialog.imageUrl,
                                cropImage.x * factor, cropImage.y * factor,
                                cropImage.width * factor, cropImage.height * factor)
                            wallpaperCropDialog.beginClose()
                        }
                        background: Item {}
                        contentItem: Text {
                            text: wallpaperCropDialog.saving ? "saving…" : "crop"
                            color: "white"; font.family: Fonts.family; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    AbstractButton {
                        x: 191; y: cropViewport.y + cropViewport.height + 80
                        width: 46; height: 46
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.setZoom(zoomSlider.value - 0.1)
                        background: Item {}
                        contentItem: Text {
                            text: "-"; color: "white"; font.family: Fonts.family; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Text {
                        x: 338; y: cropViewport.y + cropViewport.height + 83
                        text: "zoom"; color: "white"; font.family: Fonts.family; font.pixelSize: 34
                    }
                    AbstractButton {
                        x: 532; y: cropViewport.y + cropViewport.height + 80
                        width: 46; height: 46
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.setZoom(zoomSlider.value + 0.1)
                        background: Item {}
                        contentItem: Text {
                            text: "+"; color: "white"; font.family: Fonts.family; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        visible: !wallpaperCropDialog.cropReady
                        preventStealing: true
                    }
                }
            }
        }

        Rectangle {
            visible: noticeTimer.running
            z: 100
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 70
            width: Math.min(parent.width - 80, notice.implicitWidth + 40)
            height: notice.implicitHeight + 32
            color: "#ed25313a"
            radius: 10
            Text {
                id: notice
                objectName: "settingsNotice"
                width: parent.width - 40
                anchors.centerIn: parent
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.family: Fonts.family
                font.pixelSize: 26
                color: "white"
            }
            Timer { id: noticeTimer; interval: 3000 }
        }
    }
}
