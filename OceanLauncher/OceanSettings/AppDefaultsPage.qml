import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Flickable {
    id: page
    property var backend: null
    property var prompts: null
    property bool largeText: false
    property bool reducedMotion: false
    property string applicationId: ""
    property string applicationName: ""
    property string pendingExtension: ""
    signal backRequested()
    clip: true
    contentHeight: body.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    onVisibleChanged: {
        if (visible && backend) backend.refreshFileTypes()
        if (!visible) {
            if (pendingExtension.length && prompts && prompts.busy) prompts.cancel()
            pendingExtension = ""
        }
    }
    function choose(fileType) {
        if (!backend || !prompts || prompts.busy || !fileType.handlers.length) return
        const choices = {"none": qsTr("No default")}
        for (const handler of fileType.handlers) choices[handler.id] = handler.name
        pendingExtension = fileType.extension
        prompts.selection(qsTr("Open %1 files with").arg(fileType.label),
            qsTr("Files uses this app when you open a %1.").arg(fileType.label.toLowerCase()),
            {choices: choices, initialText: fileType.defaultId || "none", acceptLabel: qsTr("Set default"), cancelLabel: qsTr("Cancel")})
    }
    Connections {
        target: page.prompts
        function onFinished(requestId, result, text) {
            const extension = page.pendingExtension
            if (!extension.length) return
            page.pendingExtension = ""
            if (!page.visible || result !== "accepted" || !page.backend) return
            page.backend.setFileDefault(extension, text === "none" ? "" : text)
        }
    }
    Column {
        id: body
        width: parent.width
        SettingsRow {
            objectName: "settingsAppDefaultsBack"
            heading: "‹ " + page.applicationName
            onClicked: page.backRequested()
        }
        Text {
            text: qsTr("App defaults"); color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
            font.pixelSize: page.largeText ? 44 : 40
            width: parent.width; wrapMode: Text.Wrap; topPadding: 12; bottomPadding: 12
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"; font.family: Fonts.family
            font.pixelSize: page.largeText ? 30 : 25; bottomPadding: 12
            text: qsTr("Choose which app opens each type of file in Files.")
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#ffd1d1"; font.family: Fonts.family
            font.pixelSize: page.largeText ? 30 : 25
            visible: !!page.backend && page.backend.fileTypesError.length > 0
            text: page.backend ? page.backend.fileTypesError : ""
            textFormat: Text.PlainText
        }
        Repeater {
            model: page.backend ? page.backend.fileTypes.filter(t => !page.applicationId.length || t.handlers.some(h => h.id === page.applicationId)) : []
            SettingsRow {
                required property var modelData
                objectName: "settingsFileType_" + modelData.extension
                heading: modelData.label
                detail: modelData.protectedType ? qsTr("Installation always requires confirmation") : !modelData.handlers.length ? qsTr("No app can open this type")
                    : modelData.defaultName.length ? modelData.defaultName : qsTr("No default")
                enabled: modelData.handlers.length > 0 && !modelData.protectedType
                opacity: enabled ? 1 : 0.6
                largeText: page.largeText
                onClicked: page.choose(modelData)
            }
        }
    }
}
