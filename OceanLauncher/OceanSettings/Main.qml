import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 768
    height: 1366
    flags: Qt.Window | Qt.FramelessWindowHint
    visible: true
    title: "Ocean Settings"
    color: "#404b52"
    property var settingsBackend: null
    property var connectivityBackend: null
    property var timeBackend: null
    property var promptClient: null
    property var permissionClient: null
    property var applicationsBackend: null
    readonly property real contentScale: Math.min(width / 768, height / 1366)
    signal exitRequested(bool home)
    onExitRequested: home => {
        if (settingsBackend) settingsBackend.closeApp(home)
        else root.showMinimized()
    }
    Connections {
        target: root.settingsBackend
        function onHomeRequested() { root.showMinimized() }
    }

    FontLoader { id: regularFont; source: "assets/fonts/Inter-Regular.otf" }
    FontLoader { source: "assets/fonts/Inter-Light.otf" }
    font.family: regularFont.name

    function goBack() {
        if (!page.back()) exitRequested(false)
    }
    Shortcut { sequences: ["Escape", "Back"]; onActivated: root.goBack() }

    Item {
        id: screen
        objectName: "settingsScreen"
        width: 768
        height: 1366
        x: Math.round((root.width - width * scale) / 2)
        y: Math.round((root.height - height * scale) / 2)
        scale: root.contentScale
        transformOrigin: Item.TopLeft
        clip: true

        Rectangle { anchors.fill: parent; color: "black" }
        Image {
            id: wallpaperImage
            anchors.fill: parent
            source: Wallpaper.source
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
        }

        SettingsPage {
            id: page
            anchors.fill: parent
            backend: root.settingsBackend
            connectivityBackend: root.connectivityBackend
            timeBackend: root.timeBackend
            promptClient: root.promptClient
            permissionClient: root.permissionClient
            applicationsBackend: root.applicationsBackend
            wallpaper: wallpaperImage
            renderScale: root.contentScale
            topInset: 70
            onClosed: root.exitRequested(false)
        }
    }
}
