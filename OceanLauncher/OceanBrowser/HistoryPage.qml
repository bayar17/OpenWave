pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

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
    readonly property var menuEntries: menuKind === "sort"
        ? [qsTr("Newest first"), qsTr("Oldest first"), qsTr("Page title")]
        : menuKind === "view" ? [qsTr("List"), qsTr("Compact")]
        : [qsTr("Delete History"), qsTr("Select"), qsTr("Sort"), qsTr("View mode"),
           qsTr("Show all tabs"), qsTr("New Tab"), qsTr("New Private Tab")]
    signal openPage(string url)
    signal browserAction(string action)
    color: "white"
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
        x: 40; y: 42; text: qsTr("History")
        color: "#111"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 48
    }
    Item {
        x: parent.width - 88; y: 44; width: 48; height: 55
        BrowserGlyph { anchors.centerIn: parent; width: 31; height: 31; kind: "search"; strokeColor: "#333" }
        TapHandler { onTapped: {
            historyPage.searchOpen = !historyPage.searchOpen
            if (historyPage.searchOpen) searchInput.forceActiveFocus()
            else { searchInput.text = ""; History.filter = "" }
        } }
    }
    Item {
        x: 40; y: 110; width: parent.width - 80; height: 45
        visible: historyPage.selecting
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 selected").arg(historyPage.selectedIds.length)
            font.family: historyPage.fontFamily; font.pixelSize: 24; color: "#444"
        }
        Text {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Delete"); color: "#c13d3d"
            font.family: historyPage.fontFamily; font.pixelSize: 26
            opacity: historyPage.selectedIds.length ? 1 : 0.4
            TapHandler { onTapped: historyPage.deleteSelected() }
        }
    }
    TextField {
        id: searchInput
        x: 40; y: 115 + (historyPage.selecting ? 50 : 0); width: parent.width - 80; height: 48
        visible: historyPage.searchOpen
        placeholderText: qsTr("Search history")
        color: "#222"; font.family: historyPage.fontFamily; font.pixelSize: 26
        background: Rectangle { color: "#f4f4f4"; radius: 14 }
        onTextChanged: searchDelay.restart()
    }
    Timer { id: searchDelay; interval: 180; onTriggered: History.filter = searchInput.text }
    ListView {
        id: visits
        x: 40; y: 115 + (historyPage.searchOpen ? 60 : 0) + (historyPage.selecting ? 50 : 0)
        width: parent.width - 80; height: parent.height - y - 125
        model: History; clip: true
        ScrollBar.vertical: ScrollBar { }
        section.property: History.sortOrder === "title" ? "" : "dateGroup"
        section.criteria: ViewSection.FullString
        section.delegate: Text {
            required property string section
            width: visits.width; height: 42; text: section
            color: "#777"; font.family: historyPage.fontFamily; font.pixelSize: 26
            verticalAlignment: Text.AlignVCenter
        }
        delegate: Item {
            id: visit
            required property var historyId
            required property string storedUrl
            required property string pageTitle
            required property string faviconSource
            required property string visitTime
            width: visits.width; height: historyPage.compact ? 64 : 84
            Rectangle {
                anchors.fill: parent
                color: "#eaf5fa"
                visible: historyPage.selecting && historyPage.selectedIds.indexOf(visit.historyId) >= 0
            }
            Rectangle {
                x: 1; width: 28; height: 28; radius: 14
                anchors.verticalCenter: parent.verticalCenter
                visible: historyPage.selecting
                border.width: 1; border.color: "#999"
                color: historyPage.selectedIds.indexOf(visit.historyId) >= 0 ? "#00aeef" : "white"
            }
            Image {
                id: icon
                x: historyPage.selecting ? 41 : 0; width: 36; height: 36
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
                anchors.left: icon.right; anchors.leftMargin: 12
                anchors.right: timestamp.left; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: visit.pageTitle; elide: Text.ElideRight
                color: "#333"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 30
            }
            Text {
                id: timestamp
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                text: visit.visitTime
                color: "#444"; font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 24
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#888" }
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
        x: 40; y: visits.y + 75; width: parent.width - 80
        visible: History.error.length > 0 || History.count === 0
        text: History.error.length ? History.error : !History.ready ? qsTr("Loading history…")
            : History.filter.length ? qsTr("No matching visits") : qsTr("No browsing history yet")
        wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
        color: "#666"; font.family: historyPage.fontFamily; font.pixelSize: 26
    }
    Popup {
        id: options
        parent: historyPage
        x: parent.width - width - 25; y: parent.height - 114 - height; width: 335
        height: optionItems.implicitHeight + 38
        padding: 19; modal: true; dim: false
        transformOrigin: Item.BottomRight
        background: Rectangle { radius: 42; color: "#b7b7b7" }
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
                    width: options.availableWidth; height: 70
                    Text {
                        anchors.fill: parent; text: option.modelData
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 40; color: "white"
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
        width: Math.min(520, parent.width - 80); height: deleteItems.implicitHeight + 40
        padding: 20; modal: true; dim: true
        background: Rectangle { color: "#b7b7b7"; radius: 35 }
        contentItem: Column {
            id: deleteItems
            spacing: 12
            Text {
                width: deletion.availableWidth; text: qsTr("Delete History")
                horizontalAlignment: Text.AlignHCenter
                font.family: historyPage.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 36; color: "white"
            }
            ComboBox {
                id: period
                width: deletion.availableWidth
                model: [qsTr("All history"), qsTr("Today"), qsTr("Yesterday"), qsTr("Last 7 days"), qsTr("Date range")]
                font.family: historyPage.fontFamily; font.pixelSize: 25
            }
            TextField {
                id: firstDate; width: deletion.availableWidth
                visible: period.currentIndex === 4; placeholderText: qsTr("From: YYYY-MM-DD")
                font.family: historyPage.fontFamily; font.pixelSize: 25
            }
            TextField {
                id: lastDate; width: deletion.availableWidth
                visible: period.currentIndex === 4; placeholderText: qsTr("Through: YYYY-MM-DD")
                font.family: historyPage.fontFamily; font.pixelSize: 25
            }
            Item {
                width: deletion.availableWidth; height: 55
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cancel"); color: "white"; font.family: historyPage.fontFamily; font.pixelSize: 28
                    TapHandler { onTapped: deletion.close() }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Delete"); color: "#8c2020"; font.family: historyPage.fontFamily; font.pixelSize: 28
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
