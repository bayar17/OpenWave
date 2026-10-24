import QtQuick
import QtQuick.Controls

// WaterRouge presentation for Quick Menu editing.
// Deliberately does not own preferences, connectivity actions or lock authority.
// The compositor passes in the current layout and persists applyRequested().
Item {
    id: editor
    objectName: "waterRougeQuickMenuEditor"

    property bool editing: false
    property bool reduceMotion: false
    property string fontFamily: "Inter"
    property url assetBase: "qrc:/OceanTopbard/assets/QuickMenu/"
    property var defaultOrder: ["wifi", "bluetooth", "airplane", "volume", "brightness", "lock"]
    property var descriptions: ({
        "wifi": qsTr("Wi-Fi"),
        "bluetooth": qsTr("Bluetooth"),
        "airplane": qsTr("Airplane Mode"),
        "volume": qsTr("Volume"),
        "brightness": qsTr("Brightness"),
        "lock": qsTr("Lock Screen")
    })
    property real reveal: editing ? 1 : 0

    signal applyRequested(var orderedIds, var hiddenIds)
    signal cancelRequested()

    visible: editing || reveal > 0.001
    enabled: editing
    focus: editing
    z: 100

    Behavior on reveal {
        NumberAnimation {
            duration: editor.reduceMotion ? 0 : 210
            easing.type: Easing.OutCubic
        }
    }

    ListModel { id: controlsModel }

    function beginEdit(orderIds, hiddenIds) {
        const requested = Array.isArray(orderIds) ? orderIds : defaultOrder
        const hidden = Array.isArray(hiddenIds) ? hiddenIds : []
        const valid = defaultOrder
        const seen = ({})
        const ordered = []
        for (let i = 0; i < requested.length; ++i) {
            const id = requested[i]
            if (valid.indexOf(id) >= 0 && !seen[id]) {
                seen[id] = true
                ordered.push(id)
            }
        }
        for (let i = 0; i < valid.length; ++i)
            if (!seen[valid[i]]) ordered.push(valid[i])

        controlsModel.clear()
        for (let i = 0; i < ordered.length; ++i) {
            const id = ordered[i]
            controlsModel.append({
                controlId: id,
                controlLabel: descriptions[id],
                isShown: hidden.indexOf(id) < 0
            })
        }
        editing = true
        panel.forceActiveFocus()
    }

    function restoreDefaults() {
        beginEdit(defaultOrder, [])
    }

    function currentOrder() {
        const ordered = []
        for (let i = 0; i < controlsModel.count; ++i)
            ordered.push(controlsModel.get(i).controlId)
        return ordered
    }

    function currentHidden() {
        const hidden = []
        for (let i = 0; i < controlsModel.count; ++i) {
            const row = controlsModel.get(i)
            if (!row.isShown) hidden.push(row.controlId)
        }
        return hidden
    }

    function moveControl(from, to) {
        const next = Math.max(0, Math.min(controlsModel.count - 1, to))
        if (from < 0 || from >= controlsModel.count || from === next) return
        controlsModel.move(from, next, 1)
    }

    function toggleVisibility(controlId) {
        for (let i = 0; i < controlsModel.count; ++i) {
            if (controlsModel.get(i).controlId === controlId) {
                controlsModel.setProperty(i, "isShown", !controlsModel.get(i).isShown)
                return
            }
        }
    }

    function accept() {
        if (!editing) return
        applyRequested(currentOrder(), currentHidden())
        editing = false
    }

    function cancel() {
        if (!editing) return
        editing = false
        cancelRequested()
    }

    Keys.onEscapePressed: event => {
        cancel()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: "#080F15"
        opacity: 0.58 * editor.reveal
        MouseArea {
            anchors.fill: parent
            onClicked: editor.cancel()
        }
    }

    Item {
        id: panel
        objectName: "waterRougeEditorSheet"
        width: Math.min(parent.width - 30, 690)
        height: Math.min(parent.height - 72, 710)
        anchors.centerIn: parent
        opacity: editor.reveal
        scale: 0.975 + editor.reveal * 0.025
        transformOrigin: Item.Center
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: 24
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#F0304D5B" }
                GradientStop { position: 0.48; color: "#EC243845" }
                GradientStop { position: 1.0; color: "#F11A2D3B" }
            }
            border.width: 1
            border.color: "#7EACC7D4"
        }
        Rectangle {
            x: 18; y: 1
            width: parent.width - 36
            height: 2
            radius: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#0038DCE9" }
                GradientStop { position: 0.35; color: "#9F76E8EF" }
                GradientStop { position: 0.72; color: "#73F1A6AC" }
                GradientStop { position: 1.0; color: "#00F1A6AC" }
            }
        }

        Text {
            id: heading
            x: 27; y: 25
            text: qsTr("Edit Controls")
            color: "#F5FCFF"
            font.family: editor.fontFamily
            font.pixelSize: 31
            font.weight: Font.Light
        }

        Text {
            x: 29; y: 67
            text: qsTr("Drag a handle to change the order")
            color: "#B9D0D8"
            font.family: editor.fontFamily
            font.pixelSize: 17
        }

        Item {
            id: resetAction
            objectName: "waterRougeRestoreDefaults"
            width: 165; height: 42
            anchors.right: parent.right
            anchors.rightMargin: 21
            y: 95

            Text {
                anchors.centerIn: parent
                text: qsTr("Restore Defaults")
                color: "#F1A9AF"
                font.family: editor.fontFamily
                font.pixelSize: 17
                font.weight: Font.Normal
            }
            TapHandler { onTapped: editor.restoreDefaults() }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Restore default Quick Menu layout")
        }

        Rectangle {
            x: 27; y: 137
            width: parent.width - 54
            height: 1
            color: "#59B7D1DB"
        }

        ListView {
            id: controlsList
            objectName: "waterRougeControlList"
            x: 22; y: 151
            width: panel.width - 44
            height: panel.height - y - 105
            clip: true
            model: controlsModel
            spacing: 7
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            displaced: Transition {
                NumberAnimation {
                    properties: "x,y"
                    duration: editor.reduceMotion ? 0 : 180
                    easing.type: Easing.OutCubic
                }
            }

            delegate: Item {
                id: row
                objectName: "waterRougeRow_" + model.controlId
                width: controlsList.width
                height: 67
                property real dragOffsetY: 0
                z: reorderDrag.active ? 10 : 1

                Item {
                    id: rowVisual
                    width: parent.width
                    height: parent.height
                    y: row.dragOffsetY
                    scale: reorderDrag.active && !editor.reduceMotion ? 1.015 : 1
                    Behavior on scale {
                        NumberAnimation { duration: editor.reduceMotion ? 0 : 130 }
                    }

                    Rectangle {
                        visible: reorderDrag.active
                        anchors.fill: rowSurface
                        anchors.topMargin: 6
                        anchors.leftMargin: 1
                        anchors.rightMargin: -1
                        radius: 15
                        color: "#59061019"
                    }

                    Rectangle {
                        id: rowSurface
                        anchors.fill: parent
                        radius: 14
                        color: reorderDrag.active ? "#A13B7484" : "#49334D5A"
                        border.color: reorderDrag.active ? "#B55BDBE8" : "#324B6976"
                        border.width: 1
                        Behavior on color {
                            ColorAnimation { duration: editor.reduceMotion ? 0 : 140 }
                        }

                        Rectangle {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 46; height: 46; radius: 23
                            color: model.isShown ? "#3650B6C7" : "#334C6570"

                            IconImage {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: model.controlId !== "lock"
                                source: model.controlId === "lock" ? "" :
                                        editor.assetBase + (model.controlId === "brightness" ? "brightness" : model.controlId) + ".svg"
                                renderScale: 1
                            }
                            Item {
                                width: 31; height: 31
                                anchors.centerIn: parent
                                visible: model.controlId === "lock"
                                Rectangle {
                                    x: 7; y: 4; width: 17; height: 17
                                    radius: 8
                                    color: "transparent"
                                    border.color: "#E9F9FC"; border.width: 3
                                }
                                Rectangle {
                                    x: 3; y: 15; width: 25; height: 17
                                    radius: 4
                                    color: "#E9F9FC"
                                }
                            }
                        }

                        Text {
                            x: 70
                            width: Math.max(50, visibilityChip.x - x - 8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.controlLabel
                            color: model.isShown ? "#F5FCFF" : "#A0B1BA"
                            font.family: editor.fontFamily
                            font.pixelSize: 21
                            font.weight: Font.Light
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            id: visibilityChip
                            objectName: "waterRougeVisibility_" + model.controlId
                            width: 90; height: 38
                            x: parent.width - width - 52
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 19
                            color: model.isShown ? "#5946BFCD" : "#3A324450"
                            border.color: model.isShown ? "#A471DFEA" : "#50667C85"
                            Text {
                                anchors.centerIn: parent
                                text: model.isShown ? qsTr("Visible") : qsTr("Hidden")
                                color: model.isShown ? "#E6FBFF" : "#C1CDD0"
                                font.family: editor.fontFamily
                                font.pixelSize: 15
                            }
                            TapHandler { onTapped: editor.toggleVisibility(model.controlId) }
                            Accessible.role: Accessible.Button
                            Accessible.name: model.controlLabel + (model.isShown ? qsTr(", hide") : qsTr(", show"))
                        }

                        Item {
                            id: handle
                            objectName: "waterRougeDrag_" + model.controlId
                            x: parent.width - 48
                            width: 43; height: parent.height

                            Repeater {
                                model: 6
                                Rectangle {
                                    x: 13 + (index % 2) * 10
                                    y: 21 + Math.floor(index / 2) * 9
                                    width: 4; height: 4
                                    radius: 2
                                    color: reorderDrag.active ? "#72E6F0" : "#B3D6DE"
                                }
                            }

                            DragHandler {
                                id: reorderDrag
                                target: null
                                xAxis.enabled: false
                                yAxis.enabled: true
                                onTranslationChanged: {
                                    if (active) row.dragOffsetY = translation.y
                                }
                                onActiveChanged: {
                                    if (!active) {
                                        const delta = Math.round(row.dragOffsetY / (row.height + controlsList.spacing))
                                        const previousIndex = index
                                        row.dragOffsetY = 0
                                        editor.moveControl(previousIndex, previousIndex + delta)
                                    } else {
                                        row.dragOffsetY = 0
                                    }
                                }
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Drag to reorder ") + model.controlLabel
                        }
                    }
                }
                Keys.onPressed: event => {
                    if (event.modifiers & Qt.ControlModifier) {
                        if (event.key === Qt.Key_Up) {
                            editor.moveControl(index, index - 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down) {
                            editor.moveControl(index, index + 1)
                            event.accepted = true
                        }
                    }
                }
            }
        }

        Rectangle {
            x: 27
            y: panel.height - 96
            width: panel.width - 54; height: 1
            color: "#446B929D"
        }

        Item {
            id: cancelButton
            objectName: "waterRougeCancel"
            x: 29
            y: panel.height - 80
            width: 140; height: 58
            Text {
                anchors.centerIn: parent
                text: qsTr("Cancel")
                color: "#C7D8DF"
                font.family: editor.fontFamily
                font.pixelSize: 21
            }
            TapHandler { onTapped: editor.cancel() }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Cancel customization")
        }

        Rectangle {
            id: doneButton
            objectName: "waterRougeDone"
            width: 190; height: 55
            x: panel.width - width - 24
            y: panel.height - 77
            radius: 27.5
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#4ED8E7" }
                GradientStop { position: 1.0; color: "#72DBD8" }
            }
            border.color: "#A6EFFFF4"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("Done")
                color: "#173642"
                font.family: editor.fontFamily
                font.pixelSize: 22
                font.weight: Font.Medium
            }
            TapHandler { onTapped: editor.accept() }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Save control arrangement")
        }
    }
}
