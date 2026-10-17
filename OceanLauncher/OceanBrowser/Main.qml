pragma ComponentBehavior: Bound
import QtQuick
import OpenWave.Fonts 1.0
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import "SearchCompatibility.js" as SearchCompatibility

ApplicationWindow {
    id: root
    width: 768; height: 1366
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "Ocean Browser"
    color: "#414d58"

    property string pageMode: "web"
    property bool settingsOpen: false
    property string initialUrl: ""
    property bool menuOpen: false
    property bool menuClosingForTabs: false
    property bool webAvailable: false
    property var webView: null
    property string engineError: ""
    readonly property bool errorVisible: pageMode === "web" && (!webAvailable || engineError.length > 0)
    onErrorVisibleChanged: Qt.callLater(root.refreshAddress)
    readonly property bool historyActive: webView && String(webView.url) === "about:history"
    property string downloadNotice: ""
    property string contextImage: ""
    property string contextLink: ""
    readonly property real contentScale: Math.min(width / 768, height / 1366)
    property var tabs: []
    property int activeTab: -1
    property real overviewProgress: 0
    property bool overviewOpen: false
    property string closingTabId: ""
    property bool returningFromTabs: false
    property string transitionPreview: ""
    property real tabTargetX: 62
    property real tabTargetY: 50
    property string pendingCapture: ""
    property string pendingAction: ""
    property int pendingIndex: -1
    readonly property bool transitionBusy: menuClosingForTabs || unfoldTabs.running
                                          || pendingAction.length > 0 || closingTabId.length > 0
    readonly property bool reducedMotion: BrowserData.reduceMotion
    readonly property real tabWidth: tabList.cellWidth - 34
    readonly property real tabHeight: tabWidth * webHost.height / webHost.width
    Component.onCompleted: {
        const session = BrowserData.session()
        const saved = BrowserPreferences.rememberTabs && !BrowserPreferences.startPrivate ? (session.tabs || []) : []
        for (let tab of saved) appendTab(false, tab.url, tab.id, tab.title)
        let selected = tabs.findIndex(tab => tab.id === session.activeId)
        if (tabs.length === 0) appendTab(BrowserPreferences.startPrivate, "about:overview")
        activateTab(selected < 0 ? 0 : selected)
        if (initialUrl.length > 0) navigate(initialUrl)
    }

    font.family: Fonts.family

    function addressFor(text) {
        const value = text.trim()
        if (!value || value.toLowerCase() === "about:overview") return "about:overview"
        if (/^[a-z][a-z0-9+.-]*:/i.test(value)) return value
        if (/^(localhost|\d{1,3}(\.\d{1,3}){3})(:\d+)?(\/|$)/i.test(value)) return "http://" + value
        if (/^[^\s]+\.[^\s]+$/.test(value)) return "https://" + value
        return BrowserPreferences.searchUrl(value)
    }

    ListModel { id: tabsModel }
    Timer {
        id: captureTimeout
        interval: 1500
        onTriggered: root.finishCapture()
    }
    Connections {
        target: BrowserData
        function onPreviewReady(id, source) {
            const index = root.tabs.findIndex(tab => tab.id === id)
            if (index >= 0 && String(source).length) {
                root.tabs[index].preview = String(source)
                tabsModel.setProperty(index, "preview", String(source))
            }
            if (id === root.pendingCapture) root.finishCapture()
        }
        function onStorageError(message) { root.engineError = message }
    }

    function saveTabs() {
        BrowserData.saveSession(BrowserPreferences.rememberTabs ? tabs.map(tab => ({id: tab.id, url: tab.url,
            title: tab.title, privateTab: tab.privateTab})) : [],
            activeTab >= 0 ? tabs[activeTab].id : "")
    }
    function refreshAddress() {
        const address = activeTab >= 0 ? tabs[activeTab].failedUrl || tabs[activeTab].url : "about:overview"
        if (errorVisible && !addressInput.activeFocus) { addressInput.text = ""; return }
        if (addressInput.activeFocus || BrowserPreferences.alwaysShowFullUrl)
            addressInput.text = address
        else {
            const origin = address.match(/^https?:\/\/[^/]+/i)
            addressInput.text = origin ? origin[0] : address
        }
        if (!addressInput.activeFocus) addressInput.cursorPosition = 0
    }
    function retryNavigation() {
        if (transitionBusy || activeTab < 0) return
        menuOpen = false
        addressInput.focus = false
        if (!webAvailable) { activateTab(activeTab); return }
        const address = tabs[activeTab].failedUrl || tabs[activeTab].url
        if (!BrowserInput.reloadView(webView, address)) webView.reload()
    }
    function submitAddress() {
        if (errorVisible && !addressInput.activeFocus) retryNavigation()
        else navigate(addressInput.activeFocus ? addressInput.text : String(webView ? webView.url : "about:overview"))
    }
    function openSettings() {
        menuOpen = false
        addressInput.focus = false
        overviewOpen = false
        overviewProgress = 0
        pageMode = "web"
        settingsOpen = true
    }
    Connections {
        target: BrowserPreferences
        function onChanged() { root.saveTabs(); if (!addressInput.activeFocus) root.refreshAddress() }
    }
    function tabLabel(address) {
        if (address === "about:overview") return "Overview"
        if (address === "about:history") return "History"
        const host = address.match(/^https?:\/\/([^/]+)/i)
        return host ? host[1] : address
    }
    function appendTab(privateTab, address, id, title) {
        const tab = {id: id || ("tab-" + Date.now() + "-" + Math.floor(Math.random() * 1000000)),
            privateTab: privateTab, url: address || "about:overview", title: title || "",
            view: null, preview: id && !privateTab ? String(BrowserData.previewSource(id)) : "", error: "", failedUrl: ""}
        tabs = tabs.concat([tab])
        tabsModel.append({tabId: tab.id, address: tab.url, label: tabLabel(tab.url),
            privateTab: privateTab, preview: tab.preview})
        return tabs.length - 1
    }
    function ensureWebView(tab) {
        if (tab.view) return true
        try {
            const view = Qt.createQmlObject(
                'import QtQuick; import org.wpewebkit.qtwpe 1.0; WPEView { property bool navigationLoading: false; userScripts: '
                    + JSON.stringify([SearchCompatibility.imagePaintScript])
                    + '; anchors.fill: parent; zoomLevel: parent.width / parent.cssViewportWidth; visible: false; privateBrowsing: '
                    + (tab.privateTab ? 'true' : 'false') + ' }', webHost, "OceanWPEView")
            tab.view = view
            view.webViewCreated.connect(function() {
                BrowserInput.attachView(view)
                Cookies.attachView(view, tab.privateTab)
                Downloads.attachView(view, tab.privateTab)
                if (root.tabs[root.activeTab] === tab) Downloads.setActiveView(view)
            })
            if (view.privateBrowsing !== tab.privateTab)
                throw new Error("The tab session was created with the wrong privacy mode")
            view.urlChanged.connect(function() {
                const index = root.tabs.findIndex(entry => entry.id === tab.id)
                if (index < 0) return
                tab.url = String(view.url)
                tabsModel.setProperty(index, "address", tab.url)
                tabsModel.setProperty(index, "label", root.tabLabel(tab.url))
                if (root.activeTab === index && !addressInput.activeFocus)
                    root.refreshAddress()
                root.saveTabs()
            })
            view.titleChanged.connect(function() { tab.title = view.title; root.saveTabs() })
            view.loadingChanged.connect(function(request) {
                if (request.status === 0) view.navigationLoading = true
                else if (request.status === 1 || request.status === 2 || request.status === 3)
                    view.navigationLoading = false
                tab.error = request.status === 3 ? request.errorString || qsTr("Unable to load this page.") : ""
                tab.failedUrl = request.status === 3 ? String(request.url || tab.url) : ""
                if (root.tabs[root.activeTab] === tab) root.engineError = tab.error
                if (request.status === 3) console.warn("Ocean Browser load failed: " + request.errorString)
                if (request.status === 2 && !request.errorString && BrowserPreferences.rememberHistory) {
                    const address = String(view.url)
                    const token = History.recordVisit(address, String(view.title), tab.privateTab)
                    if (token.length) view.runJavaScript("JSON.stringify({url:location.href,title:document.title,icon:(document.querySelector('link[rel~=icon]')||{}).href||location.origin+'/favicon.ico'})", function(result) {
                        try {
                            const metadata = JSON.parse(String(result))
                            History.setVisitDetails(token, metadata.title, metadata.icon, metadata.url)
                        } catch (error) { /* A missing favicon uses the default webpage icon. */ }
                    })
                }
            })
            view.url = tab.url
        } catch (error) {
            console.warn("Ocean WPE view: " + error)
            if (tab.view) { tab.view.destroy(); tab.view = null }
            tab.error = qsTr("WPEWebKit is not available.")
            return false
        }
        return true
    }
    function activateTab(index) {
        if (index < 0 || index >= tabs.length) return
        for (let tab of tabs) if (tab.view) tab.view.visible = false
        activeTab = index
        const tab = tabs[index]
        Downloads.setActiveView(tab.view)
        webAvailable = ensureWebView(tab)
        webView = tab.view
        if (webView) webView.visible = true
        engineError = tab.error
        root.refreshAddress()
        pageMode = "web"
        saveTabs()
    }
    function captureThen(action, index) {
        if (transitionBusy) return
        pendingAction = action
        pendingIndex = index === undefined ? -1 : index
        const tab = tabs[activeTab]
        pendingCapture = tab ? tab.id : ""
        const item = historyActive || errorVisible ? webHost : tab && tab.view
        if (tab && item && item.visible
                && BrowserData.capturePreview(item, tab.id, tab.privateTab))
            captureTimeout.restart()
        else finishCapture()
    }
    function finishCapture() {
        captureTimeout.stop()
        const action = pendingAction
        const index = pendingIndex
        pendingAction = ""
        pendingCapture = ""
        pendingIndex = -1
        if (action === "overview") {
            overviewOpen = true
            pageMode = "web"
            updateTabTarget(activeTab, true)
            returningFromTabs = false
            unfoldTabs.to = 1
            unfoldTabs.start()
        } else if (action === "new" || action === "private") {
            activateTab(appendTab(action === "private", "about:overview"))
            if (overviewOpen) returnToPage(activeTab)
        } else if (action === "switch") returnToPage(index)
    }
    function updateTabTarget(index, scrollToCard) {
        tabList.forceLayout()
        if (scrollToCard || !tabList.itemAtIndex(index))
            tabList.positionViewAtIndex(index, GridView.Beginning)
        const card = tabList.itemAtIndex(index)
        const columnX = card ? card.x : (index % 2) * tabList.cellWidth
        const rowY = card ? card.y : Math.floor(index / 2) * tabList.cellHeight
        tabTargetX = tabList.x + columnX + (tabList.cellWidth - tabWidth) / 2 - tabList.contentX
        tabTargetY = tabList.y + rowY - tabList.contentY
    }
    function returnToPage(index) {
        if (index < 0 || index >= tabs.length || unfoldTabs.running) return
        updateTabTarget(index, false)
        transitionPreview = tabs[index].preview
        activateTab(index)
        returningFromTabs = true
        unfoldTabs.to = 0
        unfoldTabs.start()
    }
    function closeTab(index) {
        if (transitionBusy || index < 0 || index >= tabs.length) return
        // Keep the model and live view until the card has finished disappearing.
        closingTabId = tabs[index].id
    }
    function finishCloseTab(id) {
        if (id !== closingTabId) return
        const index = tabs.findIndex(tab => tab.id === id)
        if (index < 0) { closingTabId = ""; return }
        const tab = tabs[index]
        BrowserData.removePreview(tab.id)
        if (tab.view) tab.view.destroy()
        tabs.splice(index, 1)
        tabsModel.remove(index)
        if (tabs.length === 0) appendTab(false, "about:overview")
        activateTab(Math.max(0, Math.min(index < activeTab ? activeTab - 1 : activeTab, tabs.length - 1)))
        closingTabId = ""
        if (tabs.length === 1 && overviewOpen) returnToPage(0)
    }
    NumberAnimation {
        id: unfoldTabs
        target: root; property: "overviewProgress"
        duration: root.reducedMotion ? 0 : 330
        easing.type: Easing.InOutCubic
        onFinished: {
            root.overviewOpen = to === 1
            root.returningFromTabs = false
            root.transitionPreview = ""
        }
    }
    function navigate(input) {
        const address = addressFor(input)
        if (address.toLowerCase() === "about:settings") { openSettings(); return }
        settingsOpen = false
        menuOpen = false
        addressInput.focus = false
        pageMode = "web"
        if (activeTab < 0) activateTab(appendTab(false, address))
        if (webView) {
            webView.visible = true
            if (activeTab >= 0 && tabs[activeTab].failedUrl === address
                    && BrowserInput.reloadView(webView, address)) return
            webView.url = address
        }
    }
    function goBack() {
        if (transitionBusy) return
        if (settingsOpen) { settingsOpen = false; return }
        if (menuOpen) { menuOpen = false; return }
        if (overviewOpen) { returnToPage(activeTab); return }
        if (pageMode === "downloads") { pageMode = "web"; return }
        if (pageMode === "web" && webView && webView.canGoBack) { webView.goBack(); return }
        if (pageMode !== "web" || (webView && String(webView.url) !== "about:overview")) {
            navigate("about:overview"); return
        }
        Qt.quit()
    }
    function menuAction(label) {
        if (transitionBusy) return
        // The menu reverses its opening scale before the page moves into its card.
        menuClosingForTabs = label === "Show all tabs" && !overviewOpen
        menuOpen = false
        if (label === "Show all tabs") {
            addressInput.focus = false
        } else if (label === "New Tab" || label === "New Private Tab") {
            if (overviewOpen) {
                activateTab(appendTab(label === "New Private Tab", "about:overview"))
                returnToPage(activeTab)
            } else captureThen(label === "New Private Tab" ? "private" : "new")
        } else if (label === "History") {
            overviewOpen = false
            overviewProgress = 0
            navigate("about:history")
        } else if (label === "Settings") {
            openSettings()
        } else {
            pageMode = label.toLowerCase()
            overviewOpen = false
            overviewProgress = 0
        }
    }
    function finishMenuClose() {
        if (!menuClosingForTabs || menuOpen || menu.reveal !== 0) return
        menuClosingForTabs = false
        captureThen("overview")
    }

    Shortcut { sequences: ["Escape", "Back"]; onActivated: root.goBack() }

    Item {
        id: screen
        width: 768; height: 1366
        x: Math.round((root.width - width * scale) / 2)
        y: Math.round((root.height - height * scale) / 2)
        scale: root.contentScale
        transformOrigin: Item.TopLeft
        clip: true

        Rectangle { anchors.fill: parent; color: "black" }
        Image {
            id: wallpaper
            anchors.fill: parent
            source: Wallpaper.source
            fillMode: Image.PreserveAspectCrop
            smooth: true; mipmap: true
        }
        MultiEffect {
            anchors.fill: parent
            source: wallpaper
            visible: wallpaper.status === Image.Ready
            blurEnabled: true; blurMax: 64; blur: 1
            autoPaddingEnabled: false
        }
        Rectangle { anchors.fill: parent; color: "#b3454e53"; visible: wallpaper.status === Image.Ready }
        Rectangle { anchors.fill: parent; color: "black"; opacity: 0.22; visible: wallpaper.status === Image.Ready }

        Rectangle {
            id: page
            x: 30; y: 92; width: 718; height: 1235
            radius: 32
            color: "white"
            clip: true
            enabled: !root.settingsOpen
            layer.enabled: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: pageMask }

            Item {
                id: webHost
                x: 1; width: parent.width - 2
                anchors.top: parent.top; anchors.topMargin: 1
                anchors.bottom: bottomShade.top
                readonly property real physicalScale: Math.min(root.Screen.width / root.width,
                                                               root.Screen.height / root.height)
                readonly property real cssViewportWidth: Math.min(500, width * physicalScale)
                visible: root.pageMode === "web"
                         && (root.overviewProgress < 1 || root.returningFromTabs)
                enabled: !root.transitionBusy && !root.overviewOpen
                transform: [
                    Scale {
                        origin.x: 0; origin.y: 0
                        xScale: 1 + (root.tabWidth / webHost.width - 1) * root.overviewProgress
                        yScale: xScale
                    },
                    Translate {
                        x: (root.tabTargetX - webHost.x) * root.overviewProgress
                        y: (root.tabTargetY - webHost.y) * root.overviewProgress
                    }
                ]
                z: root.overviewProgress > 0 ? 3 : 0
                HistoryPage {
                    id: historyContent
                    anchors.fill: parent; z: 5
                    visible: root.historyActive
                    fontFamily: Fonts.family; reduceMotion: root.reducedMotion
                    onOpenPage: function(url) { root.navigate(url) }
                    onBrowserAction: function(action) {
                        if (action === "Show all tabs") root.captureThen("overview")
                        else root.menuAction(action)
                    }
                }
                Image {
                    anchors.fill: parent
                    source: root.transitionPreview
                    visible: root.returningFromTabs && source.toString().length > 0
                    opacity: Math.min(1, root.overviewProgress * 2)
                    fillMode: Image.Stretch
                    z: 10
                    layer.enabled: true
                    layer.effect: MultiEffect { blurEnabled: true; blurMax: 32; blur: 0.6 }
                }
                BrowserErrorPage {
                    anchors.fill: parent; z: 20
                    visible: root.errorVisible
                    message: root.engineError
                    fontFamily: Fonts.family
                }
                clip: true
            }

            Item {
                visible: ["about", "unavailable"].includes(root.pageMode)
                anchors.fill: parent
                Text {
                    id: messageTitle
                    x: 45; y: 65; width: parent.width - 90
                    text: root.pageMode === "downloads" ? "Downloads"
                        : root.pageMode === "about" ? "About Ocean Browser" : "Private tabs"
                    color: "#222"; font.family: Fonts.family; font.pixelSize: 48
                }
                Text {
                    id: messageBody
                    x: 45; y: 150; width: parent.width - 90
                    text: root.pageMode === "about" ? "Ocean Browser uses WPEWebKit and JavaScriptCore."
                        : root.pageMode === "downloads" ? "No downloads yet"
                        : "Private browsing needs an isolated WPEWebKit session."
                    wrapMode: Text.WordWrap
                    color: "#555"; font.family: Fonts.family; font.pixelSize: 30
                }
            }

            DownloadsPage {
                anchors.fill: parent
                visible: root.pageMode === "downloads" && !root.overviewOpen
                fontFamily: Fonts.family
            }

            GridView {
                id: tabList
                x: 45; y: 50; width: page.width - 90; height: page.height - 160
                cellWidth: width / 2
                cellHeight: root.tabHeight + 90
                flow: GridView.FlowLeftToRight
                flickableDirection: Flickable.VerticalFlick
                model: tabsModel
                clip: true
                visible: root.overviewOpen || root.overviewProgress > 0
                opacity: root.returningFromTabs ? Math.max(0, (root.overviewProgress - 0.8) * 5)
                                               : Math.max(0, (root.overviewProgress - 0.55) / 0.45)
                enabled: root.overviewOpen && !root.transitionBusy
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    id: tabCard
                    required property int index
                    required property string tabId
                    required property string label
                    required property string preview
                    required property bool privateTab
                    width: tabList.cellWidth; height: tabList.cellHeight
                    readonly property bool closing: root.closingTabId === tabId
                    transformOrigin: Item.Center
                    onClosingChanged: if (closing) closeTabAnimation.start()
                    ParallelAnimation {
                        id: closeTabAnimation
                        NumberAnimation {
                            target: tabCard; property: "scale"; to: 0
                            duration: root.reducedMotion ? 0 : 220; easing.type: Easing.InCubic
                        }
                        NumberAnimation {
                            target: tabCard; property: "opacity"; to: 0
                            duration: root.reducedMotion ? 0 : 220; easing.type: Easing.InCubic
                        }
                        onFinished: root.finishCloseTab(tabCard.tabId)
                    }
                    Rectangle {
                        id: cardImage
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.tabWidth; height: root.tabHeight
                        radius: 22; color: "#eeeeee"
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            maskEnabled: true; maskSource: cardMask
                        }
                        Image {
                            anchors.fill: parent
                            source: tabCard.preview
                            fillMode: Image.Stretch; smooth: true; cache: false
                            layer.enabled: true
                            layer.effect: MultiEffect { blurEnabled: true; blurMax: 32; blur: 0.6 }
                        }
                        Image {
                            anchors.centerIn: parent; width: 120; height: 120
                            visible: !tabCard.preview.length
                            source: "assets/globe.svg"
                        }
                        TapHandler { onTapped: root.returnToPage(tabCard.index) }
                    }
                    Rectangle {
                        id: cardMask
                        width: cardImage.width; height: cardImage.height; radius: cardImage.radius
                        visible: false; layer.enabled: true
                    }
                    Text {
                        anchors.top: cardImage.bottom; anchors.topMargin: 10
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 20
                        text: (tabCard.privateTab ? "Private · " : "") + tabCard.label
                        elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                        color: "#111"; font.family: Fonts.family; font.pixelSize: 32
                    }
                    Rectangle {
                        x: cardImage.x + cardImage.width - 54; y: 12
                        width: 42; height: 42; radius: 21; color: "#cc454e53"
                        BrowserGlyph { anchors.centerIn: parent; width: 25; height: 25; kind: "close" }
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            onClicked: root.closeTab(tabCard.index)
                        }
                    }
                }
            }

            Rectangle {
                id: bottomShade
                x: 0; y: parent.height - 120
                width: parent.width; height: 120
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00ffffff" }
                    GradientStop { position: 0.54; color: "#747474" }
                    GradientStop { position: 1; color: "#424047" }
                }
            }

            Item {
                id: controls
                visible: !root.overviewOpen && root.overviewProgress === 0
                enabled: !root.transitionBusy
                x: 14; y: parent.height - 90; width: parent.width - 28; height: 70
                Item {
                    id: backButton
                    anchors.left: parent.left
                    width: 65; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 43; height: 43; kind: "back" }
                    TapHandler { onTapped: root.goBack() }
                }
                Item {
                    id: forwardButton
                    anchors.left: backButton.right; anchors.leftMargin: 18
                    width: 63; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 43; height: 43; kind: "forward" }
                    opacity: root.webView && root.pageMode === "web" && root.webView.canGoForward ? 1 : 0.65
                    TapHandler { onTapped: if (root.webView && root.webView.canGoForward) root.webView.goForward() }
                }
                Rectangle {
                    id: addressBar
                    anchors.left: forwardButton.right
                    anchors.right: menuButton.left; anchors.rightMargin: 9
                    height: parent.height; radius: 32
                    color: "white"
                    TextField {
                        id: addressInput
                        anchors.left: parent.left; anchors.leftMargin: 14
                        anchors.right: searchButton.left; anchors.rightMargin: 3
                        height: parent.height
                        text: "about:overview"
                        placeholderText: qsTr("type to search")
                        placeholderTextColor: "#9d9d9d"
                        color: "#121212"; selectionColor: "#8fd5e4"
                        font.family: Fonts.family; font.pixelSize: 32
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: 0; rightPadding: 0
                        topPadding: 0; bottomPadding: 0
                        background: Item {}
                        onActiveFocusChanged: root.refreshAddress()
                        onAccepted: root.navigate(text)
                    }
                    Item {
                        id: searchButton
                        Accessible.role: Accessible.Button
                        Accessible.name: root.errorVisible && !addressInput.activeFocus ? qsTr("Reload page") : qsTr("Search or open address")
                        Accessible.onPressAction: root.submitAddress()
                        anchors.right: parent.right; anchors.rightMargin: 9
                        width: 62; height: parent.height
                        BrowserGlyph {
                            anchors.centerIn: parent
                            width: 42; height: 42
                            kind: root.errorVisible && !addressInput.activeFocus ? "reload" : "search"
                            strokeColor: "black"
                        }
                        TapHandler { onTapped: root.submitAddress() }
                    }
                    AddressLoadingBorder {
                        anchors.fill: parent
                        loading: root.webView !== null && root.webView.navigationLoading
                        navigationSession: root.webView
                        windowPresented: root.visible && root.visibility !== Window.Minimized
                                         && root.visibility !== Window.Hidden
                        reducedMotion: root.reducedMotion
                        cornerRadius: addressBar.radius
                        renderScale: root.contentScale * root.Screen.devicePixelRatio
                    }
                }
                Item {
                    id: menuButton
                    anchors.right: parent.right; anchors.rightMargin: 12
                    width: 57; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 45; height: 45; kind: "menu" }
                    TapHandler { onTapped: {
                        addressInput.focus = false
                        if (root.historyActive) historyContent.openMenu()
                        else root.menuOpen = !root.menuOpen
                    } }
                }
            }

            Item {
                x: controls.x; y: controls.y; width: controls.width; height: controls.height
                visible: root.overviewOpen && root.overviewProgress === 1
                enabled: !root.transitionBusy
                Item {
                    anchors.left: parent.left
                    width: backButton.width; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 45; height: 45; kind: "plus" }
                    TapHandler { onTapped: root.menuAction("New Tab") }
                }
                Item {
                    anchors.right: parent.right; anchors.rightMargin: 12
                    width: menuButton.width; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 45; height: 45; kind: "menu" }
                    TapHandler { onTapped: root.menuOpen = !root.menuOpen }
                }
            }

            MouseArea {
                x: 0; y: 0; width: page.width; height: page.height - 86
                visible: root.menuOpen || menu.reveal > 0.001
                enabled: visible; preventStealing: true
                onClicked: root.menuOpen = false
            }

            Rectangle {
                id: menu
                property real reveal: root.menuOpen ? 1 : 0
                onRevealChanged: if (reveal === 0) Qt.callLater(root.finishMenuClose)
                Behavior on reveal {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : root.menuOpen ? 260 : 200
                        easing.type: root.menuOpen ? Easing.OutCubic : Easing.InCubic
                    }
                }
                visible: root.menuOpen || reveal > 0
                enabled: root.menuOpen && reveal > 0.95 && !root.transitionBusy
                anchors.right: controls.right; anchors.rightMargin: 12
                anchors.bottom: controls.top; anchors.bottomMargin: 24
                width: 335; height: menuItems.implicitHeight + 38
                z: 10
                radius: 42
                color: "#b7b7b7"
                opacity: Math.min(1, reveal * 2)
                transform: Scale {
                    origin.x: menu.width; origin.y: menu.height
                    xScale: Math.max(0.01, menu.reveal)
                    yScale: Math.max(0.01, menu.reveal)
                }
                MouseArea { anchors.fill: parent; onClicked: {} }
                Column {
                    id: menuItems
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: ["Show all tabs", "New Tab", "New Private Tab", "History", "Downloads", "Settings", "About"]
                        delegate: Column {
                            id: menuRow
                            required property string modelData
                            width: menu.width
                            Item {
                                width: parent.width; height: 70
                                Text {
                                    anchors.fill: parent
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    text: menuRow.modelData
                                    color: "white"; font.family: Fonts.family; font.pixelSize: 43
                                    font.preferTypoLineMetrics: true
                                }
                                TapHandler { onTapped: root.menuAction(menuRow.modelData) }
                            }
                            Item {
                                visible: menuRow.modelData === "New Private Tab"
                                width: parent.width; height: 20
                                Rectangle {
                                    width: parent.width - 54; height: 2; color: "#ddd"
                                    anchors.centerIn: parent
                                }
                            }
                        }
                    }
                }
            }
        }
        ShaderEffectSource {
            id: settingsBackdrop
            sourceItem: page
            sourceRect: Qt.rect(-13, -28, 733, 1276)
            live: root.settingsOpen
            visible: false
        }
        BrowserSettingsPage {
            x: 17; y: 64; width: 733; height: 1276
            visible: root.settingsOpen
            fontFamily: Fonts.family; reduceMotion: root.reducedMotion
            backdrop: settingsBackdrop
            onClosed: root.settingsOpen = false
        }
        Rectangle {
            id: pageMask
            width: page.width; height: page.height; radius: page.radius
            visible: false; layer.enabled: true
        }
    }

    Connections {
        target: Downloads
        function onCompleted(filename) { root.downloadNotice = qsTr("Downloaded %1").arg(filename); downloadNoticeTimer.restart() }
        function onContextMenuRequested(imageUrl, linkUrl) {
            root.contextImage = imageUrl; root.contextLink = linkUrl; resourceMenu.open()
        }
    }
    Timer { id: downloadNoticeTimer; interval: 4500; onTriggered: root.downloadNotice = "" }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - 145 * root.contentScale
        width: Math.min(parent.width - 40, noticeText.implicitWidth + 30); height: 44 * root.contentScale
        radius: 12; color: "#df414d58"; visible: root.downloadNotice.length > 0; z: 100
        Text { id: noticeText; anchors.centerIn: parent; text: root.downloadNotice; color: "white"; font.family: Fonts.family; font.pixelSize: 24 * root.contentScale }
    }
    Popup {
        id: resourceMenu
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 335 * root.contentScale)
        padding: 16 * root.contentScale; modal: true; dim: false; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#dfb7b7b7"; radius: 32 * root.contentScale }
        contentItem: Column {
            Repeater {
                model: (root.contextImage.length ? [qsTr("Save Image"), qsTr("Save Image As…")] : [])
                    .concat(root.contextLink.length ? [qsTr("Save Link As…")] : []).concat([qsTr("Save As…")])
                delegate: Item {
                    required property string modelData
                    width: resourceMenu.availableWidth; height: 60 * root.contentScale
                    Text { anchors.centerIn: parent; text: parent.modelData; color: "white"; font.family: Fonts.family; font.pixelSize: 32 * root.contentScale }
                    TapHandler { onTapped: {
                        resourceMenu.close()
                        if (modelData === qsTr("Save Image")) Downloads.saveResource(root.contextImage, true, false)
                        else if (modelData === qsTr("Save Image As…")) Downloads.saveResource(root.contextImage, true, true)
                        else if (modelData === qsTr("Save Link As…")) Downloads.saveResource(root.contextLink, false, true)
                        else Downloads.savePageAs()
                    } }
                }
            }
        }
    }
}
