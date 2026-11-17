import QtQuick

Window {
    visible: true
    color: "#1f2a30"
    Column {
        anchors.centerIn: parent
        spacing: 24
        Image { source: "file:///Applications/org.example.hello/share/icons/icon.svg"; width: 160; height: 160; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: "Hello from OpenWave"; color: "white"; font.pixelSize: 44; anchors.horizontalCenter: parent.horizontalCenter }
        Text { text: "org.example.hello"; color: "#a8ffffff"; font.pixelSize: 28; anchors.horizontalCenter: parent.horizontalCenter }
    }
}
