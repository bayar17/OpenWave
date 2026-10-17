import QtQuick
import OpenWave.Fonts 1.0
import QtQuick.Controls
import QtQuick.Effects

FocusScope {
    id: root
    property var backend: null
    property bool reduceMotion: false
    property bool canPresent: true
    readonly property real uiScale: Math.max(0.01, Math.min(width / 768, height / 1366))
    property color dimColor: "black"
    property real dimOpacity: 0.38
    property Item backdrop: null
    property color panelTint: "#b37b7d7e"
    property var shownRequest: ({})
    property string phase: "hidden"
    readonly property real unfold: transition.progress
    readonly property real backdropOpacity: transition.alpha
    property real safeTop: 70 * uiScale
    property real safeBottom: 55 * uiScale
    readonly property bool inputPrompt: shownRequest.type === "text_input"
    readonly property bool secretInput: inputPrompt && !!shownRequest.secret
    readonly property bool canRevealPassword: secretInput && !!shownRequest.allowPasswordReveal
    property bool passwordVisible: false
    readonly property bool formPrompt: shownRequest.type === "form"
    readonly property bool selectionPrompt: shownRequest.type === "selection"
    property string selectedChoice: ""
    property string selectionSearchText: ""
    readonly property var filteredChoices: Object.keys(shownRequest.choices || {}).filter(key =>
        (key + " " + shownRequest.choices[key]).toLocaleLowerCase().includes(selectionSearchText.trim().toLocaleLowerCase()))
    readonly property var formKeys: formPrompt ? (shownRequest.fieldOrder || "").split(",") : []
    property var formValues: ({})
    readonly property bool messagePrompt: shownRequest.type === "message"
    readonly property bool compactInput: inputPrompt && !(shownRequest.message || "").length
    readonly property bool inputValid: selectionPrompt ? Object.prototype.hasOwnProperty.call(shownRequest.choices || {}, selectedChoice)
        : formPrompt ? formValid()
        : !inputPrompt || (input.text.length >= (shownRequest.minLength || 0)
            && input.text.length <= (shownRequest.maxLength || 256) && input.acceptableInput)
    readonly property bool interactive: phase === "open" && backend && !backend.busy
    visible: phase !== "hidden"
    focus: visible

    Connections {
        target: root.backend
        function onChanged() { root.synchronize() }
    }
    function synchronize() {
        const request = backend ? backend.request : ({})
        const id = request.id || ""
        if (id === (shownRequest.id || "") && (canPresent || phase === "hidden")) return
        if (phase !== "hidden") {
            if (phase === "closing") return
            closeChoices()
            phase = "closing"
            transition.close()
        } else if (id && canPresent) open(request)
    }
    function open(request) {
        const values = ({})
        for (const key of (request.fieldOrder || "").split(","))
            if (request.fields && request.fields[key]) values[key] = request.fields[key].initialText || ""
        formValues = values
        selectedChoice = request.type === "selection" ? request.initialText || "" : ""
        selectionSearchText = ""
        passwordVisible = false
        shownRequest = request
        formViewport.contentY = 0
        input.text = request.initialText || ""
        phase = "opening"
        transition.open()
    }
    function respond(result) {
        if (!interactive) return
        if (result === "accepted" && !inputValid) return
        if (formPrompt) {
            const values = ({})
            if (result === "accepted") for (const key of formKeys)
                values[key] = fieldActive(key) ? formValues[key] || "" : ""
            backend.respondFields(shownRequest.id, result, values)
        } else backend.respond(shownRequest.id, result, result === "accepted"
            ? (inputPrompt ? input.text : selectionPrompt ? selectedChoice : "") : "")
    }
    function fieldActive(key) {
        const field = (shownRequest.fields || {})[key] || ({})
        return !field.activeWhenField || formValues[field.activeWhenField] === field.activeWhenValue
    }
    function setField(key, value) { formValues = Object.assign({}, formValues, {[key]: value}) }
    function formValid() {
        for (const key of formKeys) {
            if (!fieldActive(key)) continue
            const field = shownRequest.fields[key]
            const value = formValues[key] || ""
            if (value.length < (field.minLength || 0) || value.length > (field.maxLength || 256)
                    || /[\x00-\x1f\x7f]/.test(value)) return false
            if (field.maxBytes && encodeURIComponent(value).replace(/%[A-F\d]{2}/gi, "x").length > field.maxBytes) return false
            if (field.choices && !Object.prototype.hasOwnProperty.call(field.choices, value)) return false
        }
        return true
    }
    function focusForm(backwards) {
        const items = []
        for (let i = 0; i < formRows.count; ++i) {
            const row = formRows.itemAt(i)
            if (row && row.visible) items.push(row.control)
        }
        items.push(cancelAction, acceptAction)
        const current = items.findIndex(item => item.activeFocus)
        const next = (current + (backwards ? -1 : 1) + items.length) % items.length
        if (items.length) {
            const item = items[next]
            item.forceActiveFocus()
            if (item !== cancelAction && item !== acceptAction) {
                const top = item.mapToItem(formContent, 0, 0).y
                if (top < formViewport.contentY) formViewport.contentY = top
                else if (top + item.height > formViewport.contentY + formViewport.height)
                    formViewport.contentY = top + item.height - formViewport.height
            }
        }
    }
    function closeChoices() {
        let closed = false
        for (let i = 0; i < formRows.count; ++i) {
            const row = formRows.itemAt(i)
            if (row && row.choice.popup.visible) { row.choice.popup.close(); closed = true }
        }
        return closed
    }
    Component.onCompleted: synchronize()
    onCanPresentChanged: synchronize()
    VerticalUnfoldTransition {
        id: transition
        reduceMotion: root.reduceMotion
        onOpened: {
            root.phase = "open"
            if (root.inputPrompt) { input.forceActiveFocus(); input.selectAll() }
            else if (root.selectionPrompt) selectionSearch.forceActiveFocus()
            else if (root.formPrompt && formRows.count) formRows.itemAt(0).control.forceActiveFocus()
            else if (root.messagePrompt) acceptAction.forceActiveFocus()
            else cancelAction.forceActiveFocus()
        }
        onClosed: {
            input.text = ""
            root.passwordVisible = false
            root.formValues = ({})
            root.selectedChoice = ""
            root.selectionSearchText = ""
            root.shownRequest = ({})
            root.phase = "hidden"
            root.synchronize()
        }
    }
    Keys.onPressed: event => {
        if (root.selectionPrompt && root.interactive && (event.key === Qt.Key_Down || event.key === Qt.Key_Up)) {
            const index = root.filteredChoices.indexOf(root.selectedChoice)
            const next = Math.max(0, Math.min(root.filteredChoices.length - 1, index + (event.key === Qt.Key_Down ? 1 : -1)))
            if (root.filteredChoices.length) root.selectedChoice = root.filteredChoices[next]
            selectionList.forceActiveFocus()
            event.accepted = true
            return
        }
        if (root.selectionPrompt && selectionList.activeFocus && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            root.respond("accepted"); event.accepted = true; return
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            if (root.interactive) {
                if (root.formPrompt) root.focusForm(event.key === Qt.Key_Backtab)
                else if (root.selectionPrompt) {
                    if (selectionSearch.activeFocus) cancelAction.forceActiveFocus()
                    else if (cancelAction.activeFocus) acceptAction.forceActiveFocus()
                    else selectionSearch.forceActiveFocus()
                }
                else if (root.canRevealPassword) {
                    const controls = [input, revealPasswordAction, cancelAction, acceptAction]
                    const index = controls.findIndex(control => control.activeFocus)
                    controls[(index + (event.key === Qt.Key_Backtab ? 3 : 1) + 4) % 4].forceActiveFocus()
                }
                else if (root.inputPrompt && acceptAction.activeFocus) input.forceActiveFocus()
                else if (!root.messagePrompt && input.activeFocus) cancelAction.forceActiveFocus()
                else if (!root.messagePrompt && acceptAction.activeFocus) cancelAction.forceActiveFocus()
                else acceptAction.forceActiveFocus()
            }
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (!root.closeChoices()) root.respond("cancelled")
            event.accepted = true
        }
    }
    // The modal input shield is independent of the panel's unfolding transform.
    Rectangle {
        objectName: "systemPromptDimmer"
        anchors.fill: parent
        color: root.dimColor
        opacity: root.dimOpacity * transition.alpha
    }
    MouseArea { anchors.fill: parent; preventStealing: true; onWheel: wheel => wheel.accepted = true }
    Item {
        id: panel
        objectName: "systemPromptPanel"
        width: Math.max(0, Math.min(622 * root.uiScale, root.width - 48 * root.uiScale))
        height: Math.max(0, Math.min(content.implicitHeight + 54 * root.uiScale,
            root.height - root.safeTop - root.safeBottom - 32 * root.uiScale))
        x: (root.width - width) / 2
        y: root.safeTop + (root.height - root.safeTop - root.safeBottom - height) / 2
        opacity: transition.alpha
        transform: Scale {
            origin.x: panel.width / 2; origin.y: panel.height / 2
            xScale: 1; yScale: transition.verticalScale
        }
        Item {
            id: frost
            anchors.fill: parent
            visible: root.backdrop !== null
            readonly property int blurRadius: Math.max(1, Math.round(32 * root.uiScale))
            layer.enabled: root.visible && visible
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: frostMask
            }
            ShaderEffectSource {
                id: frostSource
                width: panel.width + 2 * frost.blurRadius
                height: panel.height + 2 * frost.blurRadius
                sourceItem: root.visible ? root.backdrop : null
                sourceRect: {
                    const origin = root.backdrop ? root.mapToItem(root.backdrop, panel.x, panel.y) : Qt.point(0, 0)
                    return Qt.rect(origin.x - frost.blurRadius, origin.y - frost.blurRadius, width, height)
                }
                live: root.visible
                hideSource: false
                visible: false
            }
            MultiEffect {
                x: -frost.blurRadius; y: -frost.blurRadius
                width: frostSource.width; height: frostSource.height
                source: frostSource
                blurEnabled: true
                blur: 1
                blurMax: frost.blurRadius
                autoPaddingEnabled: false
            }
        }
        Rectangle {
            id: frostMask
            anchors.fill: parent
            radius: 20 * root.uiScale
            color: "white"
            visible: false
            layer.enabled: root.visible && frost.visible
        }
        Rectangle {
            objectName: "systemPromptSurface"
            anchors.fill: parent
            radius: 20 * root.uiScale
            color: root.panelTint
        }
        MouseArea { anchors.fill: parent; preventStealing: true }
        Column {
            id: content
            x: 32 * root.uiScale; y: 27 * root.uiScale; width: panel.width - 64 * root.uiScale
            spacing: 8 * root.uiScale
            Text {
                id: promptTitle
                objectName: "systemPromptTitle"
                width: parent.width
                text: root.shownRequest.title || ""
                textFormat: Text.PlainText
                color: "white"
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: Math.max(1, Math.round(36 * root.uiScale))
                wrapMode: Text.Wrap
                maximumLineCount: Math.max(1, Math.min(3, Math.floor((root.height - root.safeTop
                    - root.safeBottom - (32 + 54 + (root.inputPrompt ? 66 : 0) + 78 + 24) * root.uiScale)
                    / (48 * root.uiScale))))
                elide: Text.ElideRight
            }
            Flickable {
                id: messageViewport
                visible: message.text.length > 0
                width: parent.width
                height: Math.min(message.implicitHeight,
                    Math.max(0, root.height - root.safeTop - root.safeBottom
                        - promptTitle.implicitHeight - (32 + 54 + (root.inputPrompt ? 66 : 0) + 78 + 24) * root.uiScale))
                contentWidth: width; contentHeight: message.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                clip: true
                Text {
                    id: message
                    objectName: "systemPromptMessage"
                    width: parent.width
                    text: root.shownRequest.message || ""
                    textFormat: Text.PlainText
                    color: "white"
                    font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                    wrapMode: Text.Wrap
                }
            }
            TextField {
                id: input
                objectName: "systemPromptInput"
                visible: root.inputPrompt
                width: parent.width; height: (root.compactInput ? 52 : 66) * root.uiScale
                enabled: root.interactive
                maximumLength: root.shownRequest.maxLength || 256
                placeholderText: root.shownRequest.placeholder || ""
                echoMode: root.secretInput && !root.passwordVisible ? TextInput.Password : TextInput.Normal
                inputMethodHints: root.secretInput
                    ? Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhHiddenText | Qt.ImhNoAutoUppercase
                    : Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                color: "white"; placeholderTextColor: "#99ffffff"
                leftPadding: 0; rightPadding: 0
                font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                validator: RegularExpressionValidator {
                    regularExpression: new RegExp(root.shownRequest.validationPattern || "[\\s\\S]*")
                }
                background: Item {}
                Rectangle { anchors.bottom: parent.bottom; width: parent.width
                    height: 2 * root.uiScale; color: "#aaffffff"; antialiasing: true }
                onAccepted: root.respond("accepted")
            }
            Column {
                visible: root.selectionPrompt
                width: parent.width; spacing: 16 * root.uiScale
                TextField {
                    id: selectionSearch
                    objectName: "systemPromptSelectionSearch"
                    width: parent.width; height: 66 * root.uiScale
                    enabled: root.interactive
                    text: root.selectionSearchText
                    onTextEdited: root.selectionSearchText = text
                    placeholderText: root.shownRequest.placeholder || qsTr("Search")
                    maximumLength: 128
                    color: "white"; placeholderTextColor: "#99ffffff"
                    leftPadding: 0; rightPadding: 0
                    font.family: Fonts.family; font.weight: Font.ExtraLight
                    font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                    background: Item {}
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width
                        height: 2 * root.uiScale; color: "#aaffffff"; antialiasing: true }
                    onAccepted: root.respond("accepted")
                }
                ListView {
                    id: selectionList
                    objectName: "systemPromptSelectionList"
                    width: parent.width
                    height: Math.min(480 * root.uiScale, Math.max(0, root.height - root.safeTop - root.safeBottom
                        - promptTitle.implicitHeight - (32 + 54 + 66 + 78 + 48) * root.uiScale))
                    model: root.filteredChoices
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    currentIndex: root.filteredChoices.indexOf(root.selectedChoice)
                    onCurrentIndexChanged: if (root.selectionPrompt && currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: AbstractButton {
                        required property string modelData
                        width: selectionList.width; height: 64 * root.uiScale
                        enabled: root.interactive
                        text: root.shownRequest.choices[modelData]
                        Accessible.name: text
                        onClicked: root.selectedChoice = modelData
                        contentItem: Text {
                            text: parent.text; textFormat: Text.PlainText
                            color: root.selectedChoice === parent.modelData ? "#00b7f0" : "white"
                            font.family: Fonts.family; font.weight: Font.ExtraLight
                            font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        background: Rectangle {
                            color: parent.down ? "#18ffffff" : "transparent"
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width
                                height: 2 * root.uiScale; color: "#55ffffff"; antialiasing: true }
                        }
                    }
                    Text {
                        anchors.centerIn: parent; visible: selectionList.count === 0
                        text: qsTr("No matches"); color: "#99ffffff"
                        font.family: Fonts.family; font.weight: Font.ExtraLight
                        font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                    }
                }
            }
            Flickable {
                id: formViewport
                objectName: "systemPromptFormViewport"
                visible: root.formPrompt
                width: parent.width
                height: Math.min(formContent.implicitHeight, Math.max(0,
                    root.height - root.safeTop - root.safeBottom - promptTitle.height
                    - (messageViewport.visible ? messageViewport.height : 0)
                    - footer.height - 110 * root.uiScale))
                contentWidth: width; contentHeight: formContent.implicitHeight
                clip: true; boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                Column {
                    id: formContent
                    width: parent.width; spacing: 8 * root.uiScale
                    Repeater {
                        id: formRows
                        model: root.formKeys
                        delegate: Item {
                            id: fieldRow
                            required property string modelData
                            readonly property var field: (root.shownRequest.fields || {})[modelData] || ({})
                            readonly property bool dropdown: !!field.choices
                            readonly property var control: dropdown ? choice : fieldInput
                            property alias choice: choice
                            width: parent.width; height: 66 * root.uiScale + fieldLabel.height
                            visible: root.fieldActive(modelData)
                            Text {
                                id: fieldLabel
                                objectName: "systemPromptFieldLabel_" + fieldRow.modelData
                                width: parent.width
                                height: visible ? implicitHeight : 0
                                visible: !fieldRow.dropdown && !!fieldRow.field.label
                                text: fieldRow.field.label || ""
                                textFormat: Text.PlainText; wrapMode: Text.Wrap
                                color: "#ccffffff"
                                font.family: Fonts.family; font.weight: Font.ExtraLight
                                font.pixelSize: Math.max(1, Math.round(22 * root.uiScale))
                            }
                            TextField {
                                id: fieldInput
                                objectName: fieldRow.dropdown ? "" : "systemPromptField_" + fieldRow.modelData
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.top: fieldLabel.bottom; anchors.bottom: parent.bottom
                                visible: !fieldRow.dropdown
                                enabled: root.interactive && fieldRow.visible
                                text: root.formValues[fieldRow.modelData] || ""
                                onTextEdited: root.setField(fieldRow.modelData, text)
                                maximumLength: fieldRow.field.maxLength || 256
                                placeholderText: fieldRow.field.placeholder || fieldRow.field.label || ""
                                echoMode: fieldRow.field.secret ? TextInput.Password : TextInput.Normal
                                inputMethodHints: fieldRow.field.secret ? Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
                                    : Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                                color: "white"; placeholderTextColor: "#99ffffff"
                                leftPadding: 0; rightPadding: 0
                                font.family: Fonts.family; font.weight: Font.ExtraLight
                                font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                                background: Item {}
                                onAccepted: root.respond("accepted")
                            }
                            Rectangle {
                                visible: !fieldRow.dropdown
                                anchors.bottom: parent.bottom; width: parent.width
                                height: 2 * root.uiScale; color: "#aaffffff"; antialiasing: true
                            }
                            PromptDropdown {
                                id: choice
                                objectName: fieldRow.dropdown ? "systemPromptField_" + fieldRow.modelData : ""
                                anchors.fill: parent; visible: fieldRow.dropdown
                                enabled: root.interactive && fieldRow.visible
                                choices: fieldRow.field.choices || ({})
                                value: root.formValues[fieldRow.modelData] || ""
                                label: fieldRow.field.label || ""
                                uiScale: root.uiScale; reduceMotion: root.reduceMotion
                                font.family: Fonts.family; font.weight: Font.ExtraLight
                                font.pixelSize: Math.max(1, Math.round(26 * root.uiScale))
                                onSelected: value => root.setField(fieldRow.modelData, value)
                            }
                        }
                    }
                }
            }
            Item {
                id: footer
                width: parent.width; height: (root.compactInput ? 54 : 78) * root.uiScale
                Button {
                    id: revealPasswordAction
                    objectName: "systemPromptShowPassword"
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    visible: root.canRevealPassword
                    enabled: root.interactive
                    checkable: true
                    checked: root.passwordVisible
                    width: Math.min(implicitWidth, Math.max(0, parent.width - actions.width - 12 * root.uiScale))
                    height: Math.max(44 * root.uiScale, implicitHeight)
                    text: root.passwordVisible ? qsTr("Hide password") : qsTr("Show password")
                    font.family: Fonts.family; font.weight: Font.ExtraLight
                    font.pixelSize: Math.max(1, Math.round(22 * root.uiScale))
                    Accessible.name: text
                    contentItem: Text {
                        text: revealPasswordAction.text; textFormat: Text.PlainText; color: "white"
                        font: revealPasswordAction.font; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle { radius: 8 * root.uiScale; color: revealPasswordAction.down || revealPasswordAction.visualFocus ? "#18ffffff" : "transparent" }
                    onClicked: root.passwordVisible = !root.passwordVisible
                }
                Row {
                    id: actions
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    spacing: 30 * root.uiScale
                    Button {
                        id: cancelAction
                        objectName: "systemPromptCancel"
                        visible: !root.messagePrompt
                        width: Math.min(implicitWidth, (content.width - actions.spacing - (root.canRevealPassword ? 164 * root.uiScale : 0)) / 2)
                        enabled: root.interactive
                        text: root.shownRequest.cancelLabel || qsTr("Cancel")
                        font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: Math.max(1, Math.round(34 * root.uiScale))
                        contentItem: Text {
                            text: cancelAction.text; textFormat: Text.PlainText; color: "white"
                            font: cancelAction.font; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 8 * root.uiScale; color: cancelAction.down || cancelAction.visualFocus ? "#18ffffff" : "transparent" }
                        onClicked: root.respond("cancelled")
                    }
                    Button {
                        id: acceptAction
                        objectName: "systemPromptAccept"
                        enabled: root.interactive && root.inputValid
                        width: Math.min(implicitWidth, root.messagePrompt ? content.width : (content.width - actions.spacing - (root.canRevealPassword ? 164 * root.uiScale : 0)) / 2)
                        text: root.shownRequest.acceptLabel || (root.messagePrompt ? qsTr("OK") : qsTr("Yes"))
                        opacity: root.inputValid ? 1 : 0.5
                        font.family: Fonts.family; font.weight: Font.ExtraLight; font.pixelSize: Math.max(1, Math.round(34 * root.uiScale))
                        contentItem: Text {
                            text: acceptAction.text; textFormat: Text.PlainText; color: "white"
                            font: acceptAction.font; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        }
                        background: Rectangle { radius: 8 * root.uiScale; color: acceptAction.down || acceptAction.visualFocus ? "#18ffffff" : "transparent" }
                        onClicked: root.respond("accepted")
                    }
                }
            }
        }
    }
}
