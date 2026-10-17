import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import OpenWave.Shell 1.0
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell

ApplicationWindow {
    id: root

    // Preserve the portrait design on EGLFS and in resizable desktop previews.
    readonly property real contentScale: Math.min(width / 768, height / 1366)
    property var appController: null
    property var quickMenuBackend: null
    property var portalBackend: null
    property var promptBackend: null
    property var portalFilesBackend: null
    property var timeBackend: null
    property list<string> installedApps: appController ? appController.installedApps : []
    readonly property var favoriteApps: appController ? appController.favoriteApps : []
    readonly property var unpinnedApps: appController ? appController.unpinnedApps : []
    property bool resumeDrawer: false
    property string startupError: ""
    property string currentTime: Qt.formatTime(new Date(), "h:mm AP")
    property string currentDate: Qt.formatDate(new Date(), "MMM d, yyyy - dddd")
    property var foregroundSurface: null
    property var launchSourceSurface: null
    property var appSurfaces: ({})
    property bool applicationVisible: false
    property bool transitionBusy: false
    property var exitingSurface: null
    property string exitingAppId: ""
    property real exitProgress: 0
    property real dismissalGlow: 0
    property string pendingTerminationAppId: ""
    property string pendingVisualLaunch: ""
    property bool terminationProcessExited: false
    property bool terminationSurfaceGone: false
    readonly property var appArtwork: appController ? appController.appArtwork : ({})
    readonly property bool reduceMotion: appController ? appController.reduceMotion : false
    readonly property bool appLoading: loadingView.active
    readonly property bool promptModal: systemPrompt.visible
        || (promptBackend && promptBackend.request.id !== undefined)
    readonly property var displayedPromptRequest: systemPrompt.shownRequest.id
        ? systemPrompt.shownRequest : promptBackend ? promptBackend.request : ({})
    readonly property var promptOwnerSurface: {
        const request = displayedPromptRequest
        const item = appSurfaces[request.applicationId]
        return item && item.surface && item.surface.client.processId === request.ownerPid ? item : null
    }
    function presentPromptOwner() {
        const request = promptBackend ? promptBackend.request : ({})
        if (!request.id || !request.applicationId) return
        // Keep the previous owner visible until its closing animation finishes.
        if (systemPrompt.visible && systemPrompt.shownRequest.id !== request.id) return
        const item = appSurfaces[request.applicationId]
        if (item && item.surface && item.surface.hasContent && item.surface.client.processId === request.ownerPid
            && (!applicationVisible || foregroundSurface !== item) && !transitionBusy && !appLoading)
            presentApplication(item)
    }
    onAppSurfacesChanged: presentPromptOwner()
    onTransitionBusyChanged: { presentPromptOwner(); Qt.callLater(root.presentPendingLaunch) }
    onAppLoadingChanged: { presentPromptOwner(); Qt.callLater(root.presentPendingLaunch) }
    onPendingTerminationAppIdChanged: Qt.callLater(root.presentPendingLaunch)
    function presentPendingLaunch() {
        if (!pendingVisualLaunch || appLoading || transitionBusy || pendingTerminationAppId) return
        const appId = pendingVisualLaunch
        pendingVisualLaunch = ""
        const artwork = appArtwork[appId]
        noticeTimer.stop()
        appNotice.visible = false
        if (quickMenu.progress > 0) quickMenu.settle(false)
        launchSourceSurface = foregroundSurface
        console.info("Ocean crossfading to the loading screen for " + appId)
        loadingView.startCrossfade(appId, artwork ? artwork[0] : appId, artwork ? artwork[1] : "")
    }
    Connections {
        target: root.promptBackend
        function onChanged() {
            const request = root.promptBackend.request
            if (!request.id) return
            if (quickMenu.progress > 0) quickMenu.settle(false)
            root.presentPromptOwner()
        }
    }
    onForegroundSurfaceChanged: {
        edgeNavigation.close()
        const request = promptBackend ? promptBackend.request : ({})
        if (systemPrompt.visible && request.id === systemPrompt.shownRequest.id && request.ownerPid
            && foregroundSurface && foregroundSurface.surface
            && foregroundSurface.surface.client.processId !== request.ownerPid)
            promptBackend.respond(request.id, "cancelled", "")
    }
    signal appLaunchRequested(string appId)
    signal appLaunchCancelled(string appId)
    onAppLaunchRequested: appId => {
        if (appController) appController.launch(appId)
    }
    onAppLaunchCancelled: appId => {
        if (appController) appController.cancelExternalLaunch(appId)
    }
    Connections {
        target: root.appController
        function onExternalLaunchRequested(appId) {
            root.pendingVisualLaunch = appId
            root.presentPendingLaunch()
        }
        function onLaunchFinished(appId, errorMessage) {
            if (errorMessage && root.pendingVisualLaunch === appId) {
                root.pendingVisualLaunch = ""
                root.appController.cancelExternalLaunch(appId)
            }
            root.finishAppLaunch(appId, errorMessage)
        }
        function onAppProcessExited(appId) {
            if (appId === root.pendingTerminationAppId) {
                root.terminationProcessExited = true
                root.completeTermination()
            }
        }
        function onTerminationFailed(appId, message) {
            if (appId === root.pendingTerminationAppId) {
                root.pendingTerminationAppId = ""
                root.dismissalGlow = 0
            }
            appNotice.message = message
            appNotice.visible = true
            noticeTimer.restart()
        }
        function onShowExistingApp(appId) {
            const item = root.appSurfaces[appId]
            if (item) {
                console.info("Ocean resuming the existing " + appId + " surface")
                if (loadingView.crossfade && loadingView.appId === appId) {
                    root.presentApplication(item)
                } else if (root.recentIndex(appId) >= 0) {
                    loadingView.reset()
                    root.startRecentEntry(appId, null)
                } else {
                    root.presentApplication(item)
                }
            }
        }
    }
    Connections {
        target: root.quickMenuBackend
        function onOperationFailed(message) {
            appNotice.message = message
            appNotice.visible = true
            noticeTimer.restart()
        }
    }
    Component.onCompleted: {
        if (resumeDrawer) {
            drawerSheet.open = true
            drawerSheet.y = drawerSheet.openY
        }
        if (startupError) {
            appNotice.message = startupError
            appNotice.visible = true
            noticeTimer.restart()
        }
    }

    width: 768
    height: 1366
    title: "Ocean Launcher"
    font.family: regularFont.name

    FontLoader {
        id: regularFont
        source: "assets/fonts/Inter-Regular.otf"
    }

    FontLoader {
        id: lightFont
        source: "assets/fonts/Inter-Light.otf"
    }

    visible: true
    color: "black"

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            const now = new Date()
            root.currentTime = Qt.formatTime(now, "h:mm AP")
            root.currentDate = Qt.formatDate(now, "MMM d, yyyy - dddd")
        }
    }
    Connections {
        target: root.timeBackend
        function onChanged() {
            const now = new Date()
            root.currentTime = Qt.formatTime(now, "h:mm AP")
            root.currentDate = Qt.formatDate(now, "MMM d, yyyy - dddd")
        }
    }

    function goHome() {
        if (transitionBusy || promptModal)
            return
        if (quickMenu.open) {
            quickMenu.settle(false)
            return
        }
        console.info("Ocean showing Home")
        const item = applicationVisible ? foregroundSurface : null
        if (loadingView.handingOff) {
            const appId = loadingView.appId
            loadingView.reset()
            appLaunchCancelled(appId)
        } else {
            cancelAppLaunch()
        }
        if (item) {
            transitionBusy = true
            backgroundApplication(item, true)
        } else {
            applicationVisible = false
        }
        noticeTimer.stop()
        appNotice.visible = false
        drawerSheet.closeDrawer()
    }

    function navigateBack() {
        if (transitionBusy || appLoading || promptModal || portalFileChooser.phase !== "hidden") return
        if (applicationVisible && foregroundSurface && foregroundSurface.surface
            && foregroundSurface.surface.hasContent) {
            foregroundSurface.takeFocus(compositor.defaultSeat)
            compositor.defaultSeat.sendKeyEvent(Qt.Key_Back, true)
            compositor.defaultSeat.sendKeyEvent(Qt.Key_Back, false)
        } else if (drawerSheet.open) {
            drawerSheet.closeDrawer()
        }
    }

    function requestAppLaunch(app) {
        if (loadingView.active || transitionBusy || pendingTerminationAppId || quickMenu.progress > 0)
            return
        if (installedApps.indexOf(app.appId) < 0) {
            showAppNotice(app.title)
            return
        }
        noticeTimer.stop()
        appNotice.visible = false
        snapAnimation.stop()
        if (appSurfaces[app.appId] && recentIndex(app.appId) >= 0) {
            startRecentEntry(app.appId, app.imageItem)
            return
        }
        loadingView.start(app, drawerChevron)
    }

    function cancelAppLaunch() {
        if (!loadingView.active)
            return
        if (loadingView.handingOff && foregroundSurface) {
            foregroundSurface.visible = false
            applicationVisible = false
        }
        const appId = loadingView.appId
        loadingView.reset()
        appLaunchCancelled(appId)
        restoreLaunchSource()
    }

    function restoreLaunchSource() {
        const source = launchSourceSurface
        launchSourceSurface = null
        if (source && appSurfaces[source.appId] === source && foregroundSurface !== source)
            presentApplication(source)
    }

    // Report launch failures; a presented surface completes via the crossfade.
    function finishAppLaunch(appId, errorMessage) {
        if (!loadingView.active || loadingView.appId !== appId)
            return
        loadingView.reset()
        if (appController) appController.cancelExternalLaunch(appId)
        restoreLaunchSource()
        if (errorMessage) {
            appNotice.message = errorMessage
            appNotice.visible = true
            noticeTimer.restart()
        }
    }

    function showAppNotice(title) {
        appNotice.message = title + " is not installed yet"
        appNotice.visible = true
        noticeTimer.restart()
    }

    function presentApplication(item) {
        if (quickMenu.progress > 0)
            quickMenu.settle(false)
        if (foregroundSurface && foregroundSurface !== item)
            backgroundApplication(foregroundSurface, false)
        item.captureGeneration++
        removeRecentCard(item.appId)
        foregroundSurface = item
        item.y = 0
        item.visible = true
        applicationVisible = true
        item.forceActiveFocus()
        launchSourceSurface = null
        if (loadingView.crossfade)
            console.info("Ocean crossfading from loading to " + item.appId)
        loadingView.beginHandoff()
    }

    function normalizedAppId(value) {
        if (value && value.startsWith("ocean-"))
            return value.slice(6)
        return value || ""
    }

    function registerApplication(item) {
        if (!appController || !item.surface || !item.surface.client) return false
        const appId = appController.appIdForProcess(item.surface.client.processId)
        if (!appId) return false
        if (item.appId === appId) return true
        if (item.appId && appSurfaces[item.appId] === item)
            delete appSurfaces[item.appId]
        item.appId = appId
        appSurfaces[appId] = item
        return true
    }

    function recentIndex(appId) {
        for (let index = 0; index < recentApps.count; ++index) {
            if (recentApps.get(index).appId === appId)
                return index
        }
        return -1
    }

    function removeRecentCard(appId) {
        const index = recentIndex(appId)
        if (index >= 0)
            recentApps.remove(index)
    }

    function revealRecentCard(appId) {
        const index = recentIndex(appId)
        if (index >= 0)
            recentApps.setProperty(index, "revealed", true)
    }

    function backgroundApplication(item, slideOut) {
        if (foregroundSurface === item)
            foregroundSurface = null
        applicationVisible = false
        if (!item.appId || installedApps.indexOf(item.appId) < 0) {
            item.visible = false
            transitionBusy = false
            return
        }

        if (slideOut) {
            exitingSurface = item
            exitingAppId = item.appId
            exitProgress = 0
        }

        removeRecentCard(item.appId)
        const artwork = appArtwork[item.appId]
        recentApps.insert(0, {
            "appId": item.appId,
            "title": artwork ? artwork[0] : (item.toplevel.title || item.appId),
            "iconSource": artwork ? artwork[1] : "",
            "previewUrl": "",
            "revealed": false
        })

        const appId = item.appId
        const captureGeneration = ++item.captureGeneration
        const path = appController ? appController.snapshotPath(appId) : ""
        const completeCapture = result => {
            if (appSurfaces[appId] !== item || item.captureGeneration !== captureGeneration)
                return
            if (!appController || appController.snapshotPath(appId) !== path)
                return
            if (path && result && result.saveToFile(path)) {
                const index = recentIndex(appId)
                if (index >= 0)
                    recentApps.setProperty(index, "previewUrl",
                                           "file://" + path + "?v=" + Date.now())
                console.info("Ocean saved recent-app snapshot to " + path)
            } else {
                console.warn("Ocean could not save the recent-app snapshot for " + appId)
            }
            if (slideOut) {
                if (reduceMotion) {
                    item.y = oceanScreen.height
                    exitProgress = 1
                    finishExit()
                } else {
                    exitAnimation.start()
                }
            } else if (foregroundSurface !== item) {
                item.visible = false
                revealRecentCard(appId)
            }
        }
        if (!item.grabToImage(completeCapture, Qt.size(768, 1366)))
            completeCapture(null)
    }

    function finishExit() {
        const item = exitingSurface
        const appId = exitingAppId
        if (item && appSurfaces[appId] === item) {
            item.visible = false
            item.y = 0
            revealRecentCard(appId)
        }
        exitingSurface = null
        exitingAppId = ""
        exitProgress = 0
        transitionBusy = false
    }

    function startRecentEntry(appId, sourceItem) {
        if (transitionBusy || pendingTerminationAppId)
            return
        const item = appSurfaces[appId]
        const index = recentIndex(appId)
        if (!item || index < 0) {
            removeRecentCard(appId)
            return
        }

        const point = sourceItem ? sourceItem.mapToItem(oceanScreen, 0, 0)
                                 : Qt.point((oceanScreen.width - 265) / 2, 147)
        const rect = sourceItem
            ? Qt.rect(point.x, point.y, sourceItem.width, sourceItem.height)
            : Qt.rect(point.x, point.y, 265, 445)
        const previewUrl = recentApps.get(index).previewUrl
        transitionBusy = true
        if (drawerSheet.open)
            snapAnimation.stop()
        recentTransition.start(appId, previewUrl, rect)
    }

    function finishRecentExpansion() {
        const item = appSurfaces[recentTransition.appId]
        if (!item) {
            recentTransition.reset()
            transitionBusy = false
            return
        }
        if (drawerSheet.open) {
            drawerSheet.open = false
            drawerSheet.y = drawerSheet.closedY
        }
        presentApplication(item)
        recentTransition.beginHandoff()
    }

    function finishRecentHandoff() {
        recentTransition.reset()
        transitionBusy = false
    }

    function killBackgroundApp(appId) {
        if (transitionBusy || pendingTerminationAppId || recentIndex(appId) < 0
            || !appSurfaces[appId] || !appController)
            return false
        pendingTerminationAppId = appId
        terminationProcessExited = false
        terminationSurfaceGone = !appSurfaces[appId]
        dismissalGlow = 1
        if (!appController.killApp(appId)) {
            pendingTerminationAppId = ""
            dismissalGlow = 0
            return false
        }
        console.info("Ocean requested termination of background app " + appId)
        return true
    }

    function completeTermination() {
        if (!pendingTerminationAppId || !terminationProcessExited || !terminationSurfaceGone)
            return
        console.info("Ocean background app fully terminated: " + pendingTerminationAppId)
        removeRecentCard(pendingTerminationAppId)
        pendingTerminationAppId = ""
        dismissalGlow = 0
    }

    function attachApplication(xdgSurface, toplevel) {
        console.info("Ocean received a Wayland application surface")
        const item = clientSurfaceComponent.createObject(applicationLayer,
            { "shellSurface": xdgSurface, "output": compositorOutput,
              "toplevel": toplevel, "visible": false })
        if (!item) {
            finishAppLaunch("com.openwave.settings", "The application surface could not be displayed.")
            return
        }
        item.configureSize()
        if (item.surface && item.surface.hasContent)
            item.showFirstFrame()
    }

    function detachApplication(item) {
        console.info("Ocean removed a Wayland application surface")
        item.captureGeneration++
        if (exitingSurface === item) {
            exitAnimation.stop()
            exitingSurface = null
            exitingAppId = ""
            exitProgress = 0
            transitionBusy = false
        }
        if (recentTransition.active && recentTransition.appId === item.appId) {
            recentTransition.reset()
            transitionBusy = false
        }
        if (item.appId) {
            if (appSurfaces[item.appId] === item)
                delete appSurfaces[item.appId]
            if (pendingTerminationAppId !== item.appId)
                removeRecentCard(item.appId)
            if (appController)
                appController.removeSnapshot(item.appId)
            if (pendingTerminationAppId === item.appId) {
                terminationSurfaceGone = true
                completeTermination()
            }
        }
        if (foregroundSurface === item) {
            foregroundSurface = null
            applicationVisible = false
        }
        item.destroy()
    }

    ListModel { id: recentApps }

    ParallelAnimation {
        id: exitAnimation
        NumberAnimation {
            target: root.exitingSurface
            property: "y"
            from: 0
            to: oceanScreen.height
            duration: 470
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: root
            property: "exitProgress"
            from: 0
            to: 1
            duration: 470
            easing.type: Easing.InCubic
        }
        onFinished: root.finishExit()
    }

    WaylandCompositor {
        id: compositor
        socketName: "wayland-0"
        Component.onCompleted: if (root.appController) root.appController.setSessionReady(true)
        WaylandOutput {
            id: compositorOutput
            window: root
            sizeFollowsWindow: true
        }
        XdgShell {
            onToplevelCreated: (toplevel, xdgSurface) =>
                root.attachApplication(xdgSurface, toplevel)
        }
    }

    Shortcut {
        sequences: ["Home"]
        enabled: !root.promptModal
        onActivated: root.goHome()
    }
    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: !root.applicationVisible && portalFileChooser.phase === "hidden" && !root.promptModal
        onActivated: root.goHome()
    }

    Item {
        id: oceanScreen
        objectName: "oceanScreen"

        width: root.contentScale > 0 ? root.width / root.contentScale : 768
        height: root.contentScale > 0 ? root.height / root.contentScale : 1366

        x: Math.round((root.width - width * scale) / 2)
        y: Math.round((root.height - height * scale) / 2)
        scale: root.contentScale
        transformOrigin: Item.TopLeft

        clip: true

        Rectangle { anchors.fill: parent; color: "black" }
        Image {
            id: wallpaperImage

            anchors.fill: parent
            source: Wallpaper.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
        }

        Rectangle {
            id: topShade
            visible: !root.applicationVisible

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            height: 110

            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: "#80000000"
                }

                GradientStop {
                    position: 1.0
                    color: "#00000000"
                }
            }
        }

        Rectangle {
            id: dismissalScrim
            objectName: "dismissalScrim"
            anchors.fill: parent
            z: 25
            color: "black"
            opacity: root.dismissalGlow * 0.54
            visible: opacity > 0
            Behavior on opacity {
                NumberAnimation { duration: root.reduceMotion ? 0 : 150 }
            }
        }

        RecentAppGrid {
            id: recentGrid
            objectName: "recentApps"
            visible: recentApps.count > 0 && !root.applicationVisible
                     && !loadingView.active && !drawerSheet.open
                     && !recentTransition.active
            enabled: visible
            x: 32
            y: 147
            width: parent.width - 64
            height: Math.max(0, drawerSheet.closedY - y - 80)
            model: recentApps
            viewportItem: oceanScreen
            appController: root.appController
            renderScale: root.contentScale
            fontFamily: root.font.family
            reduceMotion: root.reduceMotion
            interactionEnabled: !root.transitionBusy && root.pendingTerminationAppId === ""
            z: root.dismissalGlow > 0 || root.pendingTerminationAppId ? 30 : 10

            onActivated: (appId, cardItem) => root.startRecentEntry(appId, cardItem)
            onEdgeProximity: strength => {
                if (!root.pendingTerminationAppId)
                    root.dismissalGlow = strength
            }
            onTerminationRequested: (appId, cardItem) => {
                if (!root.killBackgroundApp(appId))
                    cardItem.cancelDismissal()
            }
        }

        Item {
            id: statusBar

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            height: 70
            z: 70

            Rectangle {
                anchors.fill: parent
                visible: pageEdge.enabled
                color: pageEdge.color
            }
            Item {
                anchors.fill: parent
                layer.enabled: pageEdge.enabled
                layer.effect: MultiEffect {
                    colorization: 1
                    colorizationColor: pageEdge.darkForeground ? "#202124" : "white"
                }

                IconImage {
                    id: battery
                    renderScale: root.contentScale

                    source: "assets/status/battery.svg"

                    width: 54
                    height: 30

                    anchors.left: parent.left
                    anchors.leftMargin: 22
                    anchors.verticalCenter: parent.verticalCenter

                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    id: clock

                    text: root.currentTime
                    color: "white"
                    opacity: 1 - Math.min(1, quickMenu.progress * 5)

                    font.pixelSize: 34
                    font.family: root.font.family
                    font.weight: Font.Light

                    property real centeredX:
                        (statusBar.width - width) / 2

                    property real besideBatteryX:
                        battery.x + battery.width + 18

                    property real morphProgress:
                        Math.max(drawerSheet.statusBarMorphProgress, loadingView.progress,
                                 recentTransition.expansion,
                                 root.exitingSurface ? 1 - root.exitProgress : 0,
                                 root.applicationVisible ? 1 : 0)

                    x: centeredX +
                       (besideBatteryX - centeredX) * morphProgress

                    anchors.verticalCenter: parent.verticalCenter
                }

                SignalIcon {
                    id: networkIndicators

                    kind: "cellular"
                    connected: root.quickMenuBackend
                               ? root.quickMenuBackend.cellularAvailable : false
                    strength: root.quickMenuBackend
                              ? root.quickMenuBackend.cellularStrength : 0
                    renderScale: root.contentScale
                    width: 82
                    height: 20

                    anchors.right: wifi.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                }

                SignalIcon {
                    id: wifi
                    kind: "wifi"
                    connected: root.quickMenuBackend
                               ? root.quickMenuBackend.wifiConnected : false
                    strength: root.quickMenuBackend
                              ? root.quickMenuBackend.wifiStrength : 0
                    renderScale: root.contentScale

                    width: 45
                    height: 34

                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter

                    fillMode: Image.PreserveAspectFit
                }

            }
        }

        PageEdgeColor {
            id: pageEdge
            source: root.foregroundSurface
            enabled: root.visible && root.applicationVisible && !root.transitionBusy && !loadingView.active
                && quickMenu.progress === 0 && root.foregroundSurface !== null
                && root.foregroundSurface.topInset > 0
        }

        Item {
            id: drawerSheet
            objectName: "drawerSheet"

            property real closedY: oceanScreen.height - 185
            property real openY: statusBar.height

            readonly property real chevronArrival:
                Math.max(0, Math.min(1, 1 - (y - openY) / 96))
            readonly property real chevronBlend:
                chevronArrival * chevronArrival * (3 - 2 * chevronArrival)

            property bool open: false
            enabled: !root.applicationVisible && !root.transitionBusy
                     && quickMenu.progress === 0 && !root.promptModal

            property color drawerColor: "#D9454E53"

            property real progress: {
                var travel = closedY - openY

                if (travel <= 0)
                    return 0

                return Math.max(
                    0,
                    Math.min(
                        1,
                        (closedY - y) / travel
                    )
                )
            }

            property real statusBarMorphProgress:
                Math.max(
                    0,
                    Math.min(
                        1,
                        (progress - 0.25) / 0.20
                    )
                )

            property bool dragging: handleDrag.active

            onClosedYChanged: Qt.callLater(function() {
                if (!drawerSheet.dragging)
                    drawerSheet.animateTo(drawerSheet.open ? drawerSheet.openY : drawerSheet.closedY)
            })

            width: oceanScreen.width
            height: oceanScreen.height - openY

            x: 0
            y: closedY

            z: 20

            property string draggedAppId: ""
            property real dragPointerX: 0
            property real dragPointerY: 0
            property bool dragOverFavorites: false
            property int dragInsertionIndex: 0

            function favoriteInsertionIndex(x, y) {
                const point = favoriteRow.mapFromItem(drawerSheet, x, y)
                return Math.max(0, Math.min(root.favoriteApps.length,
                    Math.floor((point.x + 65 + favoriteRow.spacing) / (130 + favoriteRow.spacing))))
            }

            function updateGridInsertion() {
                if (!draggedAppId || dragOverFavorites) return
                const point = appGrid.mapFromItem(drawerSheet, dragPointerX, dragPointerY)
                dragInsertionIndex = appGrid.insertionIndex(point.x, point.y)
            }

            function updateAppDrag(app, x, y) {
                if (draggedAppId !== app.appId) return
                const point = app.mapToItem(drawerSheet, x, y)
                dragPointerX = point.x
                dragPointerY = point.y
                dragOverFavorites = point.y < divider.y + 18
                if (dragOverFavorites) {
                    dragInsertionIndex = favoriteInsertionIndex(point.x, point.y)
                } else {
                    updateGridInsertion()
                }
            }

            function beginAppDrag(app, x, y) {
                if (!open || progress < 0.95 || !root.appController) return
                draggedAppId = app.appId
                updateAppDrag(app, x, y)
            }

            function finishAppDrag(app, x, y) {
                if (draggedAppId !== app.appId) return
                updateAppDrag(app, x, y)
                const validDrop = dragPointerX >= 0 && dragPointerX <= width
                    && dragPointerY >= 0 && dragPointerY <= height
                const destination = dragOverFavorites
                const index = dragInsertionIndex
                draggedAppId = ""
                if (validDrop && !root.appController.moveApp(app.appId, destination, index)) {
                    appNotice.message = "Could not save app order."
                    appNotice.visible = true
                    noticeTimer.restart()
                }
            }

            function animateTo(targetY) {
                snapAnimation.stop()
                if (root.reduceMotion) {
                    drawerSheet.y = targetY
                    return
                }

                snapAnimation.from = drawerSheet.y
                snapAnimation.to = targetY

                var fullDistance = drawerSheet.closedY - drawerSheet.openY
                var remainingDistance = Math.abs(targetY - drawerSheet.y)
                var fraction = remainingDistance / fullDistance

                snapAnimation.duration = Math.round(
                    Math.max(
                        160,
                        Math.min(360, 360 * fraction)
                    )
                )

                snapAnimation.start()
            }

            function openDrawer() {
                open = true
                animateTo(openY)
            }

            function closeDrawer() {
                open = false
                animateTo(closedY)
            }

            function toggleDrawer() {
                if (open)
                    closeDrawer()
                else
                    openDrawer()
            }

            function settle() {
                var travel = closedY - openY
                var progress = (closedY - y) / travel

                if (open) {
                    if (progress >= 0.70)
                        openDrawer()
                    else
                        closeDrawer()
                } else {
                    if (progress >= 0.20)
                        openDrawer()
                    else
                        closeDrawer()
                }
            }

            NumberAnimation {
                id: snapAnimation

                target: drawerSheet
                property: "y"

                easing.type: Easing.OutQuart
            }

            Rectangle {
                anchors.fill: parent

                color: drawerSheet.drawerColor
                opacity: 1 - loadingView.progress
            }

            Item {
                id: drawerHandle
                objectName: "drawerHandle"
                visible: !loadingView.active

                width: 180
                height: 90

                anchors.horizontalCenter: parent.horizontalCenter

                y: -65

                z: 40

                Item {
                    id: drawerChevron
                    objectName: "drawerChevron"

                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: drawerSheet.chevronBlend
                        * (statusBar.height / 2 - drawerSheet.openY
                           - drawerHandle.y - drawerHandle.height / 2)

                    width: 28
                    height: 16
                    rotation: 180 * drawerSheet.chevronBlend

                    Chevron {
                        color: "#202020"
                        opacity: 1 - drawerSheet.chevronBlend
                    }

                    Chevron {
                        objectName: "arrivingChevron"
                        opacity: drawerSheet.chevronBlend
                    }
                }

                DragHandler {
                    id: handleDrag

                    target: drawerSheet

                    xAxis.enabled: false

                    yAxis.minimum: drawerSheet.openY
                    yAxis.maximum: drawerSheet.closedY

                    onActiveChanged: {
                        if (handleDrag.active)
                            snapAnimation.stop()
                        else
                            Qt.callLater(drawerSheet.settle)
                    }
                }

                TapHandler {
                    onTapped: {
                        drawerSheet.toggleDrawer()
                    }
                }
            }

            Item {
                id: dock
                opacity: 1 - Math.min(1, loadingView.progress * 3)
                enabled: !loadingView.active

                width: parent.width
                height: 185

                Rectangle {
                    anchors.fill: parent

                    color: "transparent"
                }

                Row {
                    id: favoriteRow
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 25

                    spacing: 54
                    Repeater {
                        model: root.favoriteApps
                        delegate: AppIcon {
                            required property string modelData
                            appId: modelData
                            title: root.appArtwork[modelData][0]
                            source: root.appArtwork[modelData][1]
                            renderScale: root.contentScale
                            draggable: drawerSheet.open && drawerSheet.progress > 0.95
                            onClicked: app => root.requestAppLaunch(app)
                            onDragStarted: (app, x, y) => drawerSheet.beginAppDrag(app, x, y)
                            onDragMoved: (app, x, y) => drawerSheet.updateAppDrag(app, x, y)
                            onDragFinished: (app, x, y) => drawerSheet.finishAppDrag(app, x, y)
                        }
                    }
                }
            }

            Rectangle {
                id: divider
                opacity: 1 - Math.min(1, loadingView.progress * 3)

                anchors.top: dock.bottom
                anchors.horizontalCenter: parent.horizontalCenter

                width: parent.width - 100
                height: 1

                color: "#80ffffff"
            }

            DrawerAppGrid {
                id: appGrid
                objectName: "appDrawerGrid"
                opacity: 1 - Math.min(1, loadingView.progress * 3)
                enabled: !loadingView.active

                anchors.top: divider.bottom
                anchors.left: divider.left
                anchors.right: divider.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16

                apps: root.unpinnedApps
                artwork: root.appArtwork
                renderScale: root.contentScale
                draggable: drawerSheet.open && drawerSheet.progress > 0.95
                appDragActive: drawerSheet.draggedAppId.length > 0
                appDragY: drawerSheet.dragPointerY - y
                onContentYChanged: drawerSheet.updateGridInsertion()
                onClicked: app => root.requestAppLaunch(app)
                onAppDragStarted: (app, x, y) => drawerSheet.beginAppDrag(app, x, y)
                onAppDragMoved: (app, x, y) => drawerSheet.updateAppDrag(app, x, y)
                onAppDragFinished: (app, x, y) => drawerSheet.finishAppDrag(app, x, y)
            }

            Rectangle {
                visible: drawerSheet.draggedAppId && drawerSheet.dragOverFavorites
                x: favoriteRow.x + drawerSheet.dragInsertionIndex * (130 + favoriteRow.spacing) - 5
                y: favoriteRow.y + 4
                width: 5; height: 122; radius: 2
                color: "#00c9f2"
                z: 48
            }
            Rectangle {
                readonly property point position: appGrid.insertionPoint(drawerSheet.dragInsertionIndex)
                visible: drawerSheet.draggedAppId && !drawerSheet.dragOverFavorites
                         && y >= appGrid.y && y + height <= appGrid.y + appGrid.height
                x: appGrid.x + position.x
                y: appGrid.y + position.y + 5
                width: 5; height: 120; radius: 2
                color: "#00c9f2"
                z: 48
            }
            Item {
                visible: drawerSheet.draggedAppId.length > 0
                x: drawerSheet.dragPointerX - width / 2
                y: drawerSheet.dragPointerY - 65
                width: 150; height: 175
                z: 50
                IconImage {
                    x: 10; y: 0; width: 130; height: 130
                    source: root.appArtwork[drawerSheet.draggedAppId]
                        ? root.appArtwork[drawerSheet.draggedAppId][1] : ""
                    renderScale: root.contentScale
                }
                Text {
                    anchors.top: parent.top; anchors.topMargin: 138
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.appArtwork[drawerSheet.draggedAppId]
                        ? root.appArtwork[drawerSheet.draggedAppId][0] : ""
                    color: "white"; font.family: root.font.family; font.pixelSize: 28
                }
            }
        }

        AppLoading {
            id: loadingView
            objectName: "appLoadingView"
            anchors.fill: parent
            z: crossfade && !handingOff ? 46 : 40
            wallpaper: wallpaperImage
            renderScale: root.contentScale
            fontFamily: root.font.family
            reduceMotion: root.reduceMotion
            onReady: appId => {
                if (crossfade && root.appController) {
                    if (root.foregroundSurface) root.backgroundApplication(root.foregroundSurface, false)
                    root.appController.launchExternalApp(appId)
                }
                else root.appLaunchRequested(appId)
            }
            onCancelled: root.cancelAppLaunch()
        }

        RecentAppTransition {
            id: recentTransition
            objectName: "recentAppTransition"
            anchors.fill: parent
            z: 47
            reduceMotion: root.reduceMotion
            onExpanded: root.finishRecentExpansion()
            onFinished: root.finishRecentHandoff()
        }

        Rectangle {
            id: terminationEdge
            objectName: "terminationEdge"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 160
            z: 90
            opacity: root.pendingTerminationAppId ? 1 : root.dismissalGlow
            visible: opacity > 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#eaff233a" }
                GradientStop { position: 0.2; color: "#aaff233a" }
                GradientStop { position: 1; color: "#00ff233a" }
            }
            Behavior on opacity {
                NumberAnimation { duration: root.reduceMotion ? 0 : 160 }
            }
        }

        Component {
            id: clientSurfaceComponent
            ShellSurfaceItem {
                id: clientSurface
                property var toplevel: null
                property string appId: ""
                readonly property real topInset: appId.startsWith("org.openwave.webapp.h") ? statusBar.height : 0
                function configureSize() {
                    if (toplevel && width > 0 && height > 0)
                        toplevel.sendMaximized(Qt.size(Math.round(width), Math.round(height)))
                }
                transform: Translate { y: clientSurface.topInset }
                onWidthChanged: configureSize()
                onHeightChanged: configureSize()
                property int captureGeneration: 0
                property bool firstPresentationPending: true
                function showFirstFrame() {
                    if (!firstPresentationPending || !surface || !surface.hasContent
                        || !root.registerApplication(clientSurface))
                        return
                    console.info("Ocean presenting the application's first frame")
                    firstPresentationPending = false
                    root.presentApplication(clientSurface)
                }
                width: applicationLayer.width
                height: applicationLayer.height - topInset
                opacity: loadingView.active && loadingView.handingOff
                    && root.foregroundSurface === clientSurface
                    ? loadingView.handoffProgress : 1
                focusOnClick: true
                touchEventsEnabled: true
                inputEventsEnabled: !loadingView.active && !root.transitionBusy && !root.promptModal
                Connections {
                    target: clientSurface.surface
                    function onHasContentChanged() {
                        if (clientSurface.surface && clientSurface.surface.hasContent)
                            clientSurface.showFirstFrame()
                    }
                }
                Connections {
                    target: clientSurface.toplevel
                    function onAppIdChanged() { root.registerApplication(clientSurface) }
                    function onSetMinimized() {
                        if (root.applicationVisible && root.foregroundSurface === clientSurface)
                            root.goHome()
                    }
                }
                Connections {
                    target: root.appController
                    function onRunningAppsChanged() { clientSurface.showFirstFrame() }
                }
                onSurfaceDestroyed: root.detachApplication(clientSurface)
            }
        }

        Item {
            id: applicationLayer
            objectName: "applicationLayer"
            anchors.fill: parent
            clip: true
            z: 45
        }

        QuickMenu {
            id: quickMenu
            objectName: "quickMenu"
            anchors.fill: parent
            z: 60
            wallpaper: root.applicationVisible ? applicationLayer : wallpaperImage
            backend: root.quickMenuBackend
            renderScale: root.contentScale
            regularFont: root.font.family
            lightFont: lightFont.name
            timeText: root.currentTime
            dateText: root.currentDate
            reduceMotion: root.reduceMotion
            appVisible: root.applicationVisible
            showDisabledHandle: root.promptModal && root.applicationVisible
            canOpen: (root.applicationVisible || (!drawerSheet.open
                     && drawerSheet.progress < 0.02)) && !loadingView.active
                     && !recentTransition.active && !root.transitionBusy
                     && !root.promptModal
        }

        EdgeNavigationMenu {
            id: edgeNavigation
            objectName: "edgeNavigation"
            anchors.fill: parent
            z: 95
            backdrop: root.applicationVisible ? applicationLayer : wallpaperImage
            renderScale: root.contentScale
            reduceMotion: root.reduceMotion
            safeTop: statusBar.height
            safeBottom: 0
            backAvailable: root.applicationVisible || drawerSheet.open
            canOpen: !root.transitionBusy && !root.appLoading && !root.promptModal
                && !recentTransition.active && portalFileChooser.phase === "hidden"
                && quickMenu.progress === 0
            onBackRequested: root.navigateBack()
            onHomeRequested: root.goHome()
        }

        PortalBackdrop {
            anchors.fill: parent
            z: 65
            source: root.applicationVisible ? applicationLayer : wallpaperImage
            visible: portalFileChooser.visible
        }

        PortalFileChooser {
            id: portalFileChooser
            objectName: "portalFileChooser"
            anchors.fill: parent
            z: 120
            backend: root.portalBackend
            filesBackend: root.portalFilesBackend
            backdrop: root.applicationVisible ? applicationLayer : wallpaperImage
            renderBackdrop: false
            reduceMotion: root.reduceMotion
        }

        PromptDialog {
            id: systemPrompt
            objectName: "systemPrompt"
            // Keep both the dimmer and prompt within the originating app's bounds.
            x: root.promptOwnerSurface ? applicationLayer.x + root.promptOwnerSurface.x : 0
            y: root.promptOwnerSurface ? applicationLayer.y + root.promptOwnerSurface.y : 0
            width: root.promptOwnerSurface ? root.promptOwnerSurface.width : oceanScreen.width
            height: root.promptOwnerSurface ? root.promptOwnerSurface.height : oceanScreen.height
            z: 130
            backend: root.promptBackend
            backdrop: root.promptOwnerSurface || (root.applicationVisible ? applicationLayer : wallpaperImage)
            reduceMotion: root.reduceMotion
            canPresent: !root.displayedPromptRequest.applicationId
                || (root.applicationVisible && root.promptOwnerSurface === root.foregroundSurface
                    && root.promptOwnerSurface && root.promptOwnerSurface.surface.hasContent
                    && !root.transitionBusy && !root.appLoading)
            safeTop: statusBar.height
            safeBottom: 0
            onVisibleChanged: {
                if (!visible) {
                    if (root.applicationVisible && root.foregroundSurface)
                        root.foregroundSurface.forceActiveFocus()
                    root.presentPromptOwner()
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            z: 110
            visible: root.quickMenuBackend && root.quickMenuBackend.softwareBrightnessActive
            opacity: visible ? (100 - root.quickMenuBackend.brightness) / 100 * 0.65 : 0
            color: "black"
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }

        Rectangle {
            id: appNotice
            objectName: "appNotice"
            property string message: ""
            visible: false
            z: 110
            width: Math.min(680, noticeText.implicitWidth + 48)
            height: noticeText.implicitHeight + 32
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 205
            radius: 18
            color: "#e6202020"

            Text {
                id: noticeText
                anchors.centerIn: parent
                text: appNotice.message
                font.pixelSize: 26
                font.family: root.font.family
                color: "white"
            }

            Timer {
                id: noticeTimer
                interval: 2500
                onTriggered: appNotice.visible = false
            }
        }
    }
}
