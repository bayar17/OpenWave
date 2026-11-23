import QtQuick
import QtQuick.Controls
import OpenWave.Fonts 1.0
import OpenWave.QuickMenu as Applets

Item {
    id: page
    objectName: "settingsAppearancePage"

    property var manager: typeof Appearances !== "undefined" ? Appearances : null
    property var prompts: null
    property bool largeText: false
    property bool reduceMotion: false
    property bool editing: false
    property bool quickMenuOpen: false
    property string editId: ""
    property string editName: ""
    property real editHue: 0.58
    property url editImage
    property var editCrop: null
    property url editWallpaper
    property string menuId: ""
    property Item menuCard: null
    property string pendingDeleteId: ""
    readonly property var presets: manager ? manager.presets : []
    readonly property string current: manager ? manager.current : ""
    readonly property bool busy: manager ? manager.busy : false
    readonly property var menuPreset: presets.find(item => item.id === menuId) || null
    readonly property bool canSave: editName.trim().length > 0
        && (editId.length > 0 || editCrop !== null) && !busy
    property real slideProgress: editing ? 1 : 0
    property real quickMenuProgress: quickMenuOpen ? 1 : 0
    readonly property real coverProgress: Math.max(slideProgress, quickMenuProgress)
    Behavior on slideProgress {
        enabled: !page.reduceMotion
        NumberAnimation { duration: 300; easing.type: Easing.InOutCubic }
    }
    Behavior on quickMenuProgress {
        enabled: !page.reduceMotion
        NumberAnimation { duration: 300; easing.type: Easing.InOutCubic }
    }
    readonly property color editAccent: manager ? manager.accentForHue(editHue) : "#3a6ea5"
    readonly property url previewSource: editCrop ? editImage : editWallpaper

    signal changeImageRequested(Item source)
    signal message(string text)

    implicitHeight: Math.max(listPane.implicitHeight, editorPane.implicitHeight, quickMenuPane.implicitHeight)
    height: editing ? editorPane.implicitHeight : quickMenuOpen ? quickMenuPane.implicitHeight : listPane.implicitHeight
    clip: false

    function beginCreate() {
        menuId = ""
        editId = ""
        editName = ""
        editHue = 0.58
        editImage = ""
        editCrop = null
        editWallpaper = ""
        editing = true
    }

    function beginEdit(preset) {
        menuId = ""
        editId = preset.id
        editName = preset.name
        editHue = preset.hue !== undefined ? preset.hue : 0.58
        editImage = ""
        editCrop = null
        editWallpaper = preset.thumbnail
        editing = true
    }

    function cropChosen(url, x, y, width, height) {
        editImage = url
        editCrop = {x: x, y: y, width: width, height: height}
    }

    function cancelEdit() {
        editing = false
    }

    function openMenu(id, card) {
        menuCard = card
        menuId = id
    }

    function closeMenu() {
        menuId = ""
    }

    function menuAction(key) {
        const preset = menuPreset
        if (!preset)
            return
        if (key === "set") {
            const manager = page.manager
            menuId = ""
            if (manager)
                manager.set(preset.id)
        } else if (key === "edit") {
            if (preset.user)
                beginEdit(preset)
        } else if (key === "favorite") {
            const manager = page.manager
            menuId = ""
            if (manager)
                manager.setFavorite(preset.id, !preset.favorite)
        } else if (key === "delete") {
            if (preset.user)
                requestDelete(preset)
        }
    }

    function openQuickMenu() {
        menuId = ""
        quickMenuPage.reset()
        quickMenuOpen = true
    }

    function back() {
        if (menuId.length > 0) {
            menuId = ""
            return true
        }
        if (quickMenuOpen) {
            if (!quickMenuPage.back())
                quickMenuOpen = false
            return true
        }
        if (editing) {
            editing = false
            return true
        }
        return false
    }

    function save() {
        if (!canSave || !manager)
            return
        const crop = editCrop
        manager.save(editId, editName.trim(), editHue, crop ? editImage : "",
                     crop ? crop.x : 0, crop ? crop.y : 0, crop ? crop.width : 0, crop ? crop.height : 0)
    }

    function requestDelete(preset) {
        menuId = ""
        if (!prompts) {
            message(qsTr("The prompt service is unavailable."))
            return
        }
        pendingDeleteId = preset.id
        prompts.confirmation(qsTr("Delete appearance?"),
            qsTr("Delete “%1”? This cannot be undone.").arg(preset.name),
            {acceptLabel: qsTr("Delete"), cancelLabel: qsTr("Cancel")})
    }

    Behavior on x {
        NumberAnimation {
            duration: page.reduceMotion ? 0 : 300
            easing.type: Easing.InOutCubic
        }
    }

    Connections {
        target: page.manager
        ignoreUnknownSignals: true
        function onFinished(operation, id) {
            if (operation === "save")
                page.editing = false
        }
        function onFailed(operation, text) { page.message(text) }
    }

    Connections {
        target: page.prompts
        ignoreUnknownSignals: true
        function onFinished(requestId, result, text) {
            const id = page.pendingDeleteId
            page.pendingDeleteId = ""
            if (result === "accepted" && id.length > 0 && page.manager)
                page.manager.remove(id)
        }
    }

    Item {
        id: listPane
        objectName: "settingsAppearanceList"
        width: parent.width
        implicitHeight: cards.implicitHeight + 40
        x: -page.width * page.coverProgress
        visible: x > -page.width + 1
        opacity: 1 - page.coverProgress

        Column {
            id: cards
            width: parent.width
            spacing: 14

            Text {
                objectName: "settingsAppearanceEmpty"
                visible: page.presets.length === 0
                width: parent.width
                height: visible ? 120 : 0
                text: qsTr("No appearances yet. Add one to get started.")
                color: "#d4dadd"
                font.family: Fonts.family
                font.weight: Font.ExtraLight
                font.pixelSize: 32
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }

            Repeater {
                model: page.presets
                delegate: AppearanceCard {
                    id: card
                    required property var modelData
                    required property int index
                    objectName: "settingsAppearanceCard_" + modelData.id
                    width: cards.width
                    preset: modelData
                    selected: modelData.id === page.current
                    largeText: page.largeText
                    reduceMotion: page.reduceMotion
                    onTapped: if (page.manager && !page.busy) page.manager.set(modelData.id)
                    onHeld: page.openMenu(modelData.id, card)
                    onMenuRequested: page.openMenu(modelData.id, card)
                }
            }

            Item { width: 1; height: 6 }

            AbstractButton {
                id: addRow
                objectName: "settingsAppearanceAdd"
                width: parent.width
                height: 96
                Accessible.name: qsTr("Add new Appearance")
                onClicked: page.beginCreate()
                background: Item {}
                contentItem: Item {
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add new Appearance")
                        color: "white"
                        font.family: Fonts.family
                        font.weight: Font.ExtraLight
                        font.pixelSize: page.largeText ? 42 : 36
                    }
                    SettingsSymbol {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        kind: "chevron"
                        color: "white"
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: "#66ffffff"
                    }
                }
            }

            Item { width: 1; height: 30 }

            AbstractButton {
                objectName: "settingsAppearanceQuickMenu"
                width: parent.width
                height: 96
                Accessible.name: qsTr("Quick Menu")
                onClicked: page.openQuickMenu()
                background: Item {}
                contentItem: Item {
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Quick Menu")
                        color: "white"
                        font.family: Fonts.family
                        font.weight: Font.ExtraLight
                        font.pixelSize: page.largeText ? 42 : 36
                    }
                    SettingsSymbol {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        kind: "chevron"
                        color: "white"
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: "#66ffffff"
                    }
                }
            }
        }

    }

    Item {
        id: quickMenuPane
        objectName: "settingsQuickMenuPane"
        width: parent.width
        implicitHeight: quickMenuHeader.height + quickMenuPage.implicitHeight + 60
        x: page.width * (1 - page.quickMenuProgress)
        visible: x < page.width - 1
        opacity: page.quickMenuProgress

        SubpageHeader {
            id: quickMenuHeader
            backObjectName: "settingsQuickMenuBack"
            titleObjectName: "settingsQuickMenuTitle"
            largeText: page.largeText
            title: quickMenuPage.title
            onBackClicked: page.back()
        }

        QuickMenuPage {
            id: quickMenuPage
            y: quickMenuHeader.height
            width: parent.width
            largeText: page.largeText
            onMessage: text => page.message(text)
        }
    }

    Item {
        id: editorPane
        objectName: "settingsAppearanceEditor"
        width: parent.width
        implicitHeight: 1180
        x: page.width * (1 - (page.slideProgress))
        visible: x < page.width - 1
        opacity: page.slideProgress

        SubpageHeader {
            id: editorHeader
            backObjectName: "settingsAppearanceBack"
            titleObjectName: "settingsAppearanceEditorTitle"
            actionObjectName: "settingsAppearanceSave"
            largeText: page.largeText
            title: page.editId.length > 0 ? qsTr("Edit Appearance") : qsTr("New Appearance")
            actionText: page.busy ? qsTr("saving…") : qsTr("save")
            actionEnabled: page.canSave
            onBackClicked: page.cancelEdit()
            onActionClicked: page.save()
        }

        Item {
            id: previewCard
            objectName: "settingsAppearancePreview"
            y: editorHeader.height + 10
            width: parent.width
            height: 560
            clip: true
            Rectangle { anchors.fill: parent; color: "#33000000" }
            Image {
                id: previewImage
                anchors.fill: parent
                source: page.previewSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                sourceSize: Qt.size(width * 2, height * 2)
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity {
                    enabled: !page.reduceMotion
                    NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: previewImage.status !== Image.Ready
                text: qsTr("No image selected")
                color: "#d4dadd"
                font.family: Fonts.family
                font.weight: Font.ExtraLight
                font.pixelSize: 32
            }
        }

        Column {
            y: previewCard.y + previewCard.height + 20
            width: parent.width
            spacing: 0

            Item {
                width: parent.width
                height: 100
                TextField {
                    id: nameField
                    objectName: "settingsAppearanceNameField"
                    anchors.fill: parent
                    text: page.editName
                    placeholderText: qsTr("Appearance Name")
                    placeholderTextColor: "#99ffffff"
                    maximumLength: 32
                    color: "white"
                    font.family: Fonts.family
                    font.weight: Font.ExtraLight
                    font.pixelSize: page.largeText ? 42 : 36
                    leftPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextEdited: page.editName = text
                    background: Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: nameField.activeFocus ? "white" : "#66ffffff"
                    }
                }
            }

            AbstractButton {
                id: changeImage
                objectName: "settingsAppearanceChangeImage"
                width: parent.width
                height: 100
                Accessible.name: qsTr("Change image")
                onClicked: page.changeImageRequested(changeImage)
                background: Item {}
                contentItem: Item {
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Change image")
                        color: "white"
                        font.family: Fonts.family
                        font.weight: Font.ExtraLight
                        font.pixelSize: page.largeText ? 42 : 36
                    }
                    SettingsSymbol {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        kind: "chevron"
                        color: "white"
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: "#66ffffff"
                    }
                }
            }

            Item {
                width: parent.width
                height: 150

                Item {
                    id: hueTrack
                    objectName: "settingsAppearanceHue"
                    x: 0
                    y: 36
                    width: parent.width
                    height: 40
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Color hue")
                    Accessible.description: qsTr("%1 percent").arg(Math.round(page.editHue * 100))
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 8
                        radius: 4
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.00; color: "#ff0000" }
                            GradientStop { position: 0.17; color: "#ffff00" }
                            GradientStop { position: 0.33; color: "#00ff00" }
                            GradientStop { position: 0.50; color: "#00ffff" }
                            GradientStop { position: 0.67; color: "#0000ff" }
                            GradientStop { position: 0.83; color: "#ff00ff" }
                            GradientStop { position: 1.00; color: "#ff0000" }
                        }
                    }
                    Rectangle {
                        objectName: "settingsAppearanceHueHandle"
                        x: Math.max(0, Math.min(hueTrack.width, hueTrack.width * page.editHue)) - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        height: 36
                        radius: 18
                        color: "white"
                        border.color: page.editAccent
                        border.width: 6
                        Behavior on border.color {
                            enabled: !page.reduceMotion
                            ColorAnimation { duration: 150 }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        function update(mouseX) {
                            page.editHue = Math.max(0, Math.min(1, mouseX / hueTrack.width))
                        }
                        onPressed: mouse => update(mouse.x)
                        onPositionChanged: mouse => { if (pressed) update(mouse.x) }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 90
                    text: qsTr("Color hue")
                    color: "white"
                    font.family: Fonts.family
                    font.weight: Font.ExtraLight
                    font.pixelSize: 34
                }
            }
        }
    }
}
