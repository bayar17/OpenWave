pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import OpenWave.Controls 1.0

Rectangle {
    id: bookmarksPage
    required property string fontFamily
    required property bool reduceMotion
    property real bottomInset: 100
    property bool searchOpen: false
    property bool selecting: false
    property var selectedIds: []
    signal openPage(string url)
    color: "#202226"
    onVisibleChanged: {
        if (visible) Bookmarks.filter = ""
        else { selecting = false; selectedIds = []; searchOpen = false; searchInput.text = "" }
    }
    function toggleSelection(id) {
        const next = selectedIds.slice()
        const index = next.indexOf(id)
        if (index < 0) next.push(id)
        else next.splice(index, 1)
        selectedIds = next
    }
    function deleteSelected() {
        if (!selectedIds.length) return
        Bookmarks.removeAll(selectedIds)
        selectedIds = []; selecting = false
    }
    Text {
        x: 36; y: 30; text: qsTr("Bookmarks")
        color: "white"; font.family: bookmarksPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 60
    }
    Item {
        objectName: "browserBookmarksSearch"
        x: parent.width - 108; y: 34; width: 72; height: 72
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Search bookmarks")
        BrowserGlyph { anchors.centerIn: parent; width: 40; height: 40; kind: "search"; strokeColor: "white" }
        TapHandler { onTapped: {
            bookmarksPage.searchOpen = !bookmarksPage.searchOpen
            if (bookmarksPage.searchOpen) searchInput.forceActiveFocus()
            else { searchInput.text = ""; Bookmarks.filter = "" }
        } }
    }
    Item {
        x: 36; y: 118; width: parent.width - 72; height: 56
        visible: bookmarksPage.selecting
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 selected").arg(bookmarksPage.selectedIds.length)
            font.family: bookmarksPage.fontFamily; font.pixelSize: 30; color: "#c4c9d0"
        }
        Text {
            objectName: "browserBookmarksDelete"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Delete"); color: "#ff8a8a"
            font.family: bookmarksPage.fontFamily; font.pixelSize: 32
            opacity: bookmarksPage.selectedIds.length ? 1 : 0.4
            TapHandler { onTapped: bookmarksPage.deleteSelected() }
        }
    }
    WRTextField {
        id: searchInput
        x: 36; y: 122 + (bookmarksPage.selecting ? 62 : 0); width: parent.width - 72; height: 64
        visible: bookmarksPage.searchOpen
        placeholderText: qsTr("Search bookmarks")
        placeholderTextColor: "#9aa0a8"
        color: "white"; fontPixelSize: 32; fontFamily: bookmarksPage.fontFamily
        leftPadding: 22; rightPadding: 22
        background: Rectangle { color: "#33ffffff"; radius: 32 }
        onTextChanged: searchDelay.restart()
    }
    Timer { id: searchDelay; interval: 180; onTriggered: Bookmarks.filter = searchInput.text }
    ListView {
        id: entries
        objectName: "browserBookmarksList"
        x: 36; y: 128 + (bookmarksPage.searchOpen ? 84 : 0) + (bookmarksPage.selecting ? 62 : 0)
        width: parent.width - 72; height: parent.height - y - bookmarksPage.bottomInset
        model: Bookmarks; clip: true
        ScrollBar.vertical: ScrollBar { }
        delegate: Item {
            id: entry
            required property string bookmarkId
            required property string storedUrl
            required property string pageTitle
            required property string host
            width: entries.width; height: 116
            Rectangle {
                anchors.fill: parent; color: "#2a3b4a"
                visible: bookmarksPage.selecting && bookmarksPage.selectedIds.indexOf(entry.bookmarkId) >= 0
            }
            Rectangle {
                x: 1; width: 34; height: 34; radius: 17
                anchors.verticalCenter: parent.verticalCenter
                visible: bookmarksPage.selecting
                border.width: 2; border.color: "#9aa0a8"
                color: bookmarksPage.selectedIds.indexOf(entry.bookmarkId) >= 0 ? "#0aa7f0" : "transparent"
            }
            BrowserGlyph {
                id: mark
                x: bookmarksPage.selecting ? 52 : 0; width: 44; height: 44
                anchors.verticalCenter: parent.verticalCenter
                kind: "starFilled"; strokeColor: "#f5c542"
            }
            Column {
                anchors.left: mark.right; anchors.leftMargin: 20
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    width: parent.width
                    text: entry.pageTitle; elide: Text.ElideRight
                    color: "#eceef1"; font.family: bookmarksPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 38
                }
                Text {
                    width: parent.width
                    text: entry.host; elide: Text.ElideRight
                    color: "#a8adb4"; font.family: bookmarksPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 30
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#44ffffff" }
            TapHandler {
                onTapped: {
                    if (bookmarksPage.selecting) bookmarksPage.toggleSelection(entry.bookmarkId)
                    else bookmarksPage.openPage(entry.storedUrl)
                }
                onLongPressed: { bookmarksPage.selecting = true; bookmarksPage.selectedIds = [entry.bookmarkId] }
            }
        }
    }
    Text {
        objectName: "browserBookmarksEmpty"
        x: 36; y: entries.y + 90; width: parent.width - 72
        visible: Bookmarks.count === 0 || (Bookmarks.filter.length > 0 && entries.count === 0)
        text: Bookmarks.error.length ? Bookmarks.error : Bookmarks.filter.length ? qsTr("No matching bookmarks") : qsTr("No bookmarks yet")
        wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
        color: "#aab0b8"; font.family: bookmarksPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 34
    }
}
