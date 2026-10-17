import QtQuick
import QtQuick.Window
import OpenWave.Fonts 1.0
import OpenWave.Web 1.0

Window {
    id: root
    required property string appName
    required property url initialUrl
    width: 544; height: 960
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: appName
    color: "white"

    function goBack() {
        if (page.canGoBack) page.goBack()
        else root.showMinimized()
    }
    Shortcut { sequences: ["Back", "Escape"]; onActivated: root.goBack() }
    WebView {
        id: page
        anchors.fill: parent
        privateBrowsing: false
        url: root.initialUrl
        onLoadFinished: (success, address, error) => {
            if (success) page.runJavaScript(WebApp.iconScript, result => WebApp.refreshIcon(String(result)))
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: page.errorText.length > 0
        color: "white"
        Column {
            anchors.centerIn: parent
            width: parent.width - 64
            spacing: 24
            Text {
                width: parent.width
                text: qsTr("Unable to load %1").arg(root.appName)
                color: "#414d58"; font.family: Fonts.family; font.pixelSize: 28
                wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                text: page.errorText
                color: "#626b73"; font.family: Fonts.family; font.pixelSize: 18
                wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 140; height: 52; radius: 26; color: "#dce2e5"
                Text { anchors.centerIn: parent; text: qsTr("Retry"); font.family: Fonts.family; font.pixelSize: 23; color: "#414d58" }
                TapHandler { onTapped: page.reload() }
            }
        }
    }
}
