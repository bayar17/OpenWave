import QtQuick
import OpenWave.Fonts 1.0

Item {
    id: root
    objectName: "quickMenuPage"
    readonly property var manager: typeof QuickMenuApplets !== "undefined" ? QuickMenuApplets : null
    property bool largeText: false
    property string configureId: ""
    property string configureName: ""
    readonly property int appletCount: manager ? manager.count : 0
    readonly property var rejections: manager ? manager.rejections : []
    readonly property string title: configureId.length > 0 ? configureName : qsTr("Quick Menu")

    signal message(string text)

    implicitHeight: configureId.length > 0 ? configurePane.implicitHeight : mainPane.implicitHeight

    function back() {
        if (configureId.length === 0)
            return false
        configureId = ""
        configureName = ""
        return true
    }

    function reset() {
        configureId = ""
        configureName = ""
    }

    function configure(appletId, appletName) {
        if (!manager || manager.settingsUrl(appletId).toString().length === 0)
            return
        configureName = appletName
        configureId = appletId
    }

    Connections {
        target: root.manager
        function onFailed(text) { root.message(text) }
    }

    Column {
        id: mainPane
        objectName: "quickMenuMainPane"
        width: parent.width
        visible: root.configureId.length === 0

        Text {
            objectName: "quickMenuIntro"
            width: parent.width
            topPadding: 6
            bottomPadding: 18
            text: qsTr("Choose which applets appear in the Quick Menu and in what order.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.family: Fonts.family
            font.pixelSize: root.largeText ? 32 : 28
            font.weight: Font.Light
            color: "#d4dadd"
        }

        Repeater {
            model: root.manager ? root.manager.model : null
            delegate: QuickMenuAppletRow {
                width: mainPane.width
                objectName: "quickMenuApplet_" + model.id
                largeText: root.largeText
                canMoveUp: index > 0
                canMoveDown: index < root.appletCount - 1
                onToggled: value => root.manager.setEnabled(appletId, value)
                onMoveUpRequested: root.manager.moveUp(appletId)
                onMoveDownRequested: root.manager.moveDown(appletId)
                onConfigureRequested: root.configure(appletId, model.name)
            }
        }

        Text {
            objectName: "quickMenuEmpty"
            visible: root.appletCount === 0
            width: parent.width
            height: visible ? 110 : 0
            text: qsTr("No Quick Menu applets were found.")
            wrapMode: Text.Wrap
            font.family: Fonts.family
            font.pixelSize: 30
            font.weight: Font.Light
            color: "#d4dadd"
            verticalAlignment: Text.AlignVCenter
        }

        Item {
            width: parent.width
            height: 100
            Text {
                objectName: "quickMenuReset"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Reset to defaults")
                font.family: Fonts.family
                font.pixelSize: root.largeText ? 38 : 32
                font.weight: Font.Light
                color: "#7fb0ff"
                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.onPressAction: if (root.manager) root.manager.reset()
                TapHandler {
                    onTapped: if (root.manager) root.manager.reset()
                }
            }
        }

        Column {
            objectName: "quickMenuSkipped"
            visible: root.rejections.length > 0
            width: parent.width
            Text {
                width: parent.width
                height: 70
                text: qsTr("Skipped applets")
                font.family: Fonts.family
                font.pixelSize: root.largeText ? 38 : 32
                font.weight: Font.ExtraLight
                color: "white"
                verticalAlignment: Text.AlignVCenter
            }
            Repeater {
                model: root.rejections
                delegate: Text {
                    required property var modelData
                    width: parent.width
                    text: modelData.path.split("/").pop() + ": " + modelData.reason
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.family: Fonts.family
                    font.pixelSize: 24
                    font.weight: Font.Light
                    color: "#ff9e9e"
                    bottomPadding: 10
                }
            }
        }
    }

    Item {
        id: configurePane
        objectName: "quickMenuConfigurePane"
        width: parent.width
        visible: root.configureId.length > 0
        implicitHeight: Math.max(300, configureLoader.item ? configureLoader.item.implicitHeight : 0)

        Loader {
            id: configureLoader
            objectName: "quickMenuConfigureLoader"
            anchors.fill: parent
            active: root.configureId.length > 0
            source: root.configureId.length > 0 && root.manager ? root.manager.settingsUrl(root.configureId) : ""
            onStatusChanged: {
                if (status === Loader.Error) {
                    root.message(qsTr("This applet's settings could not be loaded."))
                    root.back()
                }
            }
        }
    }
}
