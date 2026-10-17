import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects

ApplicationWindow {
    id: root
    required property var library
    property var portalClient: null
    property var wallpaperClient: null
    property bool openImportedPhoto: false
    property bool showStatusBar: false
    property bool captureMode: false
    property bool reducedMotion: false
    property string initialView: "photos"
    property int selectedIndex: 0
    property string currentPage: initialView === "albums" ? "albums" : "photos"
    property bool viewerOpen: initialView === "viewer" && library.photos.length > 0
    property bool searching: false
    property bool selecting: false
    property var selected: ({})
    property var favorites: ({})
    property string albumFilter: ""
    property real zoom: 1
    property bool slideshow: false
    property bool infoOpen: false
    readonly property var photo: library.photos[selectedIndex] || ({})
    readonly property real uiScale: Math.min(width / 768, height / 1366)
    readonly property var visiblePhotos: library.photos.map((photo, index) => Object.assign({}, photo, {index: index}))
        .filter(photo => (!albumFilter.length || photo.album === albumFilter)
            && (!searching || (photo.title + " " + photo.album).toLowerCase().includes(search.text.toLowerCase())))
    readonly property var sections: {
        const result = []
        for (const photo of visiblePhotos) {
            let section = result.find(group => group.label === photo.dateLabel)
            if (!section) { section = {label: photo.dateLabel, photos: []}; result.push(section) }
            section.photos.push(photo)
        }
        return result
    }
    width: 768; height: 1366
    visible: true; title: qsTr("Gallery"); color: "#454e53"
    flags: Qt.Window | Qt.FramelessWindowHint
    Component.onCompleted: if (library.error.length) imageError.open()
    function openPhoto(index) {
        if (index < 0 || index >= library.photos.length) return
        selectedIndex = index; zoom = 1; infoOpen = false; viewerOpen = true
    }
    function step(direction) {
        if (library.photos.length) openPhoto((selectedIndex + direction + library.photos.length) % library.photos.length)
    }
    function back() {
        if (menu.opened) { menu.close(); return }
        if (infoOpen) { infoOpen = false; return }
        if (viewerOpen) { viewerOpen = false; slideshow = false; return }
        if (selecting) { selecting = false; selected = ({}); return }
        if (searching) { searching = false; return }
        if (albumFilter.length) { albumFilter = ""; currentPage = "albums"; return }
        Qt.quit()
    }
    function toggleSelection(index) {
        const changed = Object.assign({}, selected)
        if (changed[index]) delete changed[index]; else changed[index] = true
        selected = changed
    }
    function choosePhotos(single) {
        if (!portalClient) { if (single) imageDialog.open(); else folderDialog.open(); return }
        if (portalClient.busy) return
        openImportedPhoto = single
        const options = {title: qsTr("Choose photos"), mimeTypes: library.supportedMimeTypes, requestIdentifier: "gallery-photos"}
        if (single) portalClient.openFile(options); else portalClient.openFiles(options)
    }
    Shortcut { sequences: ["Escape", "Back"]; onActivated: root.back() }
    Shortcut { sequence: "Right"; enabled: root.viewerOpen; onActivated: root.step(1) }
    Shortcut { sequence: "Left"; enabled: root.viewerOpen; onActivated: root.step(-1) }
    QtObject {
        id: theme
        readonly property color accent: "#00b7f0"
        readonly property color muted: "#a6ffffff"
        readonly property color surface: "#bd9a9e9f"
        readonly property color viewerBackground: "#161d21"
    }
    component GlyphButton: AbstractButton {
        id: control
        property string kind: "menu"
        property color glyphColor: "white"
        width: 72; height: 72
        background: Rectangle { radius: 36; color: control.down ? "#20ffffff" : "transparent" }
        contentItem: Item {
            GalleryGlyph { anchors.centerIn: parent; width: 40; height: 40; kind: control.kind; color: control.glyphColor }
        }
    }
    component TextAction: AbstractButton {
        id: control
        contentItem: Text {
            text: control.text; color: "white"; font.family: root.font.family
            font.pixelSize: 32; font.weight: Font.Light
            verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle { radius: 28; color: control.down ? "#20ffffff" : "transparent" }
    }
    Item {
        id: screen
        width: root.width / root.uiScale; height: root.height / root.uiScale
        scale: root.uiScale; transformOrigin: Item.TopLeft
        clip: true
        Image {
            id: backdrop
            anchors.fill: parent
            source: root.wallpaperClient ? root.wallpaperClient.source : root.library.photos.length ? root.library.photos[0].url : ""
            fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(768, 1366)
            visible: GraphicsInfo.api === GraphicsInfo.Software
            layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
            asynchronous: true
        }
        MultiEffect { anchors.fill: parent; visible: GraphicsInfo.api !== GraphicsInfo.Software; source: backdrop; blurEnabled: true; blurMax: 64; blur: 1; autoPaddingEnabled: false }
        Rectangle { anchors.fill: parent; color: "#dd454e53" }

        Item {
            id: status
            visible: root.showStatusBar; height: root.showStatusBar || root.portalClient ? 70 : 0; width: parent.width; z: 20
            Rectangle { x: 22; y: 23; width: 48; height: 25; radius: 2; color: "white" }
            Rectangle { x: 70; y: 30; width: 5; height: 11; color: "white" }
            Text { x: 94; y: 10; text: Qt.formatTime(new Date(), "h:mm AP"); color: "white"; font.pixelSize: 36 }
            GalleryGlyph { x: 365; y: 19; width: 36; height: 36; kind: "down" }
            GalleryGlyph { x: 710; y: 14; width: 44; height: 44; kind: "wifi" }
            Row {
                x: 626; y: 29; spacing: 3
                Repeater { model: 5; Rectangle { width: 12; height: 12; radius: 6; color: "white" } }
            }
        }
        Item {
            id: collection
            x: 0; y: status.height; width: parent.width; height: parent.height - y
            visible: !root.viewerOpen
            Text {
                x: 48; y: 22
                text: root.albumFilter.length ? root.albumFilter : qsTr("Gallery")
                color: "white"; font.pixelSize: 64; font.weight: Font.ExtraLight
                width: 490; elide: Text.ElideRight
            }
            Text {
                x: 50; y: 101
                text: qsTr("%1 photos · %2 albums").arg(root.visiblePhotos.length).arg(root.library.albums.length)
                color: theme.muted; font.pixelSize: 27; font.weight: Font.Light
            }
            GlyphButton {
                x: 566; y: 41; kind: "search"; Accessible.name: qsTr("Search photos")
                onClicked: { root.searching = !root.searching; if (root.searching) search.forceActiveFocus() }
            }
            GlyphButton {
                id: menuButton
                x: 654; y: 41; kind: "menu"; Accessible.name: qsTr("Gallery menu")
                onClicked: menu.open()
            }
            Rectangle { x: 40; y: 158; width: parent.width - 80; height: 1; color: "#80ffffff" }
            TextField {
                id: search
                objectName: "gallerySearch"
                x: 48; y: 178; width: parent.width - 96; height: 65
                visible: root.searching
                placeholderText: qsTr("Search photos or albums"); color: "white"
                font.pixelSize: 32; font.weight: Font.Light
                background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.muted } }
            }
            ListView {
                id: timeline
                objectName: "galleryTimeline"
                x: 44; y: root.searching ? 264 : 194; width: parent.width - 88
                height: parent.height - y - 156
                visible: root.currentPage === "photos"
                model: root.sections
                spacing: 30; clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Column {
                    id: section
                    required property var modelData
                    width: timeline.width; spacing: 16
                    Row {
                        width: parent.width; spacing: 16
                        Text { text: section.modelData.label; color: "white"; font.pixelSize: 34; font.weight: Font.Light }
                        Text { text: section.modelData.photos.length; color: theme.muted; font.pixelSize: 25; anchors.baseline: parent.children[0].baseline }
                    }
                    Grid {
                        width: parent.width; columns: 3; spacing: 12
                        Repeater {
                            model: section.modelData.photos
                            PhotoTile {
                                required property var modelData
                                width: (timeline.width - 24) / 3; height: width
                                photoSource: modelData.url; label: modelData.title
                                selecting: root.selecting; selected: !!root.selected[modelData.index]
                                renderScale: root.uiScale
                                onClicked: root.selecting ? root.toggleSelection(modelData.index) : root.openPhoto(modelData.index)
                                onPressAndHold: { root.selecting = true; root.toggleSelection(modelData.index) }
                            }
                        }
                    }
                }
            }
            GridView {
                id: albums
                objectName: "galleryAlbums"
                x: 44; y: 194; width: parent.width - 88; height: parent.height - y - 156
                visible: root.currentPage === "albums"
                cellWidth: width / 2; cellHeight: cellWidth + 100
                model: root.library.albums; clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                delegate: Column {
                    required property var modelData
                    width: albums.cellWidth - 20; spacing: 8
                    PhotoTile {
                        width: parent.width; height: width; photoSource: parent.modelData.cover
                        label: parent.modelData.name; renderScale: root.uiScale
                        onClicked: { root.albumFilter = parent.modelData.name; root.currentPage = "photos" }
                    }
                    Text { text: parent.modelData.name; width: parent.width; elide: Text.ElideRight; color: "white"; font.pixelSize: 32; font.weight: Font.Light }
                    Text { text: qsTr("%1 photos").arg(parent.modelData.count); color: theme.muted; font.pixelSize: 26 }
                }
            }
            Column {
                x: 70; y: 420; width: parent.width - 140; spacing: 28
                visible: root.visiblePhotos.length === 0
                Image { width: 160; height: 160; source: "assets/icon.svg"; anchors.horizontalCenter: parent.horizontalCenter }
                Text { width: parent.width; text: root.searching ? qsTr("No matching photos") : qsTr("Your photos, in one place"); color: "white"; font.pixelSize: 42; font.weight: Font.ExtraLight; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
                TextAction { width: parent.width; height: 70; text: root.portalClient ? qsTr("Add photos") : qsTr("Choose a folder"); onClicked: root.choosePhotos(false) }
            }
            Rectangle {
                x: 100; anchors.bottom: parent.bottom; anchors.bottomMargin: 44
                width: parent.width - 200; height: 88; radius: 44; color: theme.surface
                Row {
                    anchors.fill: parent; anchors.margins: 8
                    TextAction {
                        width: parent.width / 2; height: parent.height
                        text: root.selecting ? qsTr("%1 selected").arg(Object.keys(root.selected).length) : qsTr("Photos")
                        enabled: !root.selecting
                        onClicked: { root.currentPage = "photos"; root.albumFilter = "" }
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; width: 70; height: 2; color: theme.accent; visible: !root.selecting && root.currentPage === "photos" }
                    }
                    TextAction {
                        width: parent.width / 2; height: parent.height
                        text: root.selecting ? qsTr("Cancel") : qsTr("Albums")
                        onClicked: { if (root.selecting) { root.selecting = false; root.selected = ({}) } else { root.currentPage = "albums"; root.albumFilter = "" } }
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; width: 70; height: 2; color: theme.accent; visible: !root.selecting && root.currentPage === "albums" }
                    }
                }
            }
        }
        Rectangle {
            objectName: "galleryViewerBackground"
            anchors.fill: parent
            color: theme.viewerBackground
            opacity: viewer.opacity
            visible: viewer.visible
        }
        Item {
            id: viewer
            objectName: "galleryViewer"
            x: 0; y: status.height; width: parent.width; height: parent.height - y
            opacity: root.viewerOpen ? 1 : 0; visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: root.captureMode ? 0 : 220; easing.type: Easing.InOutQuad } }
            GlyphButton { x: 24; y: 16; kind: "back"; Accessible.name: qsTr("Back to photos"); onClicked: root.back() }
            CrossfadeTitle {
                objectName: "galleryPhotoTitle"
                x: 110; y: 20; width: parent.width - 220; height: 40
                text: root.photo.title || ""
                color: "white"; font: Qt.font({family: root.font.family, pixelSize: 30})
                animate: root.viewerOpen && !root.captureMode && !root.reducedMotion
            }
            Text { x: 110; y: 64; width: parent.width - 220; text: root.library.photos.length ? (root.selectedIndex + 1) + " / " + root.library.photos.length : ""; color: theme.muted; font.pixelSize: 23; horizontalAlignment: Text.AlignHCenter }
            GlyphButton { x: parent.width - 96; y: 16; kind: "info"; Accessible.name: qsTr("Photo information"); onClicked: root.infoOpen = !root.infoOpen }
            PhotoPager {
                id: pager
                objectName: "galleryPhotoPager"
                x: 0; y: 116; width: parent.width; height: parent.height - y - 172
                photo: root.photo
                previousPhoto: root.library.photos[(root.selectedIndex - 1 + root.library.photos.length) % root.library.photos.length] || ({})
                nextPhoto: root.library.photos[(root.selectedIndex + 1) % root.library.photos.length] || ({})
                active: root.viewerOpen && root.visible
                canNavigate: root.library.photos.length > 1
                captureMode: root.captureMode; reducedMotion: root.reducedMotion
                zoom: root.zoom
                onZoomRequested: value => root.zoom = value
                onStepRequested: direction => root.step(direction)
            }
            Rectangle {
                x: 64; width: parent.width - 128; height: 90; radius: 45
                anchors.bottom: parent.bottom; anchors.bottomMargin: 48; color: theme.surface
                Row {
                    anchors.centerIn: parent; spacing: 30
                    GlyphButton { kind: "back"; Accessible.name: qsTr("Previous photo"); onClicked: root.step(-1) }
                    GlyphButton {
                        kind: "heart"; glyphColor: root.favorites[root.selectedIndex] ? "#ff9dbb" : "white"
                        Accessible.name: qsTr("Favorite photo")
                        onClicked: { const next = Object.assign({}, root.favorites); next[root.selectedIndex] = !next[root.selectedIndex]; root.favorites = next }
                    }
                    GlyphButton { kind: root.slideshow ? "pause" : "play"; Accessible.name: qsTr("Toggle slideshow"); onClicked: root.slideshow = !root.slideshow }
                    GlyphButton { kind: "next"; Accessible.name: qsTr("Next photo"); onClicked: root.step(1) }
                }
            }
            Rectangle {
                x: 48; width: parent.width - 96; height: information.implicitHeight + 52; radius: 22
                anchors.bottom: parent.bottom; anchors.bottomMargin: 158
                visible: root.infoOpen; color: theme.surface
                Column {
                    id: information
                    x: 26; y: 26; width: parent.width - 52; spacing: 12
                    Text { text: root.photo.title || ""; color: "white"; font.pixelSize: 32; width: parent.width; wrapMode: Text.Wrap }
                    Text { text: [root.photo.format, root.photo.dimensions, root.photo.size].filter(Boolean).join(" · "); color: "white"; font.pixelSize: 25; width: parent.width; wrapMode: Text.Wrap }
                    Text { text: root.photo.timestamp || ""; color: "white"; font.pixelSize: 25; width: parent.width; wrapMode: Text.Wrap }
                }
            }
        }
        Rectangle {
            visible: root.showStatusBar; width: 232; height: 8; radius: 4
            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 9
            color: "white"; z: 30
        }
        Popup {
            id: menu
            scale: root.uiScale
            x: 390; y: status.height + 116; width: 330; padding: 18
            transformOrigin: Item.TopRight
            background: Rectangle { radius: 30; color: "#ed9a9e9f"; border.color: "#40ffffff" }
            contentItem: Column {
                Repeater {
                    model: [qsTr("Open image"), root.portalClient ? qsTr("Add photos") : qsTr("Choose folder"), qsTr("Select photos"), qsTr("Supported formats"), qsTr("About Gallery")]
                    TextAction {
                        required property int index
                        required property string modelData
                        width: parent.width; height: 66; text: modelData
                        onClicked: {
                            menu.close()
                            if (index === 0) root.choosePhotos(true)
                            if (index === 1) root.choosePhotos(false)
                            if (index === 2) { root.currentPage = "photos"; root.selecting = true }
                            if (index === 3) formatsDialog.open()
                            if (index === 4) aboutDialog.open()
                        }
                    }
                }
            }
            enter: Transition { NumberAnimation { property: "scale"; from: 0.02; to: 1; duration: 180; easing.type: Easing.OutCubic } }
            exit: Transition { NumberAnimation { property: "scale"; to: 0.02; duration: 140; easing.type: Easing.InCubic } }
        }
    }
    Timer { interval: 4000; running: root.slideshow && root.viewerOpen && root.visible; repeat: true; onTriggered: root.step(1) }
    FileDialog { id: imageDialog; title: qsTr("Open image"); onAccepted: { const index = root.library.openImage(selectedFile); if (index >= 0) root.openPhoto(index) } }
    FolderDialog { id: folderDialog; title: qsTr("Choose photo folder"); onAccepted: { root.library.loadFolder(selectedFolder); root.albumFilter = ""; root.currentPage = "photos" } }
    Dialog {
        id: formatsDialog
        anchors.centerIn: parent; width: Math.min(root.width - 60, 640)
        title: qsTr("Supported image formats"); standardButtons: Dialog.Ok; modal: true
        contentItem: Text { text: root.library.supportedFormats.join(" · "); wrapMode: Text.Wrap; font.pixelSize: 24; color: "white" }
        background: Rectangle { color: "#ee727d82"; radius: 22 }
    }
    Dialog {
        id: aboutDialog
        anchors.centerIn: parent; width: Math.min(root.width - 60, 640)
        title: qsTr("Gallery"); standardButtons: Dialog.Ok; modal: true
        contentItem: Text { text: qsTr("OpenWave photo viewer\ncom.openwave.photos\nQt Quick concept · 0.1"); wrapMode: Text.Wrap; font.pixelSize: 24; color: "white" }
        background: Rectangle { color: "#ee727d82"; radius: 22 }
    }
    Connections {
        target: root.library
        function onChanged() { if (root.library.error.length) imageError.open() }
    }
    Connections {
        target: root.portalClient
        function onCompleted(grants) {
            let first = -1
            for (const grant of grants) {
                const index = root.library.importImage(grant.temporaryCopy(), grant.name)
                if (first < 0 && index >= 0) first = index
            }
            root.currentPage = "photos"; root.albumFilter = ""
            if (root.openImportedPhoto && first >= 0) root.openPhoto(first)
            root.openImportedPhoto = false
        }
        function onCancelled() { root.openImportedPhoto = false }
        function onErrorOccurred(message) { root.openImportedPhoto = false; portalErrorMessage.text = message; portalError.open() }
    }
    Dialog {
        id: portalError
        anchors.centerIn: parent; width: Math.min(root.width - 60, 640)
        title: qsTr("Unable to open photos"); standardButtons: Dialog.Ok; modal: true
        contentItem: Text { id: portalErrorMessage; wrapMode: Text.Wrap; font.pixelSize: 24; color: "white" }
        background: Rectangle { color: "#ee727d82"; radius: 22 }
    }
    Dialog {
        id: imageError
        objectName: "galleryImageError"
        anchors.centerIn: parent; width: Math.min(root.width - 60, 640)
        title: qsTr("Unable to open image"); standardButtons: Dialog.Ok; modal: true
        contentItem: Text { text: root.library.error; wrapMode: Text.Wrap; font.pixelSize: 24; color: "white" }
        background: Rectangle { color: "#ee727d82"; radius: 22 }
    }
}
