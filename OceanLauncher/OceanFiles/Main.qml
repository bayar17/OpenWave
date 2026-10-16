import QtQuick
import OpenWave.Fonts 1.0
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects

ApplicationWindow {
    id: root
    width: 768; height: 1366
    flags: Qt.Window | Qt.FramelessWindowHint
    visible: true
    title: "Ocean Files"
    color: "#404b52"
    property var filesBackend: null
    property var mediaBackend: null
    property var promptClient: null
    property string replacementName: ""
    property string namingAction: ""
    property string namingDirectory: ""
    property string namingSource: ""
    property string deletionToken: ""
    property string promptError: ""
    readonly property real contentScale: Math.min(width / 768, height / 1366)
    property bool menuOpen: false
    readonly property bool naming: namingAction.length > 0
    property bool sorting: false
    property bool reduceMotion: false
    property bool navigating: false
    property bool navigationCommitted: false
    property bool pendingUp: false
    property string pendingPath: ""
    property var navigationSnapshot: null
    property int navigationGeneration: 0
    property real selectionReveal: filesBackend && filesBackend.selectionMode ? 1 : 0
    Behavior on selectionReveal {
        enabled: !root.reduceMotion
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    FontLoader { id: regular; source: "assets/fonts/Inter-Regular.otf" }
    FontLoader { id: light; source: "assets/fonts/Inter-Light.otf" }
    FontLoader { source: "assets/fonts/Inter-SemiBold.otf" }
    font.family: regular.name

    function back() {
        if (navigating) return
        if (naming) {
            namingAction = ""
            if (filesBackend) filesBackend.cancelDeletion()
            deletionToken = ""
            if (promptClient) promptClient.cancel()
            return
        }
        if (sorting) { sorting = false; return }
        if (menuOpen) { menuOpen = false; return }
        if (filesBackend && (filesBackend.previewVisible || filesBackend.selectionMode)) {
            filesBackend.up()
            searchInput.text = filesBackend.searchQuery
            return
        }
        if (filesBackend && filesBackend.currentPath !== filesBackend.rootPath) {
            navigateTo("", true)
            return
        }
        Qt.quit()
    }
    onReduceMotionChanged: {
        if (reduceMotion && navigating) {
            folderCrossfade.stop()
            if (!navigationCommitted) commitNavigation()
            resetNavigation()
        }
    }
    ParallelAnimation {
        id: folderCrossfade
        NumberAnimation {
            target: navigationContent; property: "opacity"
            from: 0; to: 1; duration: 220; easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: previousContent; property: "opacity"
            from: 1; to: 0; duration: 220; easing.type: Easing.InOutQuad
        }
        onFinished: root.resetNavigation()
    }
    function resetNavigation() {
        folderCrossfade.stop()
        navigationGeneration++
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
        searchInput.text = filesBackend.searchQuery
        fileList.positionViewAtBeginning()
        return true
    }
    function beginFolderCrossfade() {
        if (!navigating || navigationCommitted) return
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
        commitNavigation()
        resetNavigation()
    }
    function navigateTo(path, up) {
        if (navigating || !filesBackend) return
        pendingPath = path
        pendingUp = up
        navigating = true
        menuOpen = false
        searchInput.focus = false
        if (reduceMotion) {
            navigateWithoutSnapshot()
            return
        }
        const generation = ++navigationGeneration
        if (!navigationContent.grabToImage(function(result) {
            if (!root.navigating || root.navigationGeneration !== generation) return
            if (!result || !result.url) {
                root.navigateWithoutSnapshot()
                return
            }
            root.navigationSnapshot = result
            previousContent.source = result.url
            if (previousContent.status === Image.Ready)
                root.beginFolderCrossfade()
            else if (previousContent.status === Image.Error)
                root.navigateWithoutSnapshot()
        })) navigateWithoutSnapshot()
    }
    function activateRow(index, folder) {
        if (navigating || !filesBackend) return
        menuOpen = false
        if (folder && !filesBackend.selectionMode) {
            const path = filesBackend.entryPath(index)
            if (path) navigateTo(path, false)
            else filesBackend.activate(index)
        } else filesBackend.activate(index)
    }
    function holdSelectRow(index, selected) {
        if (navigating || naming || !filesBackend) return
        menuOpen = false
        searchInput.focus = false
        if (filesBackend.transferMode.length) filesBackend.cancelTransfer()
        filesBackend.setSelectionMode(true)
        if (!selected) filesBackend.activate(index)
    }
    function cancelOperations() {
        if (!filesBackend || naming || navigating) return
        filesBackend.setSelectionMode(false)
        filesBackend.cancelTransfer()
    }
    function operationAvailable(action) {
        if (!filesBackend) return false
        const selecting = filesBackend.selectionMode
        const transfer = filesBackend.transferMode
        if (action === "cancel") return selecting || transfer.length > 0
        if (action === "create_file") return !selecting && !transfer.length
        if (action === "create_folder") return !selecting
        if (action === "rename") return selecting && filesBackend.selectedCount === 1
        if (action === "move" || action === "copy")
            return transfer.length ? transfer === action : selecting && filesBackend.selectedCount > 0
        return selecting && filesBackend.selectedCount > 0
    }
    function performOperation(action) {
        if (!operationAvailable(action) || naming || navigating) return
        if (action === "cancel") cancelOperations()
        else if (action === "delete") beginDeletion()
        else if (action === "move" || action === "copy") {
            if (filesBackend.transferMode.length) completeTransfer()
            else beginTransfer(action)
        } else beginNaming(action)
    }
    function clockTime(milliseconds) {
        const seconds = Math.max(0, Math.floor(milliseconds / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
    function beginNaming(action) {
        if (!filesBackend || naming || (promptClient && promptClient.busy)) return
        promptError = ""
        if (!promptClient) { promptError = qsTr("The prompt service is unavailable."); return }
        menuOpen = false
        searchInput.focus = false
        namingDirectory = filesBackend.currentPath
        namingSource = ""
        if (action === "rename") {
            if (filesBackend.selectedCount !== 1) return
            namingSource = filesBackend.selectedName
            if (!namingSource) return
            action = filesBackend.selectedFolder ? "rename_folder" : "rename_file"
        }
        requestName(action, namingSource, "")
    }
    function requestName(action, initialText, message) {
        const folder = action === "create_folder" || action === "rename_folder"
        const rename = action === "rename_folder" || action === "rename_file"
        namingAction = action
        promptClient.textInput(rename ? (folder ? qsTr("Rename folder") : qsTr("Rename file"))
                                     : (folder ? qsTr("New folder") : qsTr("New file")), message,
            {initialText: initialText,
             placeholder: rename ? (folder ? qsTr("type new folder name") : qsTr("type new file name"))
                                 : (folder ? qsTr("type folder name") : qsTr("type file name")),
             acceptLabel: rename ? qsTr("Rename") : qsTr("Create"), cancelLabel: qsTr("Cancel"),
             minLength: 1, maxLength: 255, validationPattern: "[^/\\\\\\x00-\\x1f\\x7f]+"})
    }
    function beginDeletion() {
        if (!filesBackend || naming || navigating || filesBackend.selectedCount < 1
                || (promptClient && promptClient.busy)) return
        promptError = ""
        if (!promptClient) { promptError = qsTr("The prompt service is unavailable."); return }
        deletionToken = filesBackend.prepareDeletion()
        if (!deletionToken) return
        namingDirectory = filesBackend.currentPath
        namingAction = "delete"
        menuOpen = false
        searchInput.focus = false
        const single = filesBackend.selectedCount === 1
        promptClient.confirmation(single ? (filesBackend.selectedFolder ? qsTr("Delete folder?") : qsTr("Delete file?"))
                                        : qsTr("Delete selected items?"),
            single ? (filesBackend.selectedFolder
                        ? qsTr("Permanently delete “%1” and all its contents?").arg(filesBackend.selectedName)
                        : qsTr("Permanently delete “%1”?").arg(filesBackend.selectedName))
                   : qsTr("Permanently delete %1 selected items and any folder contents?").arg(filesBackend.selectedCount),
            {acceptLabel: qsTr("Delete"), cancelLabel: qsTr("Cancel")})
    }
    function beginTransfer(mode) {
        if (!filesBackend || naming || navigating || filesBackend.selectedCount < 1) return
        menuOpen = false
        promptError = ""
        filesBackend.prepareTransfer(mode)
    }
    function completeTransfer() {
        if (!filesBackend || naming || navigating) return
        menuOpen = false
        promptError = ""
        filesBackend.transferSelection(filesBackend.currentPath)
    }
    function finishNaming(result, text) {
        const action = namingAction
        namingAction = ""
        if (action === "delete") {
            const token = deletionToken
            deletionToken = ""
            if (result === "accepted" && filesBackend) {
                if (!filesBackend.deleteSelection(token, namingDirectory))
                    promptError = filesBackend.error || qsTr("The selected items could not be deleted.")
            } else {
                if (filesBackend) filesBackend.cancelDeletion()
                if (result === "timed_out" || result === "renderer_lost" || result === "service_lost")
                    promptError = qsTr("The prompt was interrupted. No files were changed.")
            }
            return
        }
        if (!action || result !== "accepted" || !filesBackend) {
            if (action && (result === "timed_out" || result === "renderer_lost" || result === "service_lost"))
                promptError = qsTr("The prompt was interrupted. No files were changed.")
            return
        }
        if (filesBackend.currentPath !== namingDirectory) {
            promptError = qsTr("The folder changed. Please try again.")
            return
        }
        const name = text.trim()
        if (action === "create_file" && filesBackend.fileExists(name)) {
            replacementName = name
            namingAction = "replace_file"
            promptClient.confirmation(qsTr("Replace file?"),
                qsTr("“%1” already exists. Replace it with an empty file?").arg(name),
                {acceptLabel: qsTr("Replace"), cancelLabel: qsTr("Cancel")})
            return
        }
        const ok = action === "replace_file" ? filesBackend.replaceFile(replacementName, namingDirectory)
            : action === "create_file" ? filesBackend.createFile(name)
            : action === "create_folder" ? filesBackend.createFolder(name)
            : filesBackend.renameItem(namingSource, name, namingDirectory)
        if (ok) promptError = ""
        else {
            promptError = filesBackend.error || qsTr("The operation could not be completed.")
            if (action !== "replace_file") requestName(action, text, promptError)
        }
    }
    Connections {
        target: root.promptClient
        function onErrorOccurred(message) {
            console.warn("Files prompt failed:", message)
            root.promptError = qsTr("The prompt could not be completed. No files were changed.")
        }
        function onFinished(requestId, result, text) {
            root.finishNaming(result, text)
        }
    }
    Shortcut { sequences: ["Escape", "Back"]; onActivated: root.back() }
    Connections {
        target: root.filesBackend
        function onPreviewChanged() {
            if (!root.mediaBackend) return
            if (root.filesBackend.previewAudio)
                root.mediaBackend.open(root.filesBackend.previewPath)
            else
                root.mediaBackend.stop()
        }
    }
    Timer {
        interval: 100; repeat: true
        running: root.filesBackend && root.filesBackend.previewAudio
        onTriggered: if (root.mediaBackend) root.mediaBackend.refreshPosition()
    }

    Item {
        id: screen
        objectName: "filesScreen"
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
        FrostedBackground { anchors.fill: parent; source: wallpaper; visible: wallpaper.status === Image.Ready }

        Item {
            x: 40; y: 98; width: 688; height: 60
            TextField {
                id: searchInput
                objectName: "filesSearch"
                x: 15; width: 540; height: 58
                placeholderText: "Search"
                color: "white"; placeholderTextColor: "white"
                font.family: light.name; font.pixelSize: 43
                leftPadding: 7; rightPadding: 7
                background: Item {}
                onTextChanged: if (root.filesBackend) root.filesBackend.setSearchQuery(text)
                onAccepted: focus = false
            }
            Item {
                x: 568; y: 5; width: 55; height: 50
                Image {
                    anchors.centerIn: parent; width: 31; height: 20
                    source: "assets/chevron.svg"
                    rotation: root.menuOpen ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                }
                TapHandler { onTapped: { root.menuOpen = !root.menuOpen; searchInput.focus = false } }
            }
            Item {
                x: 631; width: 57; height: 56
                Image { anchors.centerIn: parent; width: 43; height: 43; source: "assets/search.svg" }
                TapHandler { onTapped: searchInput.forceActiveFocus() }
            }
            Rectangle { y: 58; width: parent.width; height: 2; color: "#bcd0d4"; opacity: 0.85 }
        }

        Item {
            id: navigationContent
            objectName: "filesNavigationContent"
            anchors.fill: parent

            ListView {
                id: fileList
                objectName: "filesList"
                x: 80; y: 211; width: 608; height: 937
                clip: true
                model: root.filesBackend
                spacing: 15 * (Screen.pixelDensity > 0 ? Screen.pixelDensity : 3.78)
                boundsBehavior: Flickable.StopAtBounds
                delegate: Item {
                    id: row
                    objectName: "filesRow_" + index
                    required property int index
                    required property string name
                    required property bool folder
                    required property string detail
                    required property bool selected
                    required property bool audio
                    width: fileList.width; height: 92
                    Rectangle {
                        objectName: "filesSelectionCircle"
                        x: 0; y: 26; width: 40; height: 40; radius: 20
                        border.color: "#f2ffffff"; border.width: 1
                        color: row.selected ? "#003b91" : "transparent"
                        opacity: root.selectionReveal
                    }
                    Item {
                        objectName: "filesRowContents"
                        x: 38 * root.selectionReveal
                        width: parent.width - x; height: parent.height
                        Image {
                            x: 19; y: 22; width: 58; height: 52
                            source: row.folder ? "assets/folder-outline.svg"
                                : row.audio ? "assets/music-outline.svg" : "assets/file-outline.svg"
                            sourceSize.width: 116; sourceSize.height: 104
                        }
                        Text {
                            x: 91; y: 11; width: parent.width - x - 16; height: 42
                            text: row.name; color: "white"
                            font.family: regular.name; font.pixelSize: 33
                            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                        }
                        Text {
                            x: 91; y: 54; width: parent.width - x - 16; height: 27
                            text: row.detail; color: "#dde1e3"
                            font.family: light.name; font.pixelSize: 21
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle { y: 90; width: parent.width; height: 2; color: "#bcc7ca"; opacity: 0.75 }
                    TapHandler {
                        property bool held: false
                        longPressThreshold: 0.5
                        onPressedChanged: if (pressed) held = false
                        onLongPressed: {
                            held = true
                            root.holdSelectRow(row.index, row.selected)
                        }
                        onTapped: if (!held) root.activateRow(row.index, row.folder)
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: fileList.count === 0 && root.filesBackend && !root.filesBackend.error
                    text: searchInput.text.length ? "No matching files" : "This folder is empty"
                    color: "#dde1e3"; font.family: light.name; font.pixelSize: 30
                }
            }
    
            Item {
                x: 40; y: 1256; width: 688; height: 90
                Rectangle { width: parent.width; height: 2; color: "#b9c8cb"; opacity: 0.78 }
                Text {
                    x: 11; y: 18; width: 660; height: 55
                    text: root.filesBackend && root.filesBackend.selectionMode
                        ? qsTr("%1 selected · %2").arg(root.filesBackend.selectedCount).arg(root.filesBackend.breadcrumb)
                        : root.filesBackend && root.filesBackend.transferMode.length
                            ? (root.filesBackend.transferMode === "move" ? qsTr("Move %1 items · %2")
                                                                        : qsTr("Copy %1 items · %2"))
                                .arg(root.filesBackend.transferCount)
                                .arg(root.filesBackend.currentPath === root.filesBackend.rootPath ? qsTr("Data")
                                    : root.filesBackend.currentPath.split("/").pop())
                        : root.filesBackend ? root.filesBackend.breadcrumb : "Data"
                    color: "white"; font.family: light.name; font.pixelSize: 32
                    elide: root.filesBackend && root.filesBackend.transferMode.length ? Text.ElideRight : Text.ElideLeft
                }
                TapHandler { onTapped: {
                    if (!root.navigating && root.filesBackend
                            && root.filesBackend.currentPath !== root.filesBackend.rootPath)
                        root.navigateTo("", true)
                } }
            }
        }
        Image {
            id: previousContent
            objectName: "filesPreviousFolderContent"
            anchors.fill: navigationContent
            visible: root.navigating && source.toString().length > 0
            opacity: 0
            fillMode: Image.Stretch
            cache: false
            onStatusChanged: {
                if (!root.navigating || !root.navigationSnapshot) return
                if (status === Image.Ready) root.beginFolderCrossfade()
                else if (status === Image.Error) root.navigateWithoutSnapshot()
            }
        }
        Rectangle {
            id: operationsBar
            objectName: "filesOperationsBar"
            x: 56; y: 1164; width: 656; height: 80; radius: 40
            color: "#99adb0b2"
            Flickable {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                contentWidth: Math.max(width, operationsRow.width); contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                Row {
                    id: operationsRow
                    x: Math.max(0, (parent.width - width) / 2)
                    readonly property real naturalWidth: {
                        let total = 0
                        for (const child of children) total += child.preferredWidth || 0
                        return total
                    }
                    readonly property real fit: Math.max(22 / 30, Math.min(1,
                        (operationsBar.width - 32) / Math.max(1, naturalWidth)))
                    Repeater {
                        model: ["delete", "move", "copy", "rename", "create_folder", "create_file", "cancel"]
                        delegate: Item {
                            id: operation
                            required property string modelData
                            objectName: "filesOperation_" + modelData
                            visible: modelData !== "rename" || (root.filesBackend && root.filesBackend.selectionMode && root.filesBackend.selectedCount === 1)
                            readonly property string label: {
                                switch (modelData) {
                                case "delete": return qsTr("Delete")
                                case "move": return root.filesBackend && root.filesBackend.transferMode === "move" ? qsTr("Move here") : qsTr("Move")
                                case "copy": return root.filesBackend && root.filesBackend.transferMode === "copy" ? qsTr("Copy here") : qsTr("Copy")
                                case "rename": return qsTr("Rename")
                                case "create_folder": return qsTr("New Folder")
                                case "create_file": return qsTr("New File")
                                default: return qsTr("Cancel")
                                }
                            }
                            readonly property bool available: root.operationAvailable(modelData)
                            readonly property real preferredWidth: visible ? labelMetrics.advanceWidth + 16 : 0
                            property real inactive: available ? 0 : 1
                            Behavior on inactive {
                                enabled: !root.reduceMotion
                                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                            }
                            width: preferredWidth * operationsRow.fit; height: operationsBar.height
                            TextMetrics {
                                id: labelMetrics
                                text: operation.label
                                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 30
                            }
                            Text {
                                anchors.centerIn: parent
                                text: operation.label; color: "black"
                                font.family: Fonts.family; font.weight: Font.ExtraLight
                                font.pixelSize: 30 * operationsRow.fit
                                opacity: 1 - 0.5 * operation.inactive
                                layer.enabled: operation.inactive > 0.001
                                layer.effect: MultiEffect {
                                    blurEnabled: true; blurMax: 8; blur: 0.4 * operation.inactive
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: operation.available && !root.naming && !root.navigating
                                onClicked: root.performOperation(operation.modelData)
                            }
                        }
                    }
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            visible: root.navigating
            preventStealing: true
        }

        MouseArea {
            x: 0; y: 157; width: screen.width; height: screen.height - y
            visible: root.menuOpen || menu.reveal > 0.001
            enabled: visible
            preventStealing: true
            onClicked: root.menuOpen = false
        }

        Rectangle {
            id: menu
            objectName: "filesMenu"
            readonly property string hiddenFoldersAction: root.filesBackend && root.filesBackend.showHidden
                ? qsTr("Hide hidden folders") : qsTr("Show hidden folders")
            property real reveal: root.menuOpen ? 1 : 0
            Behavior on reveal { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
            visible: root.menuOpen || reveal > 0.001
            enabled: root.menuOpen && reveal > 0.95
            x: 649 - width; y: 157
            width: Math.max(250, Math.ceil(Math.max(showHiddenMetrics.advanceWidth,
                                                   hideHiddenMetrics.advanceWidth)) + 40)
            height: menuActions.count * 82 + 3
            radius: 42; topRightRadius: 0
            color: "#787e80"; border.color: "#859da3a7"
            opacity: Math.min(1, reveal * 2)
            transform: Scale {
                origin.x: menu.width; origin.y: 0
                xScale: Math.max(0.02, menu.reveal)
                yScale: Math.max(0.02, menu.reveal)
            }
            TextMetrics {
                id: showHiddenMetrics
                text: qsTr("Show hidden folders")
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 43
            }
            TextMetrics {
                id: hideHiddenMetrics
                text: qsTr("Hide hidden folders")
                font: showHiddenMetrics.font
            }
            Column {
                anchors.fill: parent
                Repeater {
                    id: menuActions
                    model: {
                        return [{key: "sort", label: qsTr("Sort"), enabled: true},
                                {key: "hidden", label: menu.hiddenFoldersAction, enabled: true}]
                    }
                    delegate: Item {
                        required property int index
                        required property var modelData
                        objectName: "filesMenuAction_" + modelData.key
                        width: menu.width; height: 82
                        Text {
                            objectName: "filesMenuLabel"
                            anchors.centerIn: parent; text: modelData.label
                            color: "white"
                            opacity: modelData.enabled ? 1 : 0.45
                            font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 43
                        }
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            onClicked: {
                                if (!modelData.enabled) return
                                root.menuOpen = false
                                const action = modelData.key
                                if (action === "sort") root.sorting = true
                                else if (action === "hidden" && root.filesBackend)
                                    root.filesBackend.setShowHidden(!root.filesBackend.showHidden)
                            }
                        }
                    }
                }
            }
        }

        MouseArea { anchors.fill: parent; visible: root.naming; preventStealing: true }
        Rectangle {
            visible: root.sorting || (root.filesBackend && root.filesBackend.previewVisible)
            anchors.fill: parent; color: "#990d161b"
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                onClicked: {
                    root.sorting = false
                    if (root.filesBackend) root.filesBackend.closePreview()
                }
            }
        }
        Rectangle {
            objectName: "filesPrompt"
            visible: root.sorting || (root.filesBackend && root.filesBackend.previewVisible)
            x: root.filesBackend && root.filesBackend.previewAudio ? 60 : 78
            y: root.filesBackend && root.filesBackend.previewAudio ? 397 : 458
            width: root.filesBackend && root.filesBackend.previewAudio ? 648 : 612
            height: root.filesBackend && root.filesBackend.previewAudio ? 510 : root.sorting ? 385 : 305
            radius: 34; color: "#e93c4950"; border.color: "#8acbd5d8"
            MouseArea {
                anchors.fill: parent
                preventStealing: true
            }
            Text {
                x: 32; y: 23; width: parent.width - 64
                height: root.filesBackend && root.filesBackend.previewAudio ? 78 : 55
                text: root.sorting ? "Sort by"
                    : (root.filesBackend ? root.filesBackend.previewName : "")
                color: "white"; font.family: regular.name
                font.pixelSize: root.filesBackend && root.filesBackend.previewAudio ? 29 : 34
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                maximumLineCount: 2
            }
            Text {
                visible: !root.sorting
                x: 32; y: root.filesBackend && root.filesBackend.previewAudio ? 103 : 100
                width: 548; height: 55
                text: root.filesBackend ? root.filesBackend.previewDetail : ""
                color: "#dde1e3"; font.family: light.name; font.pixelSize: 28
            }
            Column {
                visible: root.sorting
                x: 32; y: 94; width: 548
                Repeater {
                    model: ["Name", "Newest", "Size"]
                    delegate: Item {
                        required property int index
                        required property string modelData
                        width: 548; height: 77
                        Text {
                            anchors.verticalCenter: parent.verticalCenter; text: modelData
                            color: root.filesBackend && root.filesBackend.sortMode === index ? "#0ac9ff" : "white"
                            font.family: light.name; font.pixelSize: 32
                        }
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            onClicked: {
                                if (root.filesBackend) root.filesBackend.setSortMode(index)
                                root.sorting = false
                            }
                        }
                    }
                }
            }
            Text {
                visible: !root.sorting
                x: 33; y: root.filesBackend && root.filesBackend.previewAudio ? 460 : 234
                width: 120; height: 46
                text: root.filesBackend && root.filesBackend.previewAudio ? "Close" : "Cancel"
                color: "#dde1e3"; font.pixelSize: 27
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (root.filesBackend) root.filesBackend.closePreview()
                    }
                }
            }
            Item {
                visible: root.filesBackend && root.filesBackend.previewAudio
                anchors.fill: parent
                Image {
                    x: 272; y: 166; width: 104; height: 104
                    source: "assets/music-outline.svg"
                    sourceSize.width: 208; sourceSize.height: 208
                }
                Text {
                    x: 60; y: 277; width: 528; height: 35
                    horizontalAlignment: Text.AlignHCenter
                    text: root.mediaBackend && root.mediaBackend.loading ? "Preparing audio…"
                        : root.mediaBackend ? root.mediaBackend.error : ""
                    color: "#e4e9ea"; font.family: light.name; font.pixelSize: 20
                    elide: Text.ElideRight
                }
                Rectangle {
                    x: 284; y: 300; width: 80; height: 80; radius: 40
                    color: "#15bfed"
                    opacity: root.mediaBackend && root.mediaBackend.loading ? 0.45 : 1
                    Text {
                        anchors.centerIn: parent
                        text: root.mediaBackend && root.mediaBackend.playing ? "Ⅱ" : "▶"
                        color: "white"; font.pixelSize: 35
                    }
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        onClicked: if (root.mediaBackend) root.mediaBackend.toggle()
                    }
                }
                Slider {
                    id: playbackSlider
                    x: 38; y: 392; width: 572; height: 42
                    from: 0
                    to: Math.max(1, root.mediaBackend ? root.mediaBackend.durationMs : 1)
                    value: root.mediaBackend ? root.mediaBackend.positionMs : 0
                    onMoved: if (root.mediaBackend) root.mediaBackend.seek(value)
                    background: Rectangle {
                        x: playbackSlider.leftPadding
                        y: playbackSlider.topPadding + playbackSlider.availableHeight / 2 - height / 2
                        width: playbackSlider.availableWidth; height: 5; radius: 3
                        color: "#748c9296"
                        Rectangle {
                            width: playbackSlider.visualPosition * parent.width
                            height: parent.height; radius: 3; color: "#19d4ff"
                        }
                    }
                    handle: Rectangle {
                        x: playbackSlider.leftPadding + playbackSlider.visualPosition
                            * (playbackSlider.availableWidth - width)
                        y: playbackSlider.topPadding + playbackSlider.availableHeight / 2 - height / 2
                        width: 22; height: 22; radius: 11; color: "white"
                    }
                }
                Text {
                    x: 39; y: 424; width: 120; height: 30
                    text: root.clockTime(root.mediaBackend ? root.mediaBackend.positionMs : 0)
                    color: "white"; font.family: light.name; font.pixelSize: 22
                }
                Text {
                    x: 489; y: 424; width: 120; height: 30
                    horizontalAlignment: Text.AlignRight
                    text: root.clockTime(root.mediaBackend ? root.mediaBackend.durationMs : 0)
                    color: "white"; font.family: light.name; font.pixelSize: 22
                }
            }
        }

        Rectangle {
            visible: root.promptError.length > 0 || (root.filesBackend && root.filesBackend.error.length > 0)
            x: 48; y: 1100; width: 672; height: 100; radius: 15; color: "#df293944"
            Text {
                anchors.fill: parent; anchors.margins: 15
                text: root.promptError || (root.filesBackend ? root.filesBackend.error : "")
                color: "white"; font.pixelSize: 22; wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                onClicked: { root.promptError = ""; if (root.filesBackend) root.filesBackend.clearError() }
            }
        }
    }
}
