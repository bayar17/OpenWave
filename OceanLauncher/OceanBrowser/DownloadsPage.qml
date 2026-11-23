pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import OpenWave.Controls 1.0

Rectangle {
    id: page
    required property string fontFamily
    property real bottomInset: 100
    onVisibleChanged: if (visible) Downloads.refresh()
    color: "#202226"
    function bytes(value) {
        const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB")]
        let amount = Number(value), unit = 0
        while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; ++unit }
        return amount.toLocaleString(Qt.locale(), 'f', unit ? 1 : 0) + " " + units[unit]
    }
    Text { x: 36; y: 30; text: qsTr("Downloads"); color: "white"; font.family: page.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 60 }
    Text {
        objectName: "browserClearCompleted"
        anchors.right: parent.right; anchors.rightMargin: 36; y: 54
        text: qsTr("Clear completed"); color: "#c4c9d0"; font.family: page.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 30
        padding: 8
        Accessible.role: Accessible.Button
        Accessible.name: text
        TapHandler { onTapped: Downloads.clearCompleted() }
    }
    Text {
        x: 36; y: 122; width: parent.width - 72; text: Downloads.error
        visible: text.length > 0; color: "#ff9e9e"; wrapMode: Text.Wrap
        font.family: page.fontFamily; font.pixelSize: 30
    }
    ListView {
        id: list
        x: 36; y: Downloads.error.length ? 210 : 128; width: parent.width - 72
        height: parent.height - y - page.bottomInset; model: Downloads; clip: true
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: transfer
            required property string downloadId
            required property string fileName
            required property string destination
            required property string mimeType
            required property var totalBytes
            required property var receivedBytes
            required property real progress
            required property string status
            required property real speed
            required property string dateText
            required property string errorDetail
            required property bool canCancel
            required property bool canPause
            required property bool canResume
            required property bool canRetry
            required property bool fileExists
            required property bool privateDownload
            required property url thumbnailSource
            width: list.width; height: content.implicitHeight + 44
            Rectangle {
                x: 0; y: 18; width: 66; height: 76; radius: 10; color: "#3a3d44"
                Text { anchors.centerIn: parent; text: transfer.fileName.split('.').pop().slice(0,4).toUpperCase() || qsTr("FILE"); color: "#c4c9d0"; font.family: page.fontFamily; font.pixelSize: 22 }
                Image {
                    anchors.fill: parent; visible: transfer.fileExists && transfer.mimeType.startsWith("image/")
                    source: visible ? transfer.thumbnailSource : ""; sourceSize.width: 96; sourceSize.height: 96
                    fillMode: Image.PreserveAspectFit; asynchronous: true
                }
            }
            Column {
                id: content; x: 88; y: 10; width: parent.width - 88; spacing: 8
                Text { width: parent.width; text: transfer.fileName; color: "#eceef1"; font.family: page.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 38; elide: Text.ElideMiddle }
                Text {
                    width: parent.width; text: page.bytes(transfer.receivedBytes) + (transfer.totalBytes >= 0 ? " / " + page.bytes(transfer.totalBytes) : "")
                    color: "#a8adb4"; font.family: page.fontFamily; font.pixelSize: 30; wrapMode: Text.Wrap
                }
                WRProgressBar {
                    width: parent.width; visible: transfer.status === "Downloading" || transfer.status === "Paused"
                    from: 0; to: 1; value: transfer.progress; indeterminate: transfer.totalBytes < 0
                }
                Text {
                    width: parent.width
                    text: qsTr(transfer.status) + (transfer.speed > 0 ? " · " + page.bytes(transfer.speed) + qsTr("/s") : "")
                          + (transfer.privateDownload ? " · " + qsTr("Private") : "")
                    color: transfer.status === "Failed" ? "#ff9e9e" : "#a8adb4"; font.family: page.fontFamily; font.pixelSize: 30
                    wrapMode: Text.Wrap
                }
                Text { text: transfer.dateText; color: "#8a9099"; font.family: page.fontFamily; font.pixelSize: 28 }
                Text {
                    width: parent.width; visible: text.length > 0; wrapMode: Text.Wrap
                    text: transfer.errorDetail || (transfer.status === "Completed" && !transfer.fileExists ? qsTr("File is missing") : "")
                    color: "#ff9e9e"; font.family: page.fontFamily; font.pixelSize: 28
                }
                Flow {
                    width: parent.width; spacing: 22
                    Repeater {
                        model: (transfer.canCancel ? [qsTr("Cancel")] : [])
                            .concat(transfer.canRetry ? [qsTr("Retry")] : [])
                            .concat(transfer.status === "Completed" ? (transfer.fileExists ? [qsTr("Open File"), qsTr("Show in Files"), qsTr("Remove from List")] : [qsTr("Remove from List")]) : [])
                            .concat(transfer.status === "Failed" || transfer.status === "Cancelled" ? [qsTr("Remove from List")] : [])
                        delegate: Text {
                            required property string modelData
                            text: modelData; color: "#4fc3f7"; font.family: page.fontFamily; font.pixelSize: 32
                            padding: 10
                            TapHandler { onTapped: {
                                if (modelData === qsTr("Cancel")) Downloads.cancel(transfer.downloadId)
                                else if (modelData === qsTr("Retry")) Downloads.retry(transfer.downloadId)
                                else if (modelData === qsTr("Open File")) Downloads.openFile(transfer.downloadId)
                                else if (modelData === qsTr("Show in Files")) Downloads.showInFiles(transfer.downloadId)
                                else Downloads.removeRecord(transfer.downloadId)
                            } }
                        }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#44ffffff" }
        }
        Text { anchors.centerIn: parent; visible: list.count === 0; text: Downloads.ready ? qsTr("No downloads yet") : qsTr("Loading downloads…"); color: "#aab0b8"; font.family: page.fontFamily; font.weight: Font.ExtraLight; font.pixelSize: 36 }
    }
}
