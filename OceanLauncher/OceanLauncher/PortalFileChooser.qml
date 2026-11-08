import QtQuick
import OpenWave.Fonts 1.0
import OpenWave.Theme 1.0
import QtQuick.Controls

Item {
    id: chooser
    required property var backend
    required property var filesBackend
    property Item backdrop
    property bool renderBackdrop: true
    property bool lightTheme: false
    property bool reduceMotion: false
    property bool menuOpen: false
    property string menuPage: "main"
    property string sourcePage: "files"
    property string galleryAlbumFilter: ""
    property string galleryTab: "photos"
    property string gallerySearchQuery: ""
    property var selectedGalleryTokens: []
    property bool searchOpen: false
    property bool compactRows: false
    property bool showDetails: true
    property bool aboutOpen: false
    property var selectedPaths: []
    readonly property string requestedId: backend ? backend.requestId : ""
    property string displayedRequestId: ""
    property string displayedTitle: ""
    property string displayedMutedTitle: ""
    property string displayedPrimaryTitle: ""
    property string displayedMode: ""
    property string phase: "hidden"
    readonly property real unfold: unfoldTransition.progress
    property bool navigating: false
    property bool navigationCommitted: false
    property bool pendingUp: false
    property string pendingPath: ""
    property var navigationSnapshot: null
    property int navigationGeneration: 0
    readonly property bool imageSourceRequest: {
        if (!backend) return false
        if (backend.imageSources) return true
        if (backend.mode !== "OpenFile" && backend.mode !== "OpenFiles") return false
        const types = backend.mimeTypes || []
        for (let i = 0; i < types.length; ++i)
            if (String(types[i]).toLowerCase().startsWith("image/")) return true
        return false
    }
    visible: phase !== "hidden"

    onRequestedIdChanged: Qt.callLater(chooser.syncRequest)
    Component.onCompleted: Qt.callLater(chooser.syncRequest)
    onReduceMotionChanged: {
        if (!reduceMotion) return
        if (navigating) {
            folderCrossfade.stop()
            if (!navigationCommitted) commitNavigation()
            resetNavigation()
        }
    }

    FontLoader { id: regular; source: "assets/fonts/Inter-Regular.otf" }
    FontLoader { id: light; source: "assets/fonts/Inter-Light.otf" }

    function prepareRequest() {
        resetNavigation()
        selectedPaths = []
        menuOpen = false
        menuPage = "main"
        const initialImageSource = backend.imageSource || ""
        sourcePage = imageSourceRequest && initialImageSource === "gallery"
            ? "gallery" : "files"
        galleryAlbumFilter = ""
        galleryTab = "photos"
        gallerySearchQuery = ""
        selectedGalleryTokens = []
        searchOpen = false
        compactRows = false
        showDetails = true
        aboutOpen = false
        message.text = ""
        searchField.text = ""
        filesBackend.goToRoot()
        filesBackend.setShowHidden(false)
        if (backend.initialDirectory)
            filesBackend.goToDirectory(backend.initialDirectory)
        filenameField.text = backend.suggestedFilename
        if (sourcePage === "gallery") backend.loadGallery()
    }

    function syncRequest() {
        if (phase === "hidden") {
            if (requestedId && filesBackend) beginOpen()
        } else if (phase !== "closing"
                   && (!requestedId || requestedId !== displayedRequestId)) {
            beginClose()
        }
    }
    function beginOpen() {
        displayedRequestId = requestedId
        displayedTitle = backend.title
        displayedMutedTitle = backend.mutedTitle || ""
        displayedPrimaryTitle = backend.primaryTitle || ""
        displayedMode = backend.mode
        prepareRequest()
        phase = "opening"
        unfoldTransition.open()
    }
    function beginClose() {
        if (phase === "closing" || phase === "hidden") return
        resetNavigation()
        phase = "closing"
        unfoldTransition.close()
    }
    function finishClose() {
        if (phase !== "closing") return
        phase = "hidden"
        displayedRequestId = ""
        if (requestedId) Qt.callLater(chooser.syncRequest)
    }

    VerticalUnfoldTransition {
        id: unfoldTransition
        reduceMotion: chooser.reduceMotion
        openDuration: 250
        closeDuration: 200
        closedScale: 0
        fadeEnabled: false
        reducedDuration: 0
        proportionalClose: true
        onOpened: {
            if (chooser.phase !== "opening") return
            if (chooser.requestedId !== chooser.displayedRequestId) {
                chooser.beginClose()
                return
            }
            chooser.phase = "open"
            if (chooser.sourcePage === "source") sourceFilesButton.forceActiveFocus()
            else if (chooser.sourcePage === "gallery") galleryContent.focusGallery()
            else fileList.forceActiveFocus()
        }
        onClosed: chooser.finishClose()
    }
    ParallelAnimation {
        id: folderCrossfade
        NumberAnimation {
            target: navigationContent
            property: "opacity"
            from: 0; to: 1; duration: 220
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: previousContent
            property: "opacity"
            from: 1; to: 0; duration: 220
            easing.type: Easing.InOutQuad
        }
        onFinished: chooser.resetNavigation()
    }

    function resetNavigation() {
        folderCrossfade.stop()
        navigationGeneration += 1
        navigating = false
        navigationCommitted = false
        pendingUp = false
        pendingPath = ""
        navigationContent.opacity = 1
        previousContent.opacity = 0
        previousContent.source = ""
        navigationSnapshot = null
    }
    function commitNavigation() {
        if (navigationCommitted || !filesBackend) return false
        navigationCommitted = true
        const changed = pendingUp ? filesBackend.up()
                                  : filesBackend.goToDirectory(pendingPath)
        if (!changed) return false
        selectedPaths = []
        searchField.text = ""
        message.text = ""
        fileList.positionViewAtBeginning()
        return true
    }
    function beginFolderCrossfade() {
        if (!navigating || navigationCommitted || phase !== "open") return
        previousContent.opacity = 1
        navigationContent.opacity = 0
        if (!commitNavigation()) {
            resetNavigation()
            return
        }
        folderCrossfade.start()
    }
    function navigateWithoutSnapshot() {
        if (!navigating) return
        if (phase === "open") commitNavigation()
        resetNavigation()
    }
    function navigateTo(path, up) {
        if (phase !== "open" || navigating || !filesBackend) return
        pendingPath = path
        pendingUp = up
        navigating = true
        if (reduceMotion) {
            navigateWithoutSnapshot()
            return
        }
        const generation = ++navigationGeneration
        if (!navigationContent.grabToImage(function(result) {
            if (!chooser.navigating || chooser.phase !== "open"
                    || chooser.navigationGeneration !== generation) return
            if (!result || !result.url) {
                chooser.navigateWithoutSnapshot()
                return
            }
            chooser.navigationSnapshot = result
            previousContent.source = result.url
            if (previousContent.status === Image.Ready)
                chooser.beginFolderCrossfade()
            else if (previousContent.status === Image.Error)
                chooser.navigateWithoutSnapshot()
        })) {
            navigateWithoutSnapshot()
        }
    }
    function goUp() {
        navigateTo("", true)
    }
    function selected(row, folder) {
        if (phase !== "open" || !filesBackend || !backend || backend.busy
            || navigating || requestedId !== displayedRequestId) return
        menuOpen = false
        const path = filesBackend.entryPath(row)
        if (!path) return
        if (folder) {
            navigateTo(path, false)
            return
        }
        if (displayedMode === "SaveFile" || displayedMode === "SelectDirectory") return
        if (!backend.acceptsPath(path)) {
            message.text = "This file type cannot be selected."
            return
        }
        message.text = ""
        if (displayedMode === "OpenFiles") {
            const paths = selectedPaths.slice()
            const index = paths.indexOf(path)
            if (index >= 0) paths.splice(index, 1)
            else paths.push(path)
            selectedPaths = paths
        } else {
            // Single-file requests finish directly when a row is tapped.
            backend.choose([path])
        }
    }
    function confirm() {
        if (phase !== "open" || !backend || backend.busy
            || navigating || requestedId !== displayedRequestId) return
        if (displayedMode === "SelectDirectory") {
            backend.choose([filesBackend.currentPath])
        } else if (displayedMode === "SaveFile") {
            const name = filenameField.text.trim()
            if (!name || name === "." || name === ".." || name.indexOf("/") >= 0
                || name.indexOf("\\") >= 0) {
                message.text = "Enter a valid filename."
                return
            }
            backend.choose([filesBackend.currentPath + "/" + name])
        } else if (selectedPaths.length) {
            backend.choose(selectedPaths)
        }
    }
    function backFromGallery() {
        selectedGalleryTokens = []
        backend.cancel()
    }
    function selectGalleryItem(token) {
        if (!backend || backend.busy || !token) return
        if (displayedMode === "OpenFile") {
            backend.chooseGallery([token])
            return
        }
        const tokens = selectedGalleryTokens.slice()
        const index = tokens.indexOf(token)
        if (index >= 0) tokens.splice(index, 1)
        else if (tokens.length < 32) tokens.push(token)
        selectedGalleryTokens = tokens
    }
    readonly property var galleryAlbums: {
        if (!backend || !visibleGalleryItems) return []
        const albums = []
        for (const item of visibleGalleryItems)
            if (albums.indexOf(item.album) < 0) albums.push(item.album)
        return albums
    }
    readonly property var visibleGalleryItems: {
        if (!backend || !backend.galleryItems) return []
        const query = gallerySearchQuery.trim().toLowerCase()
        return backend.galleryItems.filter(item => {
            const matchesAlbum = galleryTab !== "photos" || !galleryAlbumFilter || item.album === galleryAlbumFilter
            const matchesQuery = !query || String(item.name).toLowerCase().includes(query)
                || String(item.album).toLowerCase().includes(query)
            return matchesAlbum && matchesQuery
        })
    }
    function toggleGallerySearch() {
        searchOpen = !searchOpen
        if (searchOpen) Qt.callLater(searchField.forceActiveFocus)
        else {
            gallerySearchQuery = ""
            searchField.text = ""
            galleryContent.focusGallery()
        }
    }
    Connections {
        target: chooser.backend
        function onErrorOccurred(error) { message.text = error }
    }
    Shortcut {
        sequences: ["Escape", "Back"]
        enabled: chooser.phase === "open"
        onActivated: {
            if (chooser.aboutOpen) chooser.aboutOpen = false
            else if (chooser.menuPage !== "main") chooser.menuPage = "main"
            else if (chooser.menuOpen) chooser.menuOpen = false
            else if (chooser.sourcePage === "gallery") chooser.backFromGallery()
            else if (chooser.searchOpen) chooser.searchOpen = false
            else chooser.backend.cancel()
        }
    }

    MouseArea { anchors.fill: parent; preventStealing: true }
    PortalBackdrop {
        objectName: "portalBackdrop"
        anchors.fill: parent
        source: chooser.backdrop
        visible: chooser.renderBackdrop
    }

    Rectangle {
        id: panel
        objectName: "portalFileChooserPanel"
        readonly property real restingY: 91
        x: chooser.sourcePage === "gallery" ? 0 : 32
        y: chooser.sourcePage === "gallery"
            ? restingY + (1 - chooser.unfold) * height : restingY
        width: chooser.sourcePage === "gallery" ? parent.width : parent.width - 69
        height: parent.height - restingY - 48
        enabled: chooser.phase === "open"
        radius: chooser.sourcePage === "gallery" ? 0 : 20
        color: chooser.sourcePage === "gallery" ? "transparent"
            : chooser.lightTheme ? "#dde8e9e9" : "#b0a8abad"
        border.color: chooser.lightTheme ? "#ffffff" : "#93dde0e1"
        border.width: chooser.sourcePage === "gallery" ? 0 : 1
        readonly property color ink: chooser.lightTheme ? "#25313a" : "#ffffff"
        readonly property color mutedInk: chooser.lightTheme ? "#68747a" : "#d9dedf"
        readonly property real titleMutedOpacity: 0.5
        transform: Scale {
            origin.x: panel.width / 2
            origin.y: panel.height / 2
            xScale: 1
            yScale: chooser.sourcePage === "gallery" ? 1 : chooser.unfold
        }
        MouseArea { anchors.fill: parent; preventStealing: true }

        AbstractButton {
            id: menuButton
            objectName: "portalMenuButton"
            x: chooser.sourcePage === "gallery" ? panel.width * 0.853 : panel.width - 151
            y: 15; width: 65; height: 62
            z: 3
            enabled: !chooser.navigating
            onClicked: {
                chooser.menuPage = "main"
                chooser.menuOpen = !chooser.menuOpen
            }
            background: Rectangle { color: menuButton.down ? "#2fffffff" : "transparent"; radius: 10 }
            contentItem: Item {
                Repeater {
                    model: 3
                    Rectangle {
                        x: 12; y: 13 + index * 14; width: 42; height: 2
                        color: panel.ink
                    }
                }
            }
        }
        AbstractButton {
            id: closeButton
            objectName: "portalCloseButton"
            x: panel.width - 81; y: 15; width: 65; height: 62
            visible: chooser.sourcePage !== "gallery"
            z: 3
            onClicked: chooser.backend.cancel()
            background: Rectangle { color: closeButton.down ? "#2fffffff" : "transparent"; radius: 10 }
            contentItem: Item {
                Rectangle { x: 13; y: 29; width: 41; height: 2; rotation: 45; color: panel.ink }
                Rectangle { x: 13; y: 29; width: 41; height: 2; rotation: -45; color: panel.ink }
            }
        }
        AbstractButton {
            id: gallerySearchButton
            objectName: "portalPhotoChooserSearchButton"
            x: panel.width * 0.733; y: 15; width: 65; height: 62
            visible: chooser.sourcePage === "gallery"
            z: 3
            onClicked: chooser.toggleGallerySearch()
            background: Rectangle { color: gallerySearchButton.down ? "#2fffffff" : "transparent"; radius: 10 }
            contentItem: Item {
                Rectangle {
                    x: 15; y: 12; width: 25; height: 25; radius: 13
                    color: "transparent"; border.color: panel.ink; border.width: 2
                }
                Rectangle {
                    x: 38; y: 36; width: 15; height: 2
                    color: panel.ink; rotation: 48
                }
            }
        }
        PortalTitle {
            id: portalTitle
            objectName: "portalTitle"
            x: 54; y: 73; width: parent.width - 105; height: 61
            visible: chooser.sourcePage !== "gallery"
            z: 3
            title: chooser.displayedTitle && chooser.displayedTitle !== chooser.displayedMode
                   ? chooser.displayedTitle
                   : chooser.displayedMode === "SaveFile" ? qsTr("Saving Document")
                   : chooser.displayedMode === "SelectDirectory" ? qsTr("Selecting Folder")
                   : chooser.displayedMode === "OpenFiles" ? qsTr("Selecting Files")
                   : qsTr("Opening File")
            mutedTitle: chooser.displayedMutedTitle
            primaryTitle: chooser.displayedPrimaryTitle
            primaryColor: panel.ink
            mutedOpacity: panel.titleMutedOpacity
            fontFamily: Fonts.family
        }
        Text {
            visible: !chooser.searchOpen && chooser.sourcePage !== "gallery"
            z: 3
            x: 55; y: 130; width: parent.width - 105; height: 37
            text: chooser.displayedMode === "OpenFiles" ? "Select files"
                : chooser.displayedMode === "SaveFile" ? "Save file as..."
                : chooser.displayedMode === "SelectDirectory" ? "Select a folder"
                : "Select a file"
            color: panel.mutedInk; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 24
            verticalAlignment: Text.AlignVCenter
        }
        TextField {
            id: searchField
            objectName: "portalPhotoChooserSearchField"
            visible: chooser.searchOpen
            enabled: !chooser.navigating
            x: chooser.sourcePage === "gallery" ? 78 : 53
            y: chooser.sourcePage === "gallery" ? 17 : 129
            width: chooser.sourcePage === "gallery"
                ? Math.max(0, panel.width * 0.733 - x - 15) : parent.width - 106
            height: chooser.sourcePage === "gallery" ? 62 : 42
            z: 3
            placeholderText: chooser.sourcePage === "gallery" ? qsTr("Search photos") : qsTr("Search files")
            color: panel.ink; placeholderTextColor: panel.mutedInk
            font.family: light.name; font.pixelSize: 26
            leftPadding: 2; rightPadding: 2
            background: Item {}
            onTextChanged: {
                if (chooser.sourcePage === "gallery") chooser.gallerySearchQuery = text
                else if (filesBackend) filesBackend.setSearchQuery(text)
            }
            onAccepted: focus = false
        }
        Rectangle {
            x: chooser.sourcePage === "gallery" ? 52 : 52
            y: 167; width: parent.width - 104; height: 2
            color: panel.ink; opacity: 0.86
            visible: chooser.sourcePage !== "gallery"
        }

        Item {
            id: navigationContent
            objectName: "portalNavigationContent"
            anchors.fill: parent
            visible: chooser.sourcePage === "files"

            ListView {
                id: fileList
                objectName: "portalFilesList"
                x: 47; y: 184
                width: parent.width - 94
                height: footerDivider.y - y
                    - (chooser.displayedMode === "SaveFile" ? 128 : 10)
                clip: true; focus: true; model: filesBackend
                boundsBehavior: Flickable.StopAtBounds
                delegate: AbstractButton {
                    id: fileRow
                    required property int index
                    required property string name
                    required property bool folder
                    required property string detail
                    width: fileList.width; height: chooser.compactRows ? 78 : 105
                    onClicked: chooser.selected(index, folder)
                    background: Rectangle {
                        color: chooser.selectedPaths.indexOf(filesBackend.entryPath(fileRow.index)) >= 0
                            ? "#4cffffff" : fileRow.down ? "#30ffffff" : "transparent"
                        radius: 8
                        Rectangle {
                            x: 0; y: parent.height - 18; width: parent.width; height: 2
                            color: panel.ink; opacity: 0.68
                        }
                    }
                    contentItem: Item {
                        Image {
                            x: 17; y: chooser.compactRows ? 7 : 12
                            width: chooser.compactRows ? 49 : 60
                            height: chooser.compactRows ? 49 : 56
                            source: fileRow.folder
                                ? "qrc:/OceanPortal/assets/folder-outline.svg"
                                : /\.(png|jpe?g|webp|gif|bmp|svg)$/i.test(fileRow.name)
                                  ? "qrc:/OceanPortal/assets/image-outline.svg"
                                  : "qrc:/OceanPortal/assets/file-outline.svg"
                            sourceSize.width: 120; sourceSize.height: 112
                        }
                        Text {
                            x: chooser.compactRows ? 78 : 91
                            y: chooser.compactRows ? 5 : 12
                            width: parent.width - x - 13; height: 44
                            text: fileRow.name; color: panel.ink
                            font.family: regular.name; font.pixelSize: chooser.compactRows ? 27 : 31
                            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            visible: chooser.showDetails
                            x: chooser.compactRows ? 78 : 91
                            y: chooser.compactRows ? 43 : 55
                            width: parent.width - x - 13; height: 29
                            text: fileRow.detail; color: panel.mutedInk
                            font.family: light.name; font.pixelSize: chooser.compactRows ? 18 : 21
                            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Keys.onReturnPressed: chooser.selected(index, folder)
                    Keys.onEnterPressed: chooser.selected(index, folder)
                }
                Text {
                    anchors.centerIn: parent
                    visible: fileList.count === 0
                    text: filesBackend && filesBackend.error ? filesBackend.error
                        : searchField.text.length ? "No matching files" : "This folder is empty"
                    color: panel.mutedInk; font.family: light.name; font.pixelSize: 28
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            AbstractButton {
                id: pathButton
                x: 48; y: parent.height - 100; width: parent.width - 96; height: 89
                onClicked: chooser.goUp()
                background: Rectangle { color: pathButton.down ? "#20ffffff" : "transparent"; radius: 8 }
                contentItem: Text {
                    text: filesBackend ? filesBackend.breadcrumb : "Data"
                    color: panel.ink; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 34
                    elide: Text.ElideLeft; verticalAlignment: Text.AlignVCenter
                    leftPadding: 9
                }
            }
        }

        PhotoChooser {
            id: galleryContent
            anchors.fill: parent
            visible: chooser.sourcePage === "gallery"
            enabled: visible
            z: 1
            backend: chooser.backend
            displayedMode: chooser.displayedMode
            albumFilter: chooser.galleryAlbumFilter
            selectedTokens: chooser.selectedGalleryTokens
            ink: panel.ink
            mutedInk: panel.mutedInk
            lightTheme: chooser.lightTheme
            albums: chooser.galleryAlbums
            items: chooser.visibleGalleryItems
            galleryTab: chooser.galleryTab
            searchOpen: chooser.searchOpen
            onBackRequested: chooser.backFromGallery()
            onAlbumSelected: album => { chooser.galleryAlbumFilter = album; chooser.galleryTab = "photos" }
            onItemSelected: token => chooser.selectGalleryItem(token)
            onTabSelected: tab => { chooser.galleryTab = tab; if (tab === "albums") chooser.galleryAlbumFilter = "" }
        }
        MouseArea {
            anchors.fill: navigationContent
            visible: chooser.navigating
            preventStealing: true
        }
        Image {
            id: previousContent
            objectName: "portalPreviousFolderContent"
            anchors.fill: navigationContent
            visible: chooser.navigating && source.toString().length > 0
            opacity: 0
            fillMode: Image.Stretch
            cache: false
            onStatusChanged: {
                if (!chooser.navigating || !chooser.navigationSnapshot) return
                if (status === Image.Ready) chooser.beginFolderCrossfade()
                else if (status === Image.Error) chooser.navigateWithoutSnapshot()
            }
        }
        TextField {
            id: filenameField
            visible: chooser.sourcePage === "files" && chooser.displayedMode === "SaveFile"
            enabled: !chooser.navigating
            x: 48; y: footerDivider.y - 116; width: parent.width - 96; height: 54
            placeholderText: "Filename"
            color: panel.ink; placeholderTextColor: panel.mutedInk
            font.family: light.name; font.pixelSize: 30
            background: Rectangle { color: "#22000000"; border.color: panel.ink; radius: 8 }
            onAccepted: chooser.confirm()
        }
        Text {
            id: message
            x: 51; y: footerDivider.y - 47; width: parent.width - 102; height: 36
            color: chooser.lightTheme ? "#a6283d" : "#fff3f3"
            font.family: light.name; font.pixelSize: 22
            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
            visible: chooser.sourcePage === "files"
        }
        Rectangle {
            id: footerDivider
            x: 48; y: parent.height - 102; width: parent.width - 96; height: 2
            color: panel.ink; opacity: 0.8
            visible: chooser.sourcePage !== "source"
        }
        AbstractButton {
            id: confirmButton
            objectName: "portalConfirmButton"
            visible: chooser.sourcePage === "files" && chooser.displayedMode !== "OpenFile"
                && (chooser.displayedMode !== "OpenFiles" || chooser.selectedPaths.length > 0)
            enabled: backend && !backend.busy && !chooser.navigating
            x: panel.width - 217; y: panel.height - 88; width: 173; height: 57
            onClicked: chooser.confirm()
            background: Rectangle {
                color: confirmButton.down ? "#a9d9dfe0" : "#9be5e8e9"; radius: 12
            }
            contentItem: Text {
                text: chooser.displayedMode === "SaveFile" ? "Save"
                    : chooser.displayedMode === "SelectDirectory" ? "Select" : "Open"
                color: chooser.lightTheme ? "#25313a" : "white"
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 29
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
        }
        AbstractButton {
            id: galleryConfirmButton
            objectName: "portalGalleryConfirmButton"
            visible: chooser.sourcePage === "gallery" && chooser.displayedMode === "OpenFiles"
                && chooser.selectedGalleryTokens.length > 0
            enabled: chooser.backend && !chooser.backend.busy
            x: panel.width - 217; y: panel.height - 178; width: 173; height: 57
            onClicked: chooser.backend.chooseGallery(chooser.selectedGalleryTokens)
            background: Rectangle { color: galleryConfirmButton.down ? "#a9d9dfe0" : "#9be5e8e9"; radius: 12 }
            contentItem: Text {
                text: qsTr("Open (%1)").arg(chooser.selectedGalleryTokens.length)
                color: chooser.lightTheme ? "#25313a" : "white"
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 25
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
        }

        // Dismissal layer prevents taps on the menu from reaching file rows.
        MouseArea {
            x: 0; y: 79; width: parent.width; height: parent.height - y
            visible: chooser.menuOpen; enabled: visible; preventStealing: true
            onClicked: chooser.menuOpen = false
        }
        Rectangle {
            x: menu.x + 8; y: menu.y + 9; width: menu.width; height: menu.height
            radius: 42; topRightRadius: 0
            color: "#35000000"
            visible: menu.visible
            opacity: menu.opacity * 0.65
        }
        Rectangle {
            id: menu
            objectName: "portalActionsMenu"
            property real reveal: chooser.menuOpen ? 1 : 0
            Behavior on reveal { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            visible: chooser.menuOpen || reveal > 0.001
            enabled: chooser.menuOpen && reveal > 0.95
            x: panel.width - width - 98; y: 70; width: 317
            height: chooser.menuPage === "main" ? 312
                : chooser.menuPage === "view" ? 156 : 260
            radius: 42; topRightRadius: 0
            color: chooser.lightTheme ? "#e5e6e8e8" : "#dfbec1c2"
            border.color: "#8cffffff"
            opacity: Math.min(1, reveal * 2)
            transform: Scale {
                origin.x: menu.width; origin.y: 0
                xScale: Math.max(0.02, menu.reveal)
                yScale: Math.max(0.02, menu.reveal)
            }
            MouseArea { anchors.fill: parent; preventStealing: true }
            Column {
                anchors.fill: parent
                Repeater {
                    model: chooser.menuPage === "main"
                        ? ["View as", filesBackend && filesBackend.showHidden
                           ? "Hide hidden files" : "Show hidden files",
                           "Sort", "Details", "Search", "About This Portal"]
                        : chooser.menuPage === "view"
                          ? ["‹ Back", "List", "Compact"]
                          : ["‹ Back", "Name", "Newest", "Size"]
                    delegate: AbstractButton {
                        required property int index
                        required property string modelData
                        width: menu.width
                        height: menu.height / (chooser.menuPage === "main" ? 6
                                               : chooser.menuPage === "view" ? 3 : 4)
                        onClicked: {
                            if (chooser.menuPage === "main") {
                                if (index === 0) chooser.menuPage = "view"
                                else if (index === 1 && filesBackend) {
                                    chooser.menuOpen = false
                                    chooser.selectedPaths = []
                                    filesBackend.setShowHidden(!filesBackend.showHidden)
                                } else if (index === 2) chooser.menuPage = "sort"
                                else if (index === 3) {
                                    chooser.showDetails = !chooser.showDetails
                                    chooser.menuOpen = false
                                } else if (index === 4) {
                                    chooser.menuOpen = false
                                    chooser.searchOpen = true
                                    searchField.forceActiveFocus()
                                } else if (index === 5) {
                                    chooser.menuOpen = false
                                    chooser.aboutOpen = true
                                }
                            } else if (index === 0) {
                                chooser.menuPage = "main"
                            } else if (chooser.menuPage === "view") {
                                chooser.compactRows = index === 2
                                chooser.menuOpen = false
                            } else if (filesBackend) {
                                filesBackend.setSortMode(index - 1)
                                chooser.menuOpen = false
                            }
                        }
                        background: Rectangle { color: parent.down ? "#26ffffff" : "transparent"; radius: 12 }
                        contentItem: Text {
                            text: modelData; color: panel.ink
                            font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 33
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
            Rectangle {
                visible: chooser.menuPage === "main"
                x: 22; y: 259; width: parent.width - 44; height: 2
                color: panel.ink; opacity: 0.5
            }
            Text {
                visible: chooser.menuPage === "main"
                x: 17; y: 5; height: 52
                text: "‹"; color: panel.ink; font.pixelSize: 31
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                visible: chooser.menuPage === "main"
                x: 17; y: 109; height: 52
                text: "‹"; color: panel.ink; font.pixelSize: 31
                verticalAlignment: Text.AlignVCenter
            }
        }
        Rectangle {
            visible: chooser.aboutOpen
            anchors.fill: parent
            color: "#47000000"
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                onClicked: chooser.aboutOpen = false
            }
        }
        Rectangle {
            id: aboutPrompt
            objectName: "portalAboutPrompt"
            visible: chooser.aboutOpen
            x: 72; y: (panel.height - height) / 2
            width: panel.width - 144; height: 230
            radius: 30
            color: chooser.lightTheme ? "#e8f2f3f3" : "#df798287"
            border.color: "#99ffffff"
            MouseArea { anchors.fill: parent; preventStealing: true }
            Text {
                x: 25; y: 26; width: parent.width - 50
                text: "About This Portal"
                color: panel.ink; font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 37
            }
            Text {
                x: 25; y: 91; width: parent.width - 50
                wrapMode: Text.WordWrap
                text: "OpenWave grants an app access only to the file or folder you select."
                color: panel.ink; font.family: light.name; font.pixelSize: 25
            }
            AbstractButton {
                x: parent.width - 112; y: parent.height - 60; width: 90; height: 45
                onClicked: chooser.aboutOpen = false
                background: Item {}
                contentItem: Text {
                    text: "Close"; color: panel.ink
                    font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 28
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
