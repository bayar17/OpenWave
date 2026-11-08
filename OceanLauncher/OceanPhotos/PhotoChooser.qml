import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Item {
    id: picker
    objectName: "portalGalleryPicker"
    required property var backend
    required property string displayedMode
    required property string albumFilter
    required property var selectedTokens
    required property color ink
    required property color mutedInk
    required property bool lightTheme
    required property var albums
    required property var items
    required property string galleryTab
    required property bool searchOpen
    signal backRequested()
    signal albumSelected(string album)
    signal itemSelected(string token)
    signal tabSelected(string tab)

    function focusGallery() {
        if (galleryTab === "albums") albumGrid.forceActiveFocus()
        else photoGrid.forceActiveFocus()
    }

    AbstractButton {
        objectName: "portalPhotoChooserBackButton"
        x: 22; y: 47; width: 48; height: 48
        z: 2
        onClicked: picker.backRequested()
        background: Item {}
        contentItem: Item {
            Rectangle {
                x: 31; y: 12; width: 2; height: 27
                color: picker.ink; rotation: 45
            }
            Rectangle {
                x: 31; y: 31; width: 2; height: 27
                color: picker.ink; rotation: -45
            }
        }
    }

    function albumCoverToken(album) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].album === album) return items[i].token
        return ""
    }

    function albumPhotoCount(album) {
        let count = 0
        for (let i = 0; i < items.length; ++i)
            if (items[i].album === album) ++count
        return count
    }

    Text {
        objectName: "portalPhotoChooserTitle"
        x: 78; y: 9; width: Math.max(0, parent.width * 0.735 - x - 15); height: 72
        visible: !picker.searchOpen
        text: picker.displayedMode === "OpenFiles" ? qsTr("Choose photos") : qsTr("Choose a photo")
        color: picker.ink
        font.family: Fonts.family; font.weight: Font.ExtraLight
        font.pixelSize: Math.min(58, Math.max(34, (parent.width - 250) / 8.4))
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }

    Text {
        x: 78; y: 77; width: parent.width - 112; height: 46
        visible: !picker.searchOpen
        text: picker.galleryTab === "albums" ? qsTr("Select an album")
                                               : picker.displayedMode === "OpenFiles"
                                                 ? qsTr("Select photos") : qsTr("Select a photo")
        color: picker.mutedInk
        font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 32
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }

    Rectangle {
        objectName: "portalPhotoChooserDivider"
        x: 34; y: 138; width: parent.width - 68; height: 2
        color: picker.ink; opacity: 0.86
    }

    GridView {
        id: photoGrid
        objectName: "portalGalleryGrid"
        x: 20; y: 151; width: parent.width - 40
        height: Math.max(0, parent.height - y - 108)
        visible: picker.galleryTab === "photos"
        cellWidth: width / 3; cellHeight: Math.min(220, cellWidth + 30)
        model: picker.items
        clip: true; focus: visible; boundsBehavior: Flickable.StopAtBounds
        delegate: AbstractButton {
            id: photoTile
            required property var modelData
            width: photoGrid.cellWidth - 8; height: photoGrid.cellHeight - 8
            onClicked: picker.itemSelected(modelData.token)
            Accessible.name: modelData.name
            background: Rectangle {
                radius: 10
                color: picker.selectedTokens.indexOf(modelData.token) >= 0 ? "#547ed0dc" : "transparent"
                border.color: picker.selectedTokens.indexOf(modelData.token) >= 0 ? "#dfffffff" : "transparent"
                border.width: 2
            }
            contentItem: Item {
                Image {
                    anchors.fill: parent
                    source: picker.backend ? picker.backend.galleryImageUrl(photoTile.modelData.token) : ""
                    sourceSize: Qt.size(width, height)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: false; clip: true
                }
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: picker.selectedTokens.indexOf(photoTile.modelData.token) >= 0 ? 4 : 0
                    border.color: "white"
                    radius: 10
                }
            }
        }
    }

    GridView {
        id: albumGrid
        objectName: "portalGalleryAlbumGrid"
        x: 20; y: 151; width: parent.width - 40
        height: Math.max(0, parent.height - y - 108)
        visible: picker.galleryTab === "albums"
        cellWidth: width / 2; cellHeight: Math.min(300, cellWidth * 0.9)
        model: picker.albums
        clip: true; focus: visible; boundsBehavior: Flickable.StopAtBounds
        delegate: AbstractButton {
            id: albumTile
            required property string modelData
            width: albumGrid.cellWidth - 12; height: albumGrid.cellHeight - 12
            onClicked: picker.albumSelected(modelData)
            Accessible.name: modelData
            background: Rectangle { color: "#18ffffff"; radius: 12 }
            contentItem: Item {
                Image {
                    x: 0; y: 0; width: parent.width; height: parent.height - 58
                    source: {
                        const token = picker.albumCoverToken(albumTile.modelData)
                        return token && picker.backend ? picker.backend.galleryImageUrl(token) : ""
                    }
                    sourceSize: Qt.size(width, height)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true; cache: false; clip: true
                }
                Text {
                    x: 12; y: parent.height - 56; width: parent.width - 24; height: 31
                    text: albumTile.modelData
                    color: picker.ink; font.family: Fonts.family; font.pixelSize: 22
                    verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                }
                Text {
                    x: 12; y: parent.height - 28; width: parent.width - 24; height: 24
                    text: qsTr("%1 photos").arg(picker.albumPhotoCount(albumTile.modelData))
                    color: picker.mutedInk; font.family: Fonts.family; font.pixelSize: 17
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    Text {
        objectName: "portalGalleryEmptyState"
        anchors.centerIn: picker.galleryTab === "albums" ? albumGrid : photoGrid
        width: parent.width - 72
        visible: !!picker.backend && picker.backend.galleryLoading
                 || !!picker.backend && picker.backend.galleryError.length > 0
                 || picker.galleryTab === "albums" && albumGrid.count === 0
                 || picker.galleryTab === "photos" && photoGrid.count === 0
        text: !picker.backend ? qsTr("Photos are unavailable")
            : picker.backend.galleryLoading ? qsTr("Loading photos…")
            : picker.backend.galleryError.length ? picker.backend.galleryError
            : qsTr("No photos available")
        color: picker.mutedInk; font.family: Fonts.family; font.pixelSize: 26
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
    }

    Rectangle {
        x: tabBar.x + 5; y: tabBar.y + 7; width: tabBar.width; height: tabBar.height
        radius: height / 2; color: "#25000000"
    }

    Rectangle {
        id: tabBar
        objectName: "portalPhotoChooserTabs"
        width: Math.min(574, parent.width - 64); height: 87
        x: (parent.width - width) / 2; y: parent.height - height
        radius: height / 2
        color: picker.lightTheme ? "#a9aeb0" : "#858789"

        Row {
            anchors.fill: parent
            AbstractButton {
                objectName: "portalPhotoChooserPhotosTab"
                width: tabBar.width / 2; height: tabBar.height
                onClicked: picker.tabSelected("photos")
                background: Item {}
                contentItem: Text {
                    text: qsTr("Photos")
                    color: picker.ink; opacity: picker.galleryTab === "photos" ? 1 : 0.82
                    font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 30
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
            AbstractButton {
                objectName: "portalPhotoChooserAlbumsTab"
                width: tabBar.width / 2; height: tabBar.height
                onClicked: picker.tabSelected("albums")
                background: Item {}
                contentItem: Text {
                    text: qsTr("Albums")
                    color: picker.ink; opacity: picker.galleryTab === "albums" ? 1 : 0.82
                    font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: 30
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
