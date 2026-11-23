pragma ComponentBehavior: Bound
import QtQuick
import OpenWave.Application 1.0
import OpenWave.Controls 1.0
import OpenWave.Fonts 1.0
import OpenWave.Theme 1.0
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import "SearchCompatibility.js" as SearchCompatibility

WRApplicationWindow {
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
    property bool findOpen: false
    property bool menuClosingForTabs: false
    property bool webAvailable: false
    property var webView: null
    property string engineError: ""
    readonly property bool errorVisible: pageMode === "web" && (!webAvailable || engineError.length > 0)
    onErrorVisibleChanged: Qt.callLater(root.refreshAddress)
    readonly property bool historyActive: webView && String(webView.url) === "about:history"
    readonly property bool bookmarksActive: webView && String(webView.url) === "about:bookmarks"
    readonly property bool downloadsActive: webView && String(webView.url) === "about:downloads"
    readonly property bool nativePageActive: historyActive || bookmarksActive || downloadsActive
    readonly property bool activePrivate: activeTab >= 0 && activeTab < tabs.length && tabs[activeTab].privateTab
    property bool overviewPrivate: false
    readonly property bool privateView: overviewOpen || overviewProgress > 0 ? overviewPrivate : activePrivate
    readonly property bool shownTabsEmpty: shownModel.count === 0
    readonly property int modeTabCount: tabs.filter(tab => tab.privateTab === (overviewOpen || overviewProgress > 0 ? overviewPrivate : activePrivate)).length
    readonly property bool bookmarkable: webView !== null && pageMode === "web" && !nativePageActive
        && Bookmarks.normalized(String(webView.url)).length > 0
    readonly property bool bookmarked: bookmarkable && Bookmarks.revision >= 0 && Bookmarks.contains(String(webView.url))
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
    readonly property string startAddress: "about:overview"
    readonly property real tabHeight: tabWidth * webHost.height / webHost.width
    Component.onCompleted: {
        const session = BrowserData.session()
        const saved = BrowserPreferences.rememberTabs && !BrowserPreferences.startPrivate ? (session.tabs || []) : []
        for (let tab of saved) appendTab(false, tab.url, tab.id, tab.title)
        let selected = tabs.findIndex(tab => tab.id === session.activeId)
        if (tabs.length === 0) appendTab(BrowserPreferences.startPrivate, startPage(BrowserPreferences.startPrivate))
        activateTab(selected < 0 ? 0 : selected)
        if (initialUrl.length > 0) navigate(initialUrl)
    }

    font.family: Fonts.family

    function addressFor(text) {
        const value = text.trim()
        if (!value || value.toLowerCase() === "about:overview") return "about:overview"
        if (value.toLowerCase() === "about:private") return "about:private"
        if (/^[a-z][a-z0-9+.-]*:/i.test(value)) return value
        if (/^(localhost|\d{1,3}(\.\d{1,3}){3})(:\d+)?(\/|$)/i.test(value)) return "http://" + value
        if (/^[^\s]+\.[^\s]+$/.test(value)) return "https://" + value
        return BrowserPreferences.searchUrl(value)
    }

    ListModel { id: tabsModel }
    ListModel { id: shownModel }
    function startPage(privateTab) { return privateTab ? "about:private" : "about:overview" }
    function rebuildShown() {
        shownModel.clear()
        for (let index = 0; index < tabs.length; ++index) {
            const tab = tabs[index]
            if (tab.privateTab !== overviewPrivate) continue
            shownModel.append({tabIndex: index, tabId: tab.id, address: tab.url, label: tabLabel(tab.url),
                privateTab: tab.privateTab, preview: tab.preview})
        }
    }
    function shownIndexOf(tabIndex) {
        for (let k = 0; k < shownModel.count; ++k) if (shownModel.get(k).tabIndex === tabIndex) return k
        return -1
    }
    function setTabField(index, key, value) {
        tabsModel.setProperty(index, key, value)
        const k = shownIndexOf(index)
        if (k >= 0) shownModel.setProperty(k, key, value)
    }
    onOverviewPrivateChanged: rebuildShown()
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
                root.setTabField(index, "preview", String(source))
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
            const origin = address.match(/^https?:\/\/([^/]+)/i)
            addressInput.text = origin ? origin[1] : address
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
        closeFind()
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
        if (address === "about:bookmarks") return "Bookmarks"
        if (address === "about:downloads") return "Downloads"
        if (address === "about:private") return "Private"
        const host = address.match(/^https?:\/\/([^/]+)/i)
        return host ? host[1] : address
    }
    function appendTab(privateTab, address, id, title) {
        const tab = {id: id || ("tab-" + Date.now() + "-" + Math.floor(Math.random() * 1000000)),
            privateTab: privateTab, url: address || startPage(privateTab), title: title || "",
            view: null, preview: id && !privateTab ? String(BrowserData.previewSource(id)) : "", error: "", failedUrl: ""}
        tabs = tabs.concat([tab])
        tabsModel.append({tabId: tab.id, address: tab.url, label: tabLabel(tab.url),
            privateTab: privateTab, preview: tab.preview})
        if (privateTab === overviewPrivate) {
            shownModel.append({tabIndex: tabs.length - 1, tabId: tab.id, address: tab.url, label: tabLabel(tab.url),
                privateTab: privateTab, preview: tab.preview})
        }
        return tabs.length - 1
    }
    function ensureWebView(tab) {
        if (tab.view) return true
        try {
            const view = Qt.createQmlObject(
                'import QtQuick; import OpenWave.Web 1.0; WebView { userScripts: '
                    + JSON.stringify([SearchCompatibility.imagePaintScript])
                    + '; anchors.fill: parent; zoomLevel: parent.width / parent.cssViewportWidth; visible: false; privateBrowsing: '
                    + (tab.privateTab ? 'true' : 'false') + ' }', webHost, "OceanWPEView")
            tab.view = view
            view.webViewCreated.connect(function() {
                WebEngine.configure(view)
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
                root.setTabField(index, "address", tab.url)
                root.setTabField(index, "label", root.tabLabel(tab.url))
                if (root.activeTab === index && !addressInput.activeFocus)
                    root.refreshAddress()
                root.saveTabs()
            })
            view.titleChanged.connect(function() { tab.title = view.title; root.saveTabs() })
            view.loadingChanged.connect(function(request) {
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
        closeFind()
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
        const item = nativePageActive || errorVisible ? webHost : tab && tab.view
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
            overviewPrivate = activePrivate
            rebuildShown()
            overviewOpen = true
            pageMode = "web"
            updateTabTarget(activeTab, true)
            returningFromTabs = false
            unfoldTabs.to = 1
            unfoldTabs.start()
        } else if (action === "new" || action === "private") {
            activateTab(appendTab(action === "private", startPage(action === "private")))
            if (overviewOpen) returnToPage(activeTab)
        } else if (action === "switch") returnToPage(index)
    }
    function updateTabTarget(tabIndex, scrollToCard) {
        tabList.forceLayout()
        const index = Math.max(0, shownIndexOf(tabIndex))
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
        removeTabAt(index)
        closingTabId = ""
        const sameMode = tabs.filter(tab => tab.privateTab === overviewPrivate).length
        if (overviewOpen && tabs.length === 1) returnToPage(0)
        else if (overviewOpen && sameMode === 0 && overviewPrivate) {
            overviewPrivate = false
            rebuildShown()
        }
    }
    function removeTabAt(index) {
        const tab = tabs[index]
        BrowserData.removePreview(tab.id)
        if (tab.view) tab.view.destroy()
        const wasActive = index === activeTab
        tabs.splice(index, 1)
        tabs = tabs.slice()
        tabsModel.remove(index)
        if (tabs.length === 0) appendTab(false, startPage(false))
        let next = index < activeTab ? activeTab - 1 : activeTab
        if (wasActive) {
            let neighbour = -1
            for (let position = Math.min(index, tabs.length - 1); position >= 0 && neighbour < 0; --position)
                if (tabs[position].privateTab === tab.privateTab) neighbour = position
            for (let position = index; position < tabs.length && neighbour < 0; ++position)
                if (tabs[position].privateTab === tab.privateTab) neighbour = position
            next = neighbour >= 0 ? neighbour : Math.min(index, tabs.length - 1)
        }
        rebuildShown()
        activateTab(Math.max(0, Math.min(next, tabs.length - 1)))
    }
    function closeCurrentTab() {
        if (transitionBusy || activeTab < 0) return
        menuOpen = false
        removeTabAt(activeTab)
    }
    function closeAllTabs(privateMode) {
        if (transitionBusy) return
        menuOpen = false
        for (let index = tabs.length - 1; index >= 0; --index) {
            if (tabs[index].privateTab !== privateMode) continue
            const tab = tabs[index]
            BrowserData.removePreview(tab.id)
            if (tab.view) tab.view.destroy()
            tabs.splice(index, 1)
            tabsModel.remove(index)
        }
        tabs = tabs.slice()
        if (tabs.length === 0) appendTab(false, startPage(false))
        if (privateMode && !tabs.some(tab => tab.privateTab)) overviewPrivate = false
        rebuildShown()
        const remaining = tabs.findIndex(tab => tab.privateTab === overviewPrivate)
        activateTab(remaining >= 0 ? remaining : 0)
        if (overviewOpen && tabs.length === 1) returnToPage(0)
    }
    function bookmarkCurrent() {
        if (!bookmarkable) return
        const address = String(webView.url)
        const added = Bookmarks.toggle(address, String(webView.title))
        showNotice(Bookmarks.error.length > 0 ? Bookmarks.error : added ? qsTr("Bookmark added") : qsTr("Bookmark removed"))
    }
    function showNotice(text) {
        downloadNotice = text
        downloadNoticeTimer.restart()
    }
    function openFind() {
        if (!webView || pageMode !== "web" || nativePageActive || errorVisible) return
        menuOpen = false
        findOpen = true
        findInput.text = ""
        findInput.forceActiveFocus()
    }
    function closeFind() {
        if (!findOpen) return
        findOpen = false
        findInput.focus = false
        WebEngine.clearFind(webView)
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
        closeFind()
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
        if (findOpen) { closeFind(); return }
        if (pageMode === "web" && webView && webView.canGoBack) { webView.goBack(); return }
        if (pageMode !== "web" || (webView && String(webView.url) !== startPage(activePrivate))) {
            navigate(startPage(activePrivate)); return
        }
        root.showMinimized()
    }
    function menuAction(label) {
        if (transitionBusy) return
        const action = label.toLowerCase()
        menuClosingForTabs = false
        menuOpen = false
        if (action === "show all tabs") {
            addressInput.focus = false
            captureThen("overview")
        } else if (action === "new tab" || action === "new private tab") {
            const privateTab = action === "new private tab"
            if (overviewOpen) {
                overviewPrivate = privateTab
                rebuildShown()
                activateTab(appendTab(privateTab, startPage(privateTab)))
                returnToPage(activeTab)
            } else captureThen(privateTab ? "private" : "new")
        } else if (action === "close all tabs") {
            closeAllTabs(overviewPrivate)
        } else if (action === "search on page") {
            openFind()
        } else if (action === "add as web app" || action === "install as web app") {
            if (webView) {
                const address = String(webView.url)
                const title = String(webView.title)
                webView.runJavaScript(WebApps.iconScript, result => WebApps.install(address, title, String(result)))
            }
        } else if (action === "bookmarks") {
            overviewOpen = false
            overviewProgress = 0
            navigate("about:bookmarks")
        } else if (action === "history") {
            overviewOpen = false
            overviewProgress = 0
            navigate("about:history")
        } else if (action === "downloads") {
            overviewOpen = false
            overviewProgress = 0
            navigate("about:downloads")
        } else if (action === "settings") {
            openSettings()
        } else if (action === "forward") {
            if (webView && webView.canGoForward) webView.goForward()
        } else if (action === "reload") {
            if (webView && pageMode === "web") { if (!BrowserInput.reloadView(webView, String(webView.url))) webView.reload() }
        } else if (action === "bookmark") {
            bookmarkCurrent()
        } else if (action === "close") {
            closeCurrentTab()
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
        Rectangle {
            anchors.fill: parent
            color: root.privateView ? "#d4465566" : "#b3454e53"
            visible: wallpaper.status === Image.Ready
            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 260 } }
        }
        Rectangle { anchors.fill: parent; color: "black"; opacity: 0.22; visible: wallpaper.status === Image.Ready }

        Rectangle {
            id: page
            x: 30; y: 92; width: 718; height: 1235
            radius: 32
            color: root.overviewOpen || root.overviewProgress > 0 ? "#2e3036" : root.activePrivate ? "black" : "#202226"
            Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 260 } }
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
                readonly property real cssViewportWidth: Math.min(412, width * physicalScale)
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
                    bottomInset: bottomShade.height - 20
                    visible: root.historyActive
                    fontFamily: Fonts.family; reduceMotion: root.reducedMotion
                    onOpenPage: function(url) { root.navigate(url) }
                    onBrowserAction: function(action) { root.menuAction(action) }
                }
                BookmarksPage {
                    id: bookmarksContent
                    objectName: "browserBookmarksPage"
                    anchors.fill: parent; z: 5
                    bottomInset: bottomShade.height - 20
                    visible: root.bookmarksActive
                    fontFamily: Fonts.family; reduceMotion: root.reducedMotion
                    onOpenPage: function(url) { root.navigate(url) }
                }
                DownloadsPage {
                    id: downloadsContent
                    objectName: "browserDownloadsPage"
                    anchors.fill: parent; z: 5
                    bottomInset: bottomShade.height - 20
                    visible: root.downloadsActive
                    fontFamily: Fonts.family
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
                id: tabsHeader
                objectName: "browserTabsHeader"
                x: 0; y: 22; width: parent.width; height: 90
                visible: tabList.visible
                opacity: tabList.opacity
                readonly property real gap: 96
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: tabsHeader.gap
                    Repeater {
                        model: [{label: qsTr("Tabs"), privateList: false}, {label: qsTr("Private tabs"), privateList: true}]
                        delegate: Item {
                            id: headerTab
                            required property var modelData
                            objectName: "browserTabsHeader_" + (modelData.privateList ? "private" : "regular")
                            readonly property bool active: root.overviewPrivate === modelData.privateList
                            width: headerLabel.implicitWidth; height: 72
                            Accessible.role: Accessible.PageTab
                            Accessible.name: modelData.label
                            Accessible.selected: active
                            Text {
                                id: headerLabel
                                anchors.top: parent.top
                                text: headerTab.modelData.label
                                color: headerTab.active ? "#0aa7f0" : "white"
                                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 46
                                Behavior on color { ColorAnimation { duration: root.reducedMotion ? 0 : 160 } }
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                width: headerTab.active ? parent.width : 0; height: 3; color: "#0aa7f0"
                                Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 200; easing.type: Easing.OutCubic } }
                            }
                            TapHandler {
                                onTapped: if (!root.transitionBusy && root.overviewOpen) root.overviewPrivate = headerTab.modelData.privateList
                            }
                        }
                    }
                }
            }

            Text {
                objectName: "browserNoPrivateTabs"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 380; width: parent.width - 160
                visible: tabList.visible && root.shownTabsEmpty
                opacity: tabList.opacity
                text: root.overviewPrivate ? qsTr("No private tabs") : qsTr("No tabs")
                color: "#aab0b8"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 40
            }

            GridView {
                id: tabList
                x: 45; y: 126; width: page.width - 90; height: page.height - 126 - 120
                cellWidth: width / 2
                cellHeight: root.tabHeight + 90
                flow: GridView.FlowLeftToRight
                flickableDirection: Flickable.VerticalFlick
                model: shownModel
                clip: true
                visible: root.overviewOpen || root.overviewProgress > 0
                opacity: root.returningFromTabs ? Math.max(0, (root.overviewProgress - 0.8) * 5)
                                               : Math.max(0, (root.overviewProgress - 0.55) / 0.45)
                enabled: root.overviewOpen && !root.transitionBusy
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    id: tabCard
                    required property int index
                    required property int tabIndex
                    required property string tabId
                    required property string label
                    required property string preview
                    required property bool privateTab
                    objectName: "browserTabCard_" + tabId
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
                        radius: 22; color: tabCard.privateTab ? "black" : "#202226"
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
                        TapHandler { onTapped: root.returnToPage(tabCard.tabIndex) }
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
                        text: tabCard.label
                        elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                        color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 36
                    }
                    Rectangle {
                        x: cardImage.x + cardImage.width - 54; y: 12
                        width: 42; height: 42; radius: 21; color: "#99000000"
                        BrowserGlyph { anchors.centerIn: parent; width: 25; height: 25; kind: "close" }
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            onClicked: root.closeTab(tabCard.tabIndex)
                        }
                    }
                }
            }

            Rectangle {
                id: bottomShade
                x: 0; y: parent.height - 120
                width: parent.width; height: 120
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 0.5; color: "#66202226" }
                    GradientStop { position: 1; color: "#cc17181b" }
                }
            }

            Item {
                id: findBar
                objectName: "browserFindBar"
                x: 22; width: parent.width - 44; height: 78
                y: controls.y - height - 14 + (root.findOpen ? 0 : 30)
                opacity: root.findOpen ? 1 : 0
                visible: opacity > 0.01
                z: 8
                Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 180 } }
                Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 200; easing.type: Easing.OutCubic } }
                Rectangle { anchors.fill: parent; radius: 39; color: "#e63a3c42"; border.width: 1; border.color: "#44ffffff" }
                WRTextField {
                    id: findInput
                    objectName: "browserFindInput"
                    anchors.left: parent.left; anchors.leftMargin: 30
                    anchors.right: findCount.left; anchors.rightMargin: 10
                    height: parent.height
                    placeholderText: qsTr("Search on page")
                    placeholderTextColor: "#9aa0a8"
                    color: "white"; selectionColor: Theme.selectionColor
                    fontPixelSize: 34
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                    background: Item {}
                    onTextChanged: findDelay.restart()
                    onAccepted: WebEngine.findNext(root.webView)
                }
                Timer { id: findDelay; interval: 160; onTriggered: if (root.findOpen) WebEngine.find(root.webView, findInput.text) }
                Text {
                    id: findCount
                    objectName: "browserFindCount"
                    anchors.right: findPrevious.left; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: findInput.text.length === 0 ? "" : WebEngine.notFound ? qsTr("No matches")
                        : WebEngine.searching ? "…" : qsTr("%n match(es)", "", WebEngine.matches)
                    color: WebEngine.notFound ? "#ff9e9e" : "#c4c9d0"
                    font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 26
                }
                Item {
                    id: findPrevious
                    anchors.right: findNext.left
                    width: 58; height: parent.height
                    opacity: WebEngine.matches > 0 ? 1 : 0.4
                    BrowserGlyph { anchors.centerIn: parent; width: 32; height: 32; kind: "chevronUp" }
                    TapHandler { enabled: WebEngine.matches > 0; onTapped: WebEngine.findPrevious(root.webView) }
                }
                Item {
                    id: findNext
                    anchors.right: findClose.left
                    width: 58; height: parent.height
                    opacity: WebEngine.matches > 0 ? 1 : 0.4
                    BrowserGlyph { anchors.centerIn: parent; width: 32; height: 32; kind: "chevronDown" }
                    TapHandler { enabled: WebEngine.matches > 0; onTapped: WebEngine.findNext(root.webView) }
                }
                Item {
                    id: findClose
                    anchors.right: parent.right; anchors.rightMargin: 12
                    width: 58; height: parent.height
                    BrowserGlyph { anchors.centerIn: parent; width: 30; height: 30; kind: "close" }
                    TapHandler { onTapped: root.closeFind() }
                }
            }

            Item {
                id: controls
                visible: !root.overviewOpen && root.overviewProgress === 0
                enabled: !root.transitionBusy
                x: 0; y: parent.height - 91; width: parent.width; height: 70
                Item {
                    id: backButton
                    objectName: "browserBackButton"
                    x: 22; width: 66; height: parent.height
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Back")
                    BrowserGlyph { anchors.centerIn: parent; width: 46; height: 46; kind: "back" }
                    TapHandler { onTapped: root.goBack() }
                }
                Rectangle {
                    id: addressBar
                    objectName: "browserAddressBar"
                    x: backButton.x + backButton.width + 15
                    width: tabsButton.x - 21 - x
                    height: parent.height; radius: height / 2
                    color: "white"
                    Rectangle {
                        id: lockCap
                        objectName: "browserLockCap"
                        width: parent.height; height: parent.height; radius: height / 2
                        color: "#bdbdbd"
                        readonly property bool secure: !root.errorVisible
                            && (root.pageMode !== "web" || !root.webView || !/^http:/i.test(String(root.webView.url)))
                        Accessible.role: Accessible.Graphic
                        Accessible.name: secure ? qsTr("Secure page") : qsTr("Page is not secure")
                        BrowserGlyph {
                            anchors.centerIn: parent; width: 40; height: 40
                            kind: lockCap.secure ? "lock" : "unlock"
                            holeColor: "#bdbdbd"
                        }
                        TapHandler { onTapped: addressInput.forceActiveFocus() }
                    }
                    WRTextField {
                        id: addressInput
                        objectName: "browserAddressInput"
                        anchors.left: lockCap.right; anchors.leftMargin: 14
                        anchors.right: parent.right; anchors.rightMargin: 22
                        height: parent.height
                        text: "about:overview"
                        placeholderText: qsTr("type to search")
                        placeholderTextColor: "#9d9d9d"
                        color: "#121212"; selectionColor: Theme.selectionColor
                        fontPixelSize: 40
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: 0; rightPadding: 0
                        topPadding: 0; bottomPadding: 0
                        background: Item {}
                        onActiveFocusChanged: { root.refreshAddress(); if (activeFocus) selectAll() }
                        onAccepted: root.navigate(text)
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
                    id: tabsButton
                    objectName: "browserTabsButton"
                    x: menuButton.x - 22 - width
                    anchors.verticalCenter: parent.verticalCenter
                    width: 55; height: 54
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Show all tabs")
                    Accessible.description: qsTr("%n tab(s)", "", root.modeTabCount)
                    Rectangle {
                        anchors.fill: parent; radius: 6
                        color: "transparent"; border.width: 2; border.color: "white"
                    }
                    Text {
                        objectName: "browserTabCount"
                        anchors.centerIn: parent
                        text: root.modeTabCount
                        color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 34
                    }
                    TapHandler { onTapped: { addressInput.focus = false; root.menuAction("Show all tabs") } }
                }
                Item {
                    id: menuButton
                    objectName: "browserMenuButton"
                    x: parent.width - 22 - width
                    width: 50; height: parent.height
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Menu")
                    BrowserGlyph { anchors.centerIn: parent; width: 46; height: 46; kind: "menu" }
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
                    objectName: "browserOverviewPlus"
                    x: 22; width: 66; height: parent.height
                    Accessible.role: Accessible.Button
                    Accessible.name: root.overviewPrivate ? qsTr("New private tab") : qsTr("New tab")
                    BrowserGlyph { anchors.centerIn: parent; width: 46; height: 46; kind: "plus" }
                    TapHandler { onTapped: root.menuAction(root.overviewPrivate ? "New Private Tab" : "New Tab") }
                }
                Item {
                    objectName: "browserOverviewMenuButton"
                    x: parent.width - 22 - width
                    width: 50; height: parent.height
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Menu")
                    BrowserGlyph { anchors.centerIn: parent; width: 46; height: 46; kind: "menu" }
                    TapHandler { onTapped: root.menuOpen = !root.menuOpen }
                }
            }

            MouseArea {
                x: 0; y: 0; width: page.width; height: page.height - 86
                visible: root.menuOpen || menu.reveal > 0.001
                enabled: visible; preventStealing: true
                onClicked: root.menuOpen = false
            }

            Item {
                id: menu
                objectName: "browserMenu"
                readonly property bool overviewMenu: root.overviewOpen
                property real reveal: root.menuOpen ? 1 : 0
                onRevealChanged: if (reveal === 0) Qt.callLater(root.finishMenuClose)
                Behavior on reveal {
                    NumberAnimation {
                        duration: root.reducedMotion ? 0 : root.menuOpen ? 260 : 200
                        easing.type: root.menuOpen ? Easing.OutCubic : Easing.InCubic
                    }
                }
                readonly property var pageEntries: [
                    {label: qsTr("New tab"), action: "New tab"},
                    {label: qsTr("New Private Tab"), action: "New Private Tab", divider: true},
                    {label: qsTr("Search on page"), action: "Search on page", needsPage: true},
                    {label: qsTr("Add as web app"), action: "Add as web app", needsInstall: true, divider: true},
                    {label: qsTr("Bookmarks"), action: "Bookmarks"},
                    {label: qsTr("History"), action: "History"},
                    {label: qsTr("Downloads"), action: "Downloads"},
                    {label: qsTr("Settings"), action: "Settings"}
                ]
                readonly property var overviewEntries: [
                    {label: qsTr("New tab"), action: "New tab"},
                    {label: qsTr("New Private Tab"), action: "New Private Tab"},
                    {label: qsTr("Close all Tabs"), action: "Close all Tabs"}
                ]
                readonly property var entries: overviewMenu ? overviewEntries : pageEntries
                readonly property real rowHeight: 78
                readonly property real stripHeight: overviewMenu ? 0 : 95
                visible: root.menuOpen || reveal > 0
                enabled: root.menuOpen && reveal > 0.95 && !root.transitionBusy
                anchors.right: controls.right; anchors.rightMargin: 18
                anchors.bottom: controls.top; anchors.bottomMargin: overviewMenu ? 4 : -2
                width: overviewMenu ? 418 : 444
                height: (overviewMenu ? 22 : 52) + entries.length * rowHeight
                        + entries.filter(entry => entry.divider).length * 20 + stripHeight + (overviewMenu ? 22 : 0)
                z: 10
                opacity: Math.min(1, reveal * 2)
                transform: Scale {
                    origin.x: menu.width; origin.y: menu.height
                    xScale: Math.max(0.01, menu.reveal)
                    yScale: Math.max(0.01, menu.reveal)
                }
                MouseArea { anchors.fill: parent; onClicked: {} }
                Rectangle {
                    anchors.fill: parent; radius: 46
                    color: "#ff585a5f"
                    border.width: 1; border.color: "#33ffffff"
                }
                Rectangle {
                    objectName: "browserMenuHandle"
                    visible: !menu.overviewMenu
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 20; width: 92; height: 6; radius: 3; color: "white"
                }
                DragHandler {
                    enabled: !menu.overviewMenu
                    yAxis.enabled: true; xAxis.enabled: false
                    target: null
                    onActiveChanged: if (!active && centroid.position.y - centroid.pressPosition.y > 50) root.menuOpen = false
                }
                Column {
                    id: menuItems
                    x: 0; width: parent.width
                    y: menu.overviewMenu ? 22 : 52
                    Repeater {
                        model: menu.entries
                        delegate: Column {
                            id: menuRow
                            required property var modelData
                            readonly property bool available: {
                                if (modelData.needsInstall)
                                    return !root.overviewOpen && root.pageMode === "web" && root.webView && !root.nativePageActive
                                        && !WebApps.busy && WebApps.canInstall(String(root.webView.url))
                                if (modelData.needsPage)
                                    return !root.overviewOpen && root.pageMode === "web" && !!root.webView && !root.nativePageActive && !root.errorVisible
                                return true
                            }
                            width: menu.width
                            Item {
                                width: parent.width; height: menu.rowHeight
                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16; anchors.rightMargin: 16
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    text: menuRow.modelData.label
                                    color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 46
                                    opacity: menuRow.available ? 1 : 0.4
                                    fontSizeMode: Text.HorizontalFit; minimumPixelSize: 30
                                    font.preferTypoLineMetrics: true
                                }
                                TapHandler { enabled: menuRow.available; onTapped: root.menuAction(menuRow.modelData.action) }
                            }
                            Item {
                                visible: !!menuRow.modelData.divider
                                width: parent.width; height: 20
                                Rectangle {
                                    width: parent.width - 110; height: 2; color: "#bbffffff"
                                    anchors.centerIn: parent
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    id: menuStrip
                    objectName: "browserMenuStrip"
                    visible: !menu.overviewMenu
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: menu.stripHeight; radius: 46
                    color: "#e69b9b9b"
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: [
                                {kind: "forward", action: "Forward", name: qsTr("Forward")},
                                {kind: "reload", action: "Reload", name: qsTr("Reload")},
                                {kind: root.bookmarked ? "starFilled" : "star", action: "Bookmark", name: root.bookmarked ? qsTr("Remove bookmark") : qsTr("Add bookmark")},
                                {kind: "close", action: "Close", name: qsTr("Close tab")}
                            ]
                            delegate: Item {
                                id: stripButton
                                required property var modelData
                                objectName: "browserMenuStrip_" + modelData.action.toLowerCase()
                                width: menuStrip.width / 4; height: menuStrip.height
                                readonly property bool available: modelData.action === "Forward"
                                        ? !!root.webView && root.pageMode === "web" && root.webView.canGoForward
                                    : modelData.action === "Reload" ? !!root.webView && root.pageMode === "web" && !root.overviewOpen
                                    : modelData.action === "Bookmark" ? root.bookmarkable
                                    : true
                                opacity: available ? 1 : 0.4
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name
                                BrowserGlyph {
                                    anchors.centerIn: parent; width: 44; height: 44
                                    kind: stripButton.modelData.kind
                                }
                                TapHandler { enabled: stripButton.available; onTapped: root.menuAction(stripButton.modelData.action) }
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
        target: WebApps
        function onCreated(appId, name) { root.downloadNotice = qsTr("Created %1").arg(name); downloadNoticeTimer.restart() }
        function onErrorOccurred(message) { root.downloadNotice = message; downloadNoticeTimer.restart() }
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
