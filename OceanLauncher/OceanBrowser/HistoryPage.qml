pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import OpenWave.Controls 1.0

Rectangle {
    id: historyPage
    required property string fontFamily
    required property bool reduceMotion
    property bool searchOpen: false
    property bool selecting: false
    property bool compact: false
    property var selectedIds: []
    property string menuKind: "main"
    property string pendingBrowserAction: ""
    property real bottomInset: 100
    readonly property var menuEntries: menuKind === "sort"
        ? [qsTr("Newest first"), qsTr("Oldest first"), qsTr("Page title")]
        : menuKind === "view" ? [qsTr("List"), qsTr("Compact")]
        : [qsTr("Delete History"), qsTr("Select"), qsTr("Sort"), qsTr("View mode"),
           qsTr("Show all tabs"), qsTr("New Tab"), qsTr("New Private Tab")]
    signal openPage(string url)
    signal browserAction(string action)
    color: "#202226"
    onVisibleChanged: {
        if (visible) History.reload()
        else { options.close(); deletion.close(); selecting = false; selectedIds = [] }
    }
    function toggleSelection(id) {
        const next = selectedIds.slice()
        const index = next.indexOf(id)
        if (index < 0) next.push(id)
        else next.splice(index, 1)
        selectedIds = next
    }
    function openMenu() { menuKind = "main"; options.open() }
    function deleteSelected() {
        if (!selectedIds.length) return
        History.removeEntries(selectedIds)
        selectedIds = []; selecting = false
    }
    Text {
        x: 36; y: 30; text: qsTr("History")
        color: "white"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 60
    }
    Item {
        x: parent.width - 108; y: 34; width: 72; height: 72
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Search history")
        BrowserGlyph { anchors.centerIn: parent; width: 40; height: 40; kind: "search"; strokeColor: "white" }
        TapHandler { onTapped: {
            historyPage.searchOpen = !historyPage.searchOpen
            if (historyPage.searchOpen) searchInput.forceActiveFocus()
            else { searchInput.text = ""; History.filter = "" }
        } }
    }
    Item {
        x: 36; y: 118; width: parent.width - 72; height: 56
        visible: historyPage.selecting
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 selected").arg(historyPage.selectedIds.length)
            font.family: historyPage.fontFamily; font.pixelSize: 30; color: "#c4c9d0"
        }
        Text {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Delete"); color: "#ff8a8a"
            font.family: historyPage.fontFamily; font.pixelSize: 32
            opacity: historyPage.selectedIds.length ? 1 : 0.4
            TapHandler { onTapped: historyPage.deleteSelected() }
        }
    }
    WRTextField {
        id: searchInput
        x: 36; y: 122 + (historyPage.selecting ? 62 : 0); width: parent.width - 72; height: 64
        visible: historyPage.searchOpen
        placeholderText: qsTr("Search history")
        placeholderTextColor: "#9aa0a8"
        color: "white"; fontPixelSize: 32; fontFamily: historyPage.fontFamily
        leftPadding: 22; rightPadding: 22
        background: Rectangle { color: "#33ffffff"; radius: 32 }
        onTextChanged: searchDelay.restart()
    }
    Timer { id: searchDelay; interval: 180; onTriggered: History.filter = searchInput.text }
    ListView {
        id: visits
        x: 36; y: 128 + (historyPage.searchOpen ? 84 : 0) + (historyPage.selecting ? 62 : 0)
        width: parent.width - 72; height: parent.height - y - historyPage.bottomInset
        model: History; clip: true
        ScrollBar.vertical: ScrollBar { }
        section.property: History.sortOrder === "title" ? "" : "dateGroup"
        section.criteria: ViewSection.FullString
        section.delegate: Text {
            required property string section
            width: visits.width; height: 56; text: section
            color: "#9aa0a8"; font.family: historyPage.fontFamily; font.pixelSize: 32
            verticalAlignment: Text.AlignVCenter
        }
        delegate: Item {
            id: visit
            required property var historyId
            required property string storedUrl
            required property string pageTitle
            required property string faviconSource
            required property string visitTime
            width: visits.width; height: historyPage.compact ? 80 : 106
            Rectangle {
                anchors.fill: parent
                color: "#2a3b4a"
                visible: historyPage.selecting && historyPage.selectedIds.indexOf(visit.historyId) >= 0
            }
            Rectangle {
                x: 1; width: 34; height: 34; radius: 17
                anchors.verticalCenter: parent.verticalCenter
                visible: historyPage.selecting
                border.width: 2; border.color: "#9aa0a8"
                color: historyPage.selectedIds.indexOf(visit.historyId) >= 0 ? "#0aa7f0" : "transparent"
            }
            Image {
                id: icon
                x: historyPage.selecting ? 52 : 0; width: 48; height: 48
                anchors.verticalCenter: parent.verticalCenter
                source: visit.faviconSource
                sourceSize.width: 64; sourceSize.height: 64
                fillMode: Image.PreserveAspectFit; smooth: true
            }
            Image {
                x: icon.x; y: icon.y; width: icon.width; height: icon.height
                source: "assets/globe.svg"
                visible: !visit.faviconSource.length || icon.status === Image.Error
            }
            Text {
                anchors.left: icon.right; anchors.leftMargin: 18
                anchors.right: timestamp.left; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: visit.pageTitle; elide: Text.ElideRight
                color: "#eceef1"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 38
            }
            Text {
                id: timestamp
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                text: visit.visitTime
                color: "#a8adb4"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 30
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#44ffffff" }
            TapHandler {
                onTapped: {
                    if (historyPage.selecting) historyPage.toggleSelection(visit.historyId)
                    else historyPage.openPage(visit.storedUrl)
                }
                onLongPressed: { historyPage.selecting = true; historyPage.selectedIds = [visit.historyId] }
            }
        }
    }
    Text {
        x: 36; y: visits.y + 90; width: parent.width - 72
        visible: History.error.length > 0 || History.count === 0
        text: History.error.length ? History.error : !History.ready ? qsTr("Loading history…")
            : History.filter.length ? qsTr("No matching visits") : qsTr("No browsing history yet")
        wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
        color: "#aab0b8"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 34
    }
    Popup {
        id: options
        parent: historyPage
        x: parent.width - width - 18; y: parent.height - height - 14; width: 460
        height: optionItems.implicitHeight + 44
        padding: 22; modal: true; dim: false
        transformOrigin: Item.BottomRight
        background: Rectangle { radius: 46; color: "#d6686a6e"; border.width: 1; border.color: "#33ffffff" }
        onClosed: {
            if (!historyPage.pendingBrowserAction.length) return
            const action = historyPage.pendingBrowserAction
            historyPage.pendingBrowserAction = ""
            historyPage.browserAction(action)
        }
        enter: Transition { NumberAnimation { property: "scale"; from: 0; to: 1; duration: historyPage.reduceMotion ? 0 : 200; easing.type: Easing.OutCubic } }
        exit: Transition { NumberAnimation { property: "scale"; to: 0; duration: historyPage.reduceMotion ? 0 : 160; easing.type: Easing.InCubic } }
        contentItem: Column {
            id: optionItems
            Repeater {
                model: historyPage.menuEntries
                delegate: Item {
                    id: option
                    required property string modelData
                    required property int index
                    width: options.availableWidth; height: 78
                    Text {
                        anchors.fill: parent; text: option.modelData
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 44; color: "white"
                    }
                    TapHandler { onTapped: {
                        if (historyPage.menuKind === "sort") {
                            History.sortOrder = ["newest", "oldest", "title"][option.index]; options.close()
                        } else if (historyPage.menuKind === "view") {
                            historyPage.compact = option.index === 1; options.close()
                        } else if (option.index === 0) {
                            options.close(); deletion.open()
                        } else if (option.index === 1) {
                            historyPage.selecting = !historyPage.selecting; historyPage.selectedIds = []; options.close()
                        } else if (option.index < 4) historyPage.menuKind = option.index === 2 ? "sort" : "view"
                        else {
                            historyPage.pendingBrowserAction = ["Show all tabs", "New Tab", "New Private Tab"][option.index - 4]
                            options.close()
                        }
                    } }
                }
            }
        }
    }
    Popup {
        id: deletion
        parent: historyPage
        anchors.centerIn: parent
        width: Math.min(600, parent.width - 72); height: deleteItems.implicitHeight + 56
        padding: 28; modal: true; dim: true
        background: Rectangle { color: "#e6585a5e"; radius: 40; border.width: 1; border.color: "#33ffffff" }
        contentItem: Column {
            id: deleteItems
            spacing: 12
            Text {
                width: deletion.availableWidth; text: qsTr("Delete History")
                horizontalAlignment: Text.AlignHCenter
                font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 44; color: "white"
            }
            WRComboBox {
                id: period
                width: deletion.availableWidth
                model: [qsTr("All history"), qsTr("Today"), qsTr("Yesterday"), qsTr("Last 7 days"), qsTr("Date range")]
                fontPixelSize: 32
            }
            WRTextField {
                id: firstDate; width: deletion.availableWidth
                visible: period.currentIndex === 4; placeholderText: qsTr("From: YYYY-MM-DD")
                fontPixelSize: 32; fontFamily: historyPage.fontFamily
            }
            WRTextField {
                id: lastDate; width: deletion.availableWidth
                visible: period.currentIndex === 4; placeholderText: qsTr("Through: YYYY-MM-DD")
                fontPixelSize: 32; fontFamily: historyPage.fontFamily
            }
            Item {
                width: deletion.availableWidth; height: 72
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cancel"); color: "white"; font.family: historyPage.fontFamily; font.pixelSize: 36
                    TapHandler { onTapped: deletion.close() }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Delete"); color: "#ffb0b0"; font.family: historyPage.fontFamily; font.pixelSize: 36
                    TapHandler { onTapped: {
                        if (period.currentIndex === 4) History.clearDateRange(firstDate.text, lastDate.text)
                        else History.clearPeriod(["all", "today", "yesterday", "week"][period.currentIndex])
                        historyPage.selectedIds = []; historyPage.selecting = false; deletion.close()
                    } }
                }
            }
        }
    }
}
