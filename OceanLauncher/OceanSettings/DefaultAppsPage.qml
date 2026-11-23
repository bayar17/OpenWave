import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0

Flickable {
    id: page
    property var backend: null
    property var prompts: null
    property bool largeText: false
    property bool reducedMotion: false
    property string pendingCategory: ""
    signal backRequested()
    signal fileTypesRequested()
    clip: true
    contentHeight: body.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    onVisibleChanged: {
        if (visible && backend) backend.refreshCategories()
        if (!visible) {
            if (pendingCategory.length && prompts && prompts.busy) prompts.cancel()
            pendingCategory = ""
        }
    }
    function choose(category) {
        if (!backend || !prompts || prompts.busy || !category.handlers.length) return
        const choices = {"none": qsTr("No default")}
        for (const handler of category.handlers) choices[handler.id] = handler.name
        pendingCategory = category.id
        prompts.selection(qsTr("Default for %1").arg(category.name.toLowerCase()),
            qsTr("Apps and Files use this app when you open this kind of content."),
            {choices: choices, initialText: category.defaultId || "none", acceptLabel: qsTr("Set default"), cancelLabel: qsTr("Cancel")})
    }
    function stateText(category) {
        if (category.state === "unavailable") return qsTr("Chosen app is not available right now")
        return category.defaultName.length ? category.defaultName : qsTr("Not set")
    }
    Connections {
        target: page.prompts
        function onFinished(requestId, result, text) {
            const category = page.pendingCategory
            if (!category.length) return
            page.pendingCategory = ""
            if (!page.visible || result !== "accepted" || !page.backend) return
            page.backend.setCategoryDefault(category, text === "none" ? "" : text)
        }
    }
    Column {
        id: body
        width: parent.width
        SettingsRow {
            objectName: "settingsDefaultAppsBack"
            heading: "‹ " + qsTr("Apps")
            onClicked: page.backRequested()
        }
        Text {
            text: qsTr("Default apps"); color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
            font.pixelSize: page.largeText ? 44 : 40
            width: parent.width; wrapMode: Text.Wrap; topPadding: 12; bottomPadding: 12
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#d4dadd"; font.family: Fonts.family
            font.pixelSize: page.largeText ? 30 : 25; bottomPadding: 12
            visible: !!page.backend && !page.backend.defaultCategories.length && !page.backend.categoriesError.length
            text: qsTr("No installed app offers a default for these categories yet.")
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; color: "#ffd1d1"; font.family: Fonts.family
            font.pixelSize: page.largeText ? 30 : 25
            visible: !!page.backend && page.backend.categoriesError.length > 0
            text: page.backend ? page.backend.categoriesError : ""
            textFormat: Text.PlainText
        }
        Repeater {
            model: page.backend ? page.backend.defaultCategories : []
            SettingsRow {
                required property var modelData
                objectName: "settingsDefaultCategory_" + modelData.id
                heading: modelData.name
                detail: page.stateText(modelData)
                largeText: page.largeText
                onClicked: page.choose(modelData)
            }
        }
        SettingsRow {
            objectName: "settingsDefaultFileTypes"
            heading: qsTr("File types")
            detail: qsTr("Choose an app for each type of file")
            largeText: page.largeText
            onClicked: page.fileTypesRequested()
        }
    }
}
