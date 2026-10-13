pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root
    property string fontFamily
    property bool reduceMotion: false
    property Item backdrop: null
    property int currentTab: 0
    signal closed()

    component SectionHeading: Item {
        property string title
        width: parent.width; height: 68
        Text {
            text: parent.title; textFormat: Text.PlainText
            font.family: root.fontFamily; font.pixelSize: 36; font.weight: Font.DemiBold
            color: "white"
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: "white" }
    }
    component ToggleRow: Item {
        id: row
        property string title
        property string controlName
        property bool value
        signal toggled(bool checked)
        width: parent.width; height: Math.max(72, label.implicitHeight + 12)
        Text {
            id: label
            anchors.left: parent.left; anchors.right: toggle.left; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: row.title; textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.family: root.fontFamily; font.pixelSize: 36; color: "white"
        }
        BrowserSwitch {
            id: toggle
            objectName: row.controlName
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            checked: row.value; reduceMotion: root.reduceMotion
            Accessible.name: row.title
            onToggled: row.toggled(checked)
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent; radius: 32
        color: "transparent"
        layer.enabled: true
        layer.effect: MultiEffect { maskEnabled: true; maskSource: panelMask }
        MultiEffect {
            anchors.fill: parent; source: root.backdrop
            blurEnabled: true; blurMax: 32; blur: 1
            autoPaddingEnabled: false
        }
        Rectangle { anchors.fill: parent; color: "#cc89969b" }
        Button {
            x: 12; y: 60; width: 56; height: 58
            Accessible.name: qsTr("Close browser settings")
            contentItem: BrowserGlyph { kind: "back"; strokeColor: "white" }
            background: Item {}
            onClicked: root.closed()
        }
        Row {
            id: tabs
            x: 88; y: 40; width: parent.width - 140; height: 94
            Repeater {
                model: [qsTr("General"), qsTr("Search"), qsTr("Privacy")]
                delegate: Button {
                    id: tab
                    required property int index
                    required property string modelData
                    width: tabs.width / 3; height: tabs.height
                    checkable: true; checked: root.currentTab === index
                    Accessible.name: modelData
                    contentItem: Text {
                        text: tab.modelData; font.family: root.fontFamily; font.pixelSize: 40
                        color: tab.checked ? "#008dff" : "white"
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Item {
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 40; height: 3
                            color: "#008dff"; visible: tab.checked
                        }
                    }
                    onClicked: root.currentTab = index
                }
            }
        }
        Rectangle { x: 88; y: 130; width: parent.width - 164; height: 6; color: "white" }

        Flickable {
            id: content
            x: 76; y: 170; width: parent.width - 147; height: parent.height - y - 65
            contentHeight: sections.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Column {
                id: sections
                width: content.width
                Column {
                    visible: root.currentTab === 0
                    width: parent.width
                    SectionHeading { title: qsTr("Downloads") }
                    ToggleRow {
                        controlName: "askOnDownload"
                        title: qsTr("Ask on Download"); value: BrowserPreferences.askOnDownload
                        onToggled: function(checked) { BrowserPreferences.askOnDownload = checked }
                    }
                    Button {
                        id: folderButton
                        width: parent.width; height: 72
                        enabled: !BrowserPreferences.choosingFolder
                        Accessible.name: qsTr("Choose download folder")
                        contentItem: Item {
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Download folder"); color: "white"
                                font.family: root.fontFamily; font.pixelSize: 36
                                width: parent.width * 0.51; elide: Text.ElideRight
                            }
                            Text {
                                anchors.right: arrow.left; anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 0.45 - 30
                                text: qsTr("Choose folder"); color: "white"
                                font.family: root.fontFamily; font.pixelSize: 32
                                horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                            }
                            BrowserGlyph {
                                id: arrow
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                width: 28; height: 28; kind: "forward"
                            }
                        }
                        background: Rectangle { color: folderButton.down ? "#18ffffff" : "transparent" }
                        onClicked: BrowserPreferences.chooseDownloadFolder()
                    }
                    Text {
                        width: parent.width; text: BrowserPreferences.downloadFolder
                        textFormat: Text.PlainText; elide: Text.ElideMiddle
                        color: "#dfe5e8"; font.family: root.fontFamily; font.pixelSize: 23
                    }
                    Item { width: 1; height: 40 }
                    SectionHeading { title: qsTr("Session") }
                    ToggleRow {
                        controlName: "startPrivate"
                        title: qsTr("Start on Private Tab"); value: BrowserPreferences.startPrivate
                        onToggled: function(checked) { BrowserPreferences.startPrivate = checked }
                    }
                    ToggleRow {
                        controlName: "rememberTabs"
                        title: qsTr("Save tabs after close"); value: BrowserPreferences.rememberTabs
                        onToggled: function(checked) { BrowserPreferences.rememberTabs = checked }
                    }
                    ToggleRow {
                        controlName: "alwaysShowFullUrl"
                        title: qsTr("Always show full URL"); value: BrowserPreferences.alwaysShowFullUrl
                        onToggled: function(checked) { BrowserPreferences.alwaysShowFullUrl = checked }
                    }
                }
                Column {
                    visible: root.currentTab === 1
                    width: parent.width
                    SectionHeading { title: qsTr("Search engine") }
                    Repeater {
                        model: [{key: "google", label: "Google"}, {key: "duckduckgo", label: "DuckDuckGo"}, {key: "bing", label: "Bing"}]
                        delegate: RadioDelegate {
                            id: engine
                            required property var modelData
                            width: sections.width; height: 80
                            checked: BrowserPreferences.searchEngine === modelData.key
                            Accessible.name: modelData.label
                            contentItem: Text {
                                text: engine.modelData.label; color: "white"
                                font.family: root.fontFamily; font.pixelSize: 36
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: Rectangle {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                width: 34; height: 34; radius: 17; color: "transparent"
                                border.color: "white"; border.width: 2
                                Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "#008dff"; visible: engine.checked }
                            }
                            background: Rectangle { color: engine.down ? "#18ffffff" : "transparent" }
                            onClicked: BrowserPreferences.searchEngine = modelData.key
                        }
                    }
                }
                Column {
                    visible: root.currentTab === 2
                    width: parent.width
                    SectionHeading { title: qsTr("Browsing") }
                    ToggleRow {
                        controlName: "rememberHistory"
                        title: qsTr("Remember browsing history"); value: BrowserPreferences.rememberHistory
                        onToggled: function(checked) { BrowserPreferences.rememberHistory = checked }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: qsTr("Private tabs never save history or cookies.")
                        font.family: root.fontFamily; font.pixelSize: 26; color: "#e0e5e8"
                    }
                    Item { width: 1; height: 40 }
                    SectionHeading { title: qsTr("Cookies") }
                    ToggleRow {
                        controlName: "blockThirdPartyCookies"
                        title: qsTr("Block third-party cookies"); value: BrowserPreferences.blockThirdPartyCookies
                        onToggled: function(checked) { BrowserPreferences.blockThirdPartyCookies = checked }
                    }
                }
                Text {
                    width: parent.width; visible: BrowserPreferences.error.length > 0
                    text: BrowserPreferences.error; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                    font.family: root.fontFamily; font.pixelSize: 26; color: "white"
                }
            }
        }
    }
    Rectangle {
        id: panelMask
        width: panel.width; height: panel.height; radius: panel.radius
        visible: false; layer.enabled: true
    }
}
