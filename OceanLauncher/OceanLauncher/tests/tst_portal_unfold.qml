import QtQuick
import QtTest
import ".." as Ocean

Item {
    id: screen
    width: 768
    height: 1366

    QtObject {
        id: request
        property string requestId: ""
        property string title: "Select a file"
        property string mutedTitle: ""
        property string primaryTitle: ""
        property string mode: "OpenFile"
        property string initialDirectory: ""
        property string suggestedFilename: ""
        property bool busy: false
        property bool imageSources: false
        property string imageSource: ""
        property var mimeTypes: []
        property var galleryItems: []
        property bool galleryLoading: false
        property string galleryError: ""
        property int selections: 0
        property var lastGalleryTokens: []
        signal errorOccurred(string error)
        function acceptsPath(path) { return true }
        function choose(paths) { selections++ }
        function chooseGallery(tokens) { lastGalleryTokens = tokens; selections++ }
        function loadGallery() { galleryLoading = false }
        function galleryImageUrl(token) { return "image://test/" + token }
        function cancel() { requestId = "" }
    }
    ListModel {
        id: files
        property string currentPath: "/Data/Users/root"
        property string rootPath: "/Data/Users/root"
        property string breadcrumb: "Data"
        property string error: ""
        property bool showHidden: false
        function goToRoot() {
            currentPath = rootPath
            breadcrumb = "Data"
            clear()
            append({name: "Documents", folder: true, detail: "1 MB"})
            append({name: "Photo.png", folder: false, detail: "1 MB"})
        }
        function goToDirectory(path) {
            currentPath = path
            breadcrumb = "Data > Documents"
            clear()
            append({name: "Inside.png", folder: false, detail: "1 MB"})
            return true
        }
        function up() {
            if (currentPath === rootPath) return false
            goToRoot()
            return true
        }
        function setSearchQuery(value) {}
        function setShowHidden(value) { showHidden = value }
        function entryPath(index) {
            const entry = index >= 0 && index < count ? get(index) : null
            return entry ? currentPath + "/" + entry.name : ""
        }
        ListElement { name: "Documents"; folder: true; detail: "1 MB" }
        ListElement { name: "Photo.png"; folder: false; detail: "1 MB" }
    }

    Ocean.PortalFileChooser {
        id: chooser
        anchors.fill: parent
        backend: request
        filesBackend: files
    }

    TestCase {
        name: "PortalUnfold"
        when: windowShown

        function init() {
            chooser.reduceMotion = true
            request.requestId = ""
            wait(10)
            compare(chooser.phase, "hidden")
            chooser.reduceMotion = false
            request.selections = 0
            request.title = "Select a file"
            request.mutedTitle = ""
            request.primaryTitle = ""
            request.imageSources = false
            request.imageSource = ""
            request.mode = "OpenFile"
            request.mimeTypes = []
            request.galleryItems = []
            request.galleryError = ""
            request.galleryLoading = false
            request.lastGalleryTokens = []
        }

        function test_gradientTitleContent() {
            chooser.reduceMotion = true
            request.title = "Downloading File"
            request.requestId = "/request/title"
            wait(10)
            const title = findChild(chooser, "portalTitle")
            const rendered = findChild(chooser, "portalTitleText")
            const gradient = findChild(chooser, "portalTitleGradient")
            compare(title.effectiveMutedTitle, "Downloading")
            compare(title.effectivePrimaryTitle, "File")
            compare(rendered.text, "Downloading File")
            verify(title.gradientEndPosition > 0 && title.gradientEndPosition < 1)
            verify(gradient.visible)
            compare(rendered.font.pixelSize, 44)
            compare(title.renderedTitle, "Downloading File")

            request.requestId = ""
            wait(10)
            request.title = "Translated full title"
            request.mutedTitle = "Selecting"
            request.primaryTitle = "Folder"
            request.requestId = "/request/translated"
            wait(10)
            compare(title.effectiveMutedTitle, "Selecting")
            compare(title.effectivePrimaryTitle, "Folder")
            compare(rendered.text, "Selecting Folder")

            request.requestId = ""
            wait(10)
            request.title = "Confirm"
            request.mutedTitle = ""
            request.primaryTitle = ""
            request.requestId = "/request/single"
            wait(10)
            compare(title.effectiveMutedTitle, "")
            compare(title.effectivePrimaryTitle, "Confirm")
            compare(rendered.text, "Confirm")
            compare(title.gradientEndPosition, 0)
        }

        function test_folderCrossfadeAndBack() {
            chooser.reduceMotion = true
            request.requestId = "/request/folder"
            wait(10)
            chooser.reduceMotion = false
            const content = findChild(chooser, "portalNavigationContent")
            const oldContent = findChild(chooser, "portalPreviousFolderContent")
            compare(files.currentPath, files.rootPath)

            chooser.selected(0, true)
            verify(chooser.navigating)
            tryCompare(files, "currentPath", files.rootPath + "/Documents")
            verify(oldContent.visible)
            wait(65)
            verify(content.opacity > 0 && content.opacity < 1)
            verify(oldContent.opacity > 0 && oldContent.opacity < 1)
            tryCompare(chooser, "navigating", false)
            compare(content.opacity, 1)
            compare(oldContent.opacity, 0)

            chooser.goUp()
            tryCompare(files, "currentPath", files.rootPath)
            tryCompare(chooser, "navigating", false)
            compare(content.opacity, 1)
        }

        function test_reducedMotionFolderNavigation() {
            chooser.reduceMotion = true
            request.requestId = "/request/folder-reduced"
            wait(10)
            chooser.selected(0, true)
            compare(files.currentPath, files.rootPath + "/Documents")
            verify(!chooser.navigating)
            compare(findChild(chooser, "portalNavigationContent").opacity, 1)
        }

        function test_centerOutOpeningAndClosing() {
            const panel = findChild(chooser, "portalFileChooserPanel")
            const backdrop = findChild(chooser, "portalBackdrop")
            request.requestId = "/request/one"
            wait(10)
            compare(chooser.phase, "opening")
            verify(chooser.visible)
            verify(!panel.enabled)
            compare(panel.width, 699)
            compare(backdrop.y, 0)
            compare(backdrop.height, 1366)
            compare(panel.transform[0].xScale, 1)
            compare(panel.transform[0].origin.y, panel.height / 2)
            const center = panel.y + panel.height / 2
            const startTop = panel.mapToItem(screen, panel.width / 2, 0).y
            const startBottom = panel.mapToItem(screen, panel.width / 2, panel.height).y
            verify(Math.abs((startTop + startBottom) / 2 - center) < 1)
            wait(90)
            const top = panel.mapToItem(screen, panel.width / 2, 0).y
            const bottom = panel.mapToItem(screen, panel.width / 2, panel.height).y
            verify(top < center)
            verify(bottom > center)
            verify(Math.abs((top + bottom) / 2 - center) < 1)
            compare(panel.width, 699)
            compare(backdrop.y, 0)
            compare(backdrop.height, 1366)
            tryCompare(chooser, "phase", "open")
            compare(chooser.unfold, 1)
            verify(panel.enabled)

            request.requestId = ""
            wait(10)
            compare(chooser.phase, "closing")
            verify(chooser.visible)
            verify(!panel.enabled)
            wait(70)
            const closingTop = panel.mapToItem(screen, panel.width / 2, 0).y
            const closingBottom = panel.mapToItem(screen, panel.width / 2, panel.height).y
            verify(closingTop > panel.y)
            verify(closingBottom < panel.y + panel.height)
            verify(Math.abs((closingTop + closingBottom) / 2 - center) < 1)
            tryCompare(chooser, "phase", "hidden")
            verify(!chooser.visible)
        }

        function test_reducedMotion() {
            chooser.reduceMotion = true
            request.requestId = "/request/reduced"
            wait(10)
            compare(chooser.phase, "open")
            compare(chooser.unfold, 1)
            request.requestId = ""
            wait(10)
            compare(chooser.phase, "hidden")
            compare(chooser.unfold, 0)
        }

        function test_explicitPhotosSourceOpensPhotosPicker() {
            chooser.reduceMotion = true
            request.imageSources = true
            request.imageSource = "gallery"
            request.galleryItems = [
                {token: "photo-one", name: "one.png", album: "Selected photos", mime: "image/png"},
                {token: "photo-two", name: "two.jpg", album: "Selected photos", mime: "image/jpeg"}
            ]
            request.requestId = "/request/image-sources"
            wait(10)
            compare(chooser.sourcePage, "gallery")
            verify(findChild(chooser, "portalSourceButton") === null)
            verify(findChild(chooser, "portalSourceDropdown") === null)
            verify(findChild(chooser, "portalGalleryPicker").visible)
            compare(findChild(chooser, "portalPhotoChooserTitle").text, "Choose a photo")
            verify(findChild(chooser, "portalPhotoChooserDivider").visible)
            verify(findChild(chooser, "portalPhotoChooserSearchButton").visible)
            verify(findChild(chooser, "portalCloseButton").visible === false)
            verify(findChild(chooser, "portalPhotoChooserTabs").visible)
            compare(chooser.galleryAlbums.length, 1)
            const albumTab = findChild(chooser, "portalPhotoChooserAlbumsTab")
            albumTab.clicked()
            compare(chooser.galleryTab, "albums")
            verify(findChild(chooser, "portalGalleryAlbumGrid").visible)
            const albumGrid = findChild(chooser, "portalGalleryAlbumGrid")
            compare(albumGrid.count, 1)
            albumGrid.itemAtIndex(0).clicked()
            compare(chooser.galleryTab, "photos")
            compare(chooser.galleryAlbumFilter, "Selected photos")
            chooser.selectGalleryItem("photo-one")
            compare(request.selections, 1)
            compare(request.lastGalleryTokens, ["photo-one"])

            request.requestId = ""
            wait(10)
            request.imageSources = false
            request.imageSource = ""
            request.mode = "OpenFile"
            request.requestId = "/request/non-image"
            wait(10)
            compare(chooser.sourcePage, "files")
            verify(findChild(chooser, "portalSourceButton") === null)
            verify(!findChild(chooser, "portalGalleryPicker").visible)
            request.requestId = ""
            wait(10)
        }

        function test_photoChooserSlidesUpAndShowsEmptyState() {
            chooser.reduceMotion = false
            request.imageSources = true
            request.imageSource = "gallery"
            request.galleryItems = []
            request.galleryError = ""
            request.galleryLoading = false
            request.requestId = "/request/empty-gallery"

            const panel = findChild(chooser, "portalFileChooserPanel")
            const emptyState = findChild(chooser, "portalGalleryEmptyState")
            const backButton = findChild(chooser, "portalPhotoChooserBackButton")
            tryCompare(chooser, "sourcePage", "gallery")
            compare(chooser.sourcePage, "gallery")
            compare(panel.x, 0)
            compare(panel.width, screen.width)
            compare(panel.radius, 0)
            compare(panel.color.a, 0)
            compare(panel.transform[0].yScale, 1)
            verify(panel.y > panel.restingY)
            tryCompare(chooser, "phase", "open", 1000)
            compare(panel.y, panel.restingY)
            verify(emptyState.visible)
            compare(emptyState.text, "No photos available")
            verify(backButton.visible)

            backButton.clicked()
            compare(request.requestId, "")
            tryCompare(chooser, "phase", "hidden", 1000)
        }

        function test_imageMimeRequestDefaultsToFilesWithoutPortalDropdown() {
            chooser.reduceMotion = true
            request.imageSources = false
            request.imageSource = ""
            request.mimeTypes = ["image/*"]
            request.mode = "OpenFile"
            request.requestId = "/request/image-mime-fallback"
            wait(10)
            compare(chooser.imageSourceRequest, true)
            compare(chooser.sourcePage, "files")
            verify(findChild(chooser, "portalSourceButton") === null)
            verify(findChild(chooser, "portalSourceDropdown") === null)
            request.requestId = ""
            wait(10)
        }

        function test_replacementWaitsForCollapse() {
            request.requestId = "/request/first"
            wait(80)
            request.requestId = "/request/second"
            wait(10)
            compare(chooser.phase, "closing")
            compare(chooser.displayedRequestId, "/request/first")
            tryCompare(chooser, "displayedRequestId", "/request/second")
            tryCompare(chooser, "phase", "open")
            compare(chooser.unfold, 1)
            request.requestId = ""
            tryCompare(chooser, "phase", "hidden")
        }
    }
}
