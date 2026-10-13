pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Rectangle {
    id: page
    required property string fontFamily
    onVisibleChanged: if (visible) Downloads.refresh()
    color: "white"
    function bytes(value) {
        const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB")]
        let amount = Number(value), unit = 0
        while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; ++unit }
        return amount.toLocaleString(Qt.locale(), 'f', unit ? 1 : 0) + " " + units[unit]
    }
    Text { x: 40; y: 42; text: qsTr("Downloads"); color: "#111"; font.family: page.fontFamily; font.pixelSize: 48 }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 40; y: 62
        text: qsTr("Clear completed"); color: "#555"; font.family: page.fontFamily; font.pixelSize: 24
        TapHandler { onTapped: Downloads.clearCompleted() }
    }
    Text {
        x: 40; y: 115; width: parent.width - 80; text: Downloads.error
        visible: text.length > 0; color: "#a33"; wrapMode: Text.Wrap
        font.family: page.fontFamily; font.pixelSize: 24
    }
    ListView {
        id: list
        x: 40; y: Downloads.error.length ? 175 : 120; width: parent.width - 80
        height: parent.height - y - 130; model: Downloads; clip: true
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
            width: list.width; height: content.implicitHeight + 35
            Rectangle {
                x: 0; y: 14; width: 48; height: 54; radius: 7; color: "#eef0f1"
                Text { anchors.centerIn: parent; text: transfer.fileName.split('.').pop().slice(0,4).toUpperCase() || qsTr("FILE"); color: "#667"; font.family: page.fontFamily; font.pixelSize: 17 }
                Image {
                    anchors.fill: parent; visible: transfer.fileExists && transfer.mimeType.startsWith("image/")
                    source: visible ? transfer.thumbnailSource : ""; sourceSize.width: 96; sourceSize.height: 96
                    fillMode: Image.PreserveAspectFit; asynchronous: true
                }
            }
            Column {
                id: content; x: 64; y: 8; width: parent.width - 64; spacing: 7
                Text { width: parent.width; text: transfer.fileName; color: "#111"; font.family: page.fontFamily; font.pixelSize: 30; elide: Text.ElideMiddle }
                Text {
                    width: parent.width; text: page.bytes(transfer.receivedBytes) + (transfer.totalBytes >= 0 ? " / " + page.bytes(transfer.totalBytes) : "")
                    color: "#555"; font.family: page.fontFamily; font.pixelSize: 24; wrapMode: Text.Wrap
                }
                ProgressBar {
                    width: parent.width; visible: transfer.status === "Downloading" || transfer.status === "Paused"
                    from: 0; to: 1; value: transfer.progress; indeterminate: transfer.totalBytes < 0
                }
                Text {
                    width: parent.width
                    text: qsTr(transfer.status) + (transfer.speed > 0 ? " · " + page.bytes(transfer.speed) + qsTr("/s") : "")
                          + (transfer.privateDownload ? " · " + qsTr("Private") : "")
                    color: transfer.status === "Failed" ? "#a33" : "#555"; font.family: page.fontFamily; font.pixelSize: 24
                    wrapMode: Text.Wrap
                }
                Text { text: transfer.dateText; color: "#777"; font.family: page.fontFamily; font.pixelSize: 22 }
                Text {
                    width: parent.width; visible: text.length > 0; wrapMode: Text.Wrap
                    text: transfer.errorDetail || (transfer.status === "Completed" && !transfer.fileExists ? qsTr("File is missing") : "")
                    color: "#a33"; font.family: page.fontFamily; font.pixelSize: 22
                }
                Flow {
                    width: parent.width; spacing: 18
                    Repeater {
                        model: (transfer.canCancel ? [qsTr("Cancel")] : [])
                            .concat(transfer.canRetry ? [qsTr("Retry")] : [])
                            .concat(transfer.status === "Completed" ? (transfer.fileExists ? [qsTr("Open File"), qsTr("Show in Files"), qsTr("Remove from List")] : [qsTr("Remove from List")]) : [])
                            .concat(transfer.status === "Failed" || transfer.status === "Cancelled" ? [qsTr("Remove from List")] : [])
                        delegate: Text {
                            required property string modelData
                            text: modelData; color: "#008bbb"; font.family: page.fontFamily; font.pixelSize: 24
                            padding: 6
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
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#999" }
        }
        Text { anchors.centerIn: parent; visible: list.count === 0; text: Downloads.ready ? qsTr("No downloads yet") : qsTr("Loading downloads…"); color: "#777"; font.family: page.fontFamily; font.pixelSize: 28 }
    }
}
