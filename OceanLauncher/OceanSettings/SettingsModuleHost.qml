import QtQuick
import OpenWave.Settings 1.0

Item {
    id: root
    objectName: "settingsModuleHost"
    property string moduleId: ""
    property color errorColor: "#ff9e9e"
    property int errorPixelSize: 30
    readonly property SettingsModuleLoader loader: moduleLoader
    readonly property bool loaded: moduleLoader.loaded
    readonly property string errorString: moduleLoader.errorString
    clip: true

    function synchronize() {
        if (moduleId.length > 0)
            moduleLoader.load(moduleId)
        else
            moduleLoader.unload()
    }

    function back() {
        const page = moduleLoader.item
        return !!page && typeof page.back === "function" && page.back() === true
    }

    function openEntry(key) {
        const page = moduleLoader.item
        if (page && typeof page.openEntry === "function")
            page.openEntry(key)
    }

    onModuleIdChanged: synchronize()
    Component.onCompleted: synchronize()

    SettingsModuleLoader {
        id: moduleLoader
        host: root
    }

    Text {
        objectName: "settingsModuleError"
        anchors.fill: parent
        anchors.margins: 24
        visible: moduleLoader.status === SettingsModuleLoader.Error
        text: moduleLoader.errorString
        textFormat: Text.PlainText
        color: root.errorColor
        font.pixelSize: root.errorPixelSize
        wrapMode: Text.Wrap
        elide: Text.ElideRight
    }
}
