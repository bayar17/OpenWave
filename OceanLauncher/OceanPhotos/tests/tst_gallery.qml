import QtQuick
import QtTest
import ".." as Photos

Item {
    QtObject {
        id: library
        property var photos: [
            {url: "", title: "Ocean.png", album: "Landscapes", dateLabel: "Today", format: "PNG", dimensions: "100 × 100", size: "1 KiB", timestamp: "Today", animated: false},
            {url: "", title: "Mountain.jpg", album: "Landscapes", dateLabel: "Yesterday", format: "JPEG", dimensions: "100 × 100", size: "2 KiB", timestamp: "Yesterday", animated: false}]
        property var albums: [{name: "Landscapes", cover: "", count: 2}]
        property var supportedFormats: ["PNG", "JPEG"]
        property var supportedMimeTypes: ["image/png", "image/jpeg"]
        property string error: ""
        signal changed()
        function openImage(url) { return 0 }
        function importImage(url, name) { return 0 }
        function loadFolder(url) { return true }
    }
    Photos.Main { id: gallery; library: library; captureMode: true }
    QtObject {
        id: portal
        property bool busy: false
        property string kind: ""
        property var options: ({})
        signal completed(var grants)
        signal cancelled()
        signal errorOccurred(string message)
        function openFile(value) { kind = "single"; options = value }
        function openFiles(value) { kind = "multiple"; options = value }
    }
    TestCase {
        name: "GalleryConcept"
        when: windowShown
        function init() {
            gallery.width = 768; gallery.height = 1366
            gallery.captureMode = true; gallery.reducedMotion = false
            gallery.zoom = 1; gallery.selectedIndex = 0
            gallery.viewerOpen = false; gallery.currentPage = "photos"
            gallery.searching = false; gallery.selecting = false; gallery.selected = ({})
            gallery.albumFilter = ""; gallery.slideshow = false; gallery.infoOpen = false
            gallery.portalClient = null; portal.kind = ""; portal.busy = false
            wait(20)
        }
        function swipePhoto(dx, dy, touchInput) {
            const pager = findChild(gallery, "galleryPhotoPager")
            const x = dx < 0 ? pager.width * 0.75 : pager.width * 0.25
            const y = pager.height / 2
            if (!touchInput) { mouseDrag(pager, x, y, dx, dy, Qt.LeftButton); return }
            const touch = touchEvent(pager)
            touch.press(0, pager, x, y).commit()
            for (let i = 1; i <= 10; ++i) {
                touch.move(0, pager, x + dx * i / 10, y + dy * i / 10).commit()
                wait(16)
            }
            touch.release(0, pager, x + dx, y + dy).commit()
        }
        function test_touchSwipesBothWaysAndResetsBetweenGestures() {
            gallery.openPhoto(0)
            const pager = findChild(gallery, "galleryPhotoPager")
            swipePhoto(-300, 0, true)
            tryCompare(gallery, "selectedIndex", 1)
            compare(pager.offset, 0)
            swipePhoto(300, 0, true)
            tryCompare(gallery, "selectedIndex", 0)
            swipePhoto(-300, 0, true)
            tryCompare(gallery, "selectedIndex", 1)
        }
        function test_mouseDragNavigatesWithoutModifiers() {
            gallery.openPhoto(0)
            swipePhoto(-300, 0, false)
            tryCompare(gallery, "selectedIndex", 1)
        }
        function test_photoFollowsFingerThenSettlesBeforeTitleChanges() {
            gallery.openPhoto(0); gallery.captureMode = false
            const pager = findChild(gallery, "galleryPhotoPager")
            const title = findChild(gallery, "galleryPhotoTitle")
            const touch = touchEvent(pager)
            touch.press(0, pager, 560, 400).commit()
            touch.move(0, pager, 510, 400).commit(); wait(16)
            touch.move(0, pager, 260, 400).commit(); wait(16)
            verify(pager.offset < -100); compare(gallery.selectedIndex, 0)
            touch.release(0, pager, 260, 400).commit()
            wait(50)
            verify(pager.settling); compare(gallery.selectedIndex, 0)
            compare(title.displayedText, "Ocean.png")
            tryCompare(gallery, "selectedIndex", 1)
            tryCompare(title, "transitioning", false)
            compare(title.displayedText, "Mountain.jpg"); compare(pager.offset, 0)
        }
        function test_pinchCancelsPhotoSwipe() {
            gallery.openPhoto(0)
            const pager = findChild(gallery, "galleryPhotoPager")
            const touch = touchEvent(pager)
            touch.press(0, pager, 400, 400).commit()
            touch.move(0, pager, 350, 400).commit(); wait(16)
            touch.move(0, pager, 300, 400).commit(); wait(16)
            touch.press(1, pager, 500, 400).commit(); wait(16)
            touch.move(0, pager, 250, 400).move(1, pager, 550, 400).commit(); wait(16)
            touch.move(0, pager, 200, 400).move(1, pager, 600, 400).commit(); wait(16)
            touch.release(0, pager, 200, 400).release(1, pager, 600, 400).commit()
            wait(30)
            compare(gallery.selectedIndex, 0); compare(pager.offset, 0)
            verify(gallery.zoom > 1)
        }
        function test_shortAndVerticalSwipesKeepCurrentPhoto() {
            gallery.openPhoto(0)
            const pager = findChild(gallery, "galleryPhotoPager")
            swipePhoto(-35, 0, true)
            wait(30); compare(gallery.selectedIndex, 0); compare(pager.offset, 0)
            swipePhoto(30, 300, true)
            wait(30); compare(gallery.selectedIndex, 0); compare(pager.offset, 0)
        }
        function test_zoomedDragPansWithoutChangingPhoto() {
            gallery.openPhoto(0); gallery.zoom = 2
            swipePhoto(-300, 0, true)
            wait(30); compare(gallery.selectedIndex, 0)
            verify(findChild(gallery, "galleryPhotoCanvas").contentX > 0)
            gallery.step(1)
            compare(gallery.zoom, 1)
            compare(findChild(gallery, "galleryPhotoPager").zoom, 1)
        }
        function test_titleCrossfadesAndFinishesAfterRapidNavigation() {
            gallery.openPhoto(0); gallery.captureMode = false
            const title = findChild(gallery, "galleryPhotoTitle")
            gallery.step(1)
            compare(title.outgoingText, "Ocean.png")
            compare(title.displayedText, "Mountain.jpg")
            wait(80)
            verify(title.progress > 0 && title.progress < 1)
            verify(findChild(title, "outgoingTitle").opacity > 0)
            verify(findChild(title, "currentTitle").opacity > 0)
            gallery.step(1)
            tryCompare(title, "transitioning", false)
            compare(title.displayedText, gallery.photo.title)
            compare(title.progress, 1)
        }
        function test_reducedMotionStillAllowsSwiping() {
            gallery.openPhoto(0); gallery.captureMode = false; gallery.reducedMotion = true
            swipePhoto(-300, 0, true)
            tryCompare(gallery, "selectedIndex", 1)
            const title = findChild(gallery, "galleryPhotoTitle")
            compare(title.transitioning, false); compare(title.progress, 1)
        }
        function test_photoTapOpensViewerAndBackReturns() {
            const timeline = findChild(gallery, "galleryTimeline")
            mouseClick(timeline, 100, 150)
            compare(gallery.viewerOpen, true)
            compare(gallery.photo.title, "Ocean.png")
            gallery.zoom = 3
            gallery.step(1)
            compare(gallery.photo.title, "Mountain.jpg"); compare(gallery.zoom, 1)
            gallery.back(); compare(gallery.viewerOpen, false)
        }
        function test_searchFiltersPhotosAndDates() {
            gallery.searching = true
            findChild(gallery, "gallerySearch").text = "mountain"
            compare(gallery.visiblePhotos.length, 1)
            compare(gallery.sections.length, 1)
            compare(gallery.sections[0].label, "Yesterday")
            gallery.back(); compare(gallery.searching, false)
        }
        function test_selectionDoesNotOpenViewer() {
            gallery.selecting = true
            const timeline = findChild(gallery, "galleryTimeline")
            mouseClick(timeline, 100, 150)
            compare(gallery.viewerOpen, false)
            compare(Object.keys(gallery.selected).length, 1)
            gallery.back(); compare(gallery.selecting, false)
            compare(Object.keys(gallery.selected).length, 0)
        }
        function test_viewerWrapsAndInfoBackKeepsPhotoOpen() {
            gallery.openPhoto(1); gallery.step(1)
            compare(gallery.selectedIndex, 0)
            gallery.step(-1); compare(gallery.selectedIndex, 1)
            gallery.infoOpen = true; gallery.back()
            compare(gallery.infoOpen, false); compare(gallery.viewerOpen, true)
        }
        function test_filteredAlbumBackReturnsToAlbums() {
            gallery.albumFilter = "Landscapes"; gallery.currentPage = "photos"
            gallery.back()
            compare(gallery.albumFilter, ""); compare(gallery.currentPage, "albums")
        }
        function test_viewerBackgroundCoversStatusAreaAndWindowEdges_data() {
            return [{tag: "qemu", width: 544, height: 960},
                    {tag: "wide", width: 900, height: 1000},
                    {tag: "tall", width: 500, height: 1100}]
        }
        function test_viewerBackgroundCoversStatusAreaAndWindowEdges(data) {
            gallery.width = data.width; gallery.height = data.height
            gallery.portalClient = portal
            gallery.openPhoto(0)
            waitForRendering(gallery.contentItem)
            const image = grabImage(gallery.contentItem)
            const points = [[0, 0], [data.width - 1, 0], [data.width / 2, 20],
                            [0, data.height - 1], [data.width - 1, data.height - 1]]
            for (const point of points) {
                compare(image.red(point[0], point[1]), 22)
                compare(image.green(point[0], point[1]), 29)
                compare(image.blue(point[0], point[1]), 33)
            }
            gallery.back()
            compare(findChild(gallery, "galleryViewerBackground").visible, false)
        }
        function test_nativePortalUsesImageFilterAndOpensImportedPhoto() {
            gallery.portalClient = portal
            gallery.choosePhotos(true)
            compare(portal.kind, "single"); compare(portal.options.mimeTypes, library.supportedMimeTypes)
            portal.completed([{name: "photo.png", temporaryCopy: () => "file:///tmp/test.png"}])
            compare(gallery.viewerOpen, true)
            gallery.back()
            gallery.choosePhotos(false)
            compare(portal.kind, "multiple")
            portal.completed([{name: "photo.png", temporaryCopy: () => "file:///tmp/test.png"}])
            compare(gallery.viewerOpen, false); compare(gallery.currentPage, "photos")
        }
    }
}
