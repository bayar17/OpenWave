import QtQuick
import QtTest
import ".." as Ocean

Item {
    id: screen
    width: 768; height: 1366
    property int behindClicks: 0
    MouseArea { anchors.fill: parent; onClicked: screen.behindClicks++ }
    QtObject {
        id: backend
        property var request: ({})
        property bool busy: false
        property int replies: 0
        property string result: ""
        property string text: ""
        property var values: ({})
        signal changed()
        function respond(id, outcome, value) {
            replies++; result = outcome; text = value
            request = ({}); changed()
        }
        function respondFields(id, outcome, fields) {
            values = fields; respond(id, outcome, "")
        }
    }
    Ocean.PromptDialog {
        id: dialog
        anchors.fill: parent
        backend: backend
    }
    TestCase {
        name: "SystemPrompt"
        when: windowShown
        function init() {
            dialog.reduceMotion = true
            backend.request = ({}); backend.changed()
            tryCompare(dialog, "phase", "hidden")
            backend.replies = 0; backend.busy = false; screen.behindClicks = 0
            backend.values = ({})
            screen.width = 768; screen.height = 1366
            dialog.canPresent = true
            dialog.reduceMotion = false
        }
        function open(type, extra) {
            backend.request = Object.assign({id: "request-1", type: type, title: "Title", message: "Message"}, extra || {})
            backend.changed()
        }
        function networkForm() {
            open("form", {message: "", fieldOrder: "ssid,security,password", fields: {
                ssid: {placeholder: "Network name (SSID)", minLength: 1, maxLength: 32, maxBytes: 32},
                security: {label: "Security", initialText: "wpa", choices: {wpa: "WPA Personal", open: "Open"}},
                password: {placeholder: "Password", secret: true, minLength: 8, maxLength: 63,
                    activeWhenField: "security", activeWhenValue: "wpa"}}})
        }
        function zoneSelection() {
            open("selection", {title: "Time zone", message: "", initialText: "Europe/Istanbul",
                placeholder: "Search city or region", acceptLabel: "Save", choices: {
                    "Europe/Istanbul": "Europe/Istanbul", "Europe/London": "Europe/London",
                    "America/New_York": "America/New York", "UTC": "UTC"}})
        }
        function test_secretInputRevealAndCleanup() {
            screen.width = 544; screen.height = 960
            open("text_input", {secret: true, allowPasswordReveal: true, message: "WPA Personal",
                minLength: 8, maxLength: 63, acceptLabel: "Connect", initialText: "test-password"})
            const input = findChild(dialog, "systemPromptInput")
            const reveal = findChild(dialog, "systemPromptShowPassword")
            compare(input.echoMode, TextInput.Password)
            compare(reveal.enabled, false)
            tryCompare(dialog, "phase", "open")
            verify(reveal.visible && reveal.width > 0)
            const accept = findChild(dialog, "systemPromptAccept")
            const cancel = findChild(dialog, "systemPromptCancel")
            verify(reveal.mapToItem(dialog, reveal.width, 0).x < cancel.mapToItem(dialog, 0, 0).x)
            mouseClick(reveal)
            compare(input.echoMode, TextInput.Normal)
            compare(input.text, "test-password")
            compare(reveal.text, "Hide password")
            mouseClick(reveal)
            compare(input.echoMode, TextInput.Password)
            compare(input.text, "test-password")
            mouseClick(reveal)
            mouseClick(cancel)
            compare(backend.text, "")
            tryCompare(dialog, "phase", "hidden")
            compare(input.text, "")
            compare(dialog.passwordVisible, false)
            open("text_input", {secret: true, allowPasswordReveal: false, message: ""})
            tryCompare(dialog, "phase", "open")
            compare(input.echoMode, TextInput.Password)
            compare(reveal.visible, false)
            dialog.respond("cancelled")
            tryCompare(dialog, "phase", "hidden")
            open("text_input", {message: ""})
            tryCompare(dialog, "phase", "open")
            compare(input.echoMode, TextInput.Normal)
            compare(reveal.visible, false)
        }
        function test_selection_search_save_and_cancel() {
            zoneSelection()
            compare(dialog.phase, "opening")
            compare(dialog.interactive, false)
            tryCompare(dialog, "phase", "open")
            const list = findChild(dialog, "systemPromptSelectionList")
            const search = findChild(dialog, "systemPromptSelectionSearch")
            compare(dialog.selectedChoice, "Europe/Istanbul")
            compare(list.count, 4)
            search.forceActiveFocus()
            for (const key of [Qt.Key_L, Qt.Key_O, Qt.Key_N, Qt.Key_D, Qt.Key_O, Qt.Key_N]) keyClick(key)
            tryCompare(list, "count", 1)
            compare(list.itemAtIndex(0).modelData, "Europe/London")
            mouseClick(list.itemAtIndex(0))
            compare(dialog.selectedChoice, "Europe/London")
            compare(backend.replies, 0)
            mouseClick(findChild(dialog, "systemPromptAccept"))
            compare(backend.result, "accepted")
            compare(backend.text, "Europe/London")
            tryCompare(dialog, "phase", "hidden")
            compare(dialog.selectedChoice, "")
            compare(dialog.selectionSearchText, "")
            zoneSelection()
            tryCompare(dialog, "phase", "open")
            dialog.selectedChoice = "UTC"
            dialog.respond("cancelled")
            compare(backend.result, "cancelled")
            compare(backend.text, "")
        }
        function test_selection_keyboard_search_and_validation() {
            open("selection", {message: "", choices: {"Europe/London": "Europe/London", "UTC": "UTC"}})
            tryCompare(dialog, "phase", "open")
            compare(dialog.inputValid, false)
            dialog.respond("accepted")
            compare(backend.replies, 0)
            dialog.selectionSearchText = "missing"
            compare(dialog.filteredChoices.length, 0)
            dialog.selectionSearchText = "london"
            keyClick(Qt.Key_Down)
            compare(dialog.selectedChoice, "Europe/London")
            keyClick(Qt.Key_Return)
            compare(backend.text, "Europe/London")
        }
        function test_selection_large_list_and_scaled_viewport() {
            screen.width = 544; screen.height = 960
            const choices = ({})
            for (let i = 0; i < 600; ++i) choices["Region/City_" + i] = "Region/City " + i
            open("selection", {message: "", choices: choices, initialText: "Region/City_599"})
            tryCompare(dialog, "phase", "open")
            const panel = findChild(dialog, "systemPromptPanel")
            const list = findChild(dialog, "systemPromptSelectionList")
            compare(list.count, 600)
            verify(list.contentHeight > list.height)
            verify(panel.height <= screen.height - dialog.safeTop - dialog.safeBottom)
            tryVerify(() => list.currentItem !== null)
            verify(list.currentItem.y >= list.contentY)
            verify(list.currentItem.y + list.currentItem.height <= list.contentY + list.height + 1)
            dialog.respond("cancelled")
        }
        function test_ipDnsFormLabelsModesAndSmallScreen() {
            screen.width = 544; screen.height = 960
            open("form", {title: "IP and DNS settings", message: "Saved Network\nChanging the active network may briefly interrupt Wi-Fi.",
                fieldOrder: "ipMode,address,prefix,gateway,dnsMode,dnsServers", fields: {
                    ipMode: {label: "IP configuration", initialText: "manual",
                        choices: {automatic: "Automatic (DHCP)", manual: "Manual"}},
                    address: {label: "IPv4 address", initialText: "192.168.1.50", minLength: 1,
                        activeWhenField: "ipMode", activeWhenValue: "manual"},
                    prefix: {label: "Subnet prefix (0–32)", initialText: "24", minLength: 1,
                        activeWhenField: "ipMode", activeWhenValue: "manual"},
                    gateway: {label: "Gateway (optional)", initialText: "192.168.1.1",
                        activeWhenField: "ipMode", activeWhenValue: "manual"},
                    dnsMode: {label: "DNS configuration", initialText: "manual",
                        choices: {automatic: "Automatic", manual: "Manual"}},
                    dnsServers: {label: "DNS servers", initialText: "1.1.1.1, 2001:4860:4860::8888", minLength: 1,
                        activeWhenField: "dnsMode", activeWhenValue: "manual"}
                }, acceptLabel: "Save", cancelLabel: "Cancel"})
            tryCompare(dialog, "phase", "open")
            const panel = findChild(dialog, "systemPromptPanel")
            const label = findChild(dialog, "systemPromptFieldLabel_address")
            const address = findChild(dialog, "systemPromptField_address")
            verify(label.visible)
            compare(label.text, "IPv4 address")
            verify(label.mapToItem(panel, 0, label.height).y <= address.mapToItem(panel, 0, 0).y)
            const save = findChild(dialog, "systemPromptAccept")
            verify(save.mapToItem(panel, 0, save.height).y <= panel.height)
            verify(panel.y >= dialog.safeTop)
            verify(panel.y + panel.height <= screen.height - dialog.safeBottom)
            const viewport = findChild(dialog, "systemPromptFormViewport")
            backend.request = Object.assign({}, backend.request, {message: "Network configuration details. ".repeat(30)})
            // Update the pending request without interrupting an already visible prompt.
            dialog.shownRequest = backend.request
            verify(viewport.contentHeight > viewport.height)
            dialog.focusForm(false)
            verify(viewport.contentY >= 0)
            dialog.setField("ipMode", "automatic")
            dialog.setField("dnsMode", "automatic")
            compare(address.visible, false)
            compare(findChild(dialog, "systemPromptField_dnsServers").visible, false)
            compare(dialog.inputValid, true)
            dialog.respond("accepted")
            compare(backend.values.ipMode, "automatic")
            compare(backend.values.address, "")
            compare(backend.values.prefix, "")
            compare(backend.values.gateway, "")
            compare(backend.values.dnsServers, "")
        }
        function test_form_validation_and_masked_password() {
            networkForm()
            tryCompare(dialog, "phase", "open")
            const password = findChild(dialog, "systemPromptField_password")
            compare(password.echoMode, TextInput.Password)
            compare(dialog.inputValid, false)
            dialog.setField("ssid", "New Network")
            dialog.setField("password", "short")
            compare(dialog.inputValid, false)
            dialog.respond("accepted")
            compare(backend.replies, 0)
            dialog.setField("password", "test-password")
            compare(dialog.inputValid, true)
            dialog.setField("ssid", "あ".repeat(11))
            compare(dialog.inputValid, false)
            dialog.setField("ssid", "New Network")
            dialog.respond("accepted")
            compare(backend.values.ssid, "New Network")
            compare(backend.values.security, "wpa")
            compare(backend.values.password, "test-password")
            tryCompare(dialog, "phase", "hidden")
            compare(Object.keys(dialog.formValues).length, 0)
        }
        function test_dropdown_slides_down_and_open_network_needs_no_password() {
            networkForm()
            tryCompare(dialog, "phase", "open")
            const dropdown = findChild(dialog, "systemPromptField_security")
            dropdown.popup.open()
            wait(55)
            verify(dropdown.reveal > 0 && dropdown.reveal < 1)
            const surface = findChild(dropdown.popup, "systemPromptDropdownSurface")
            verify(surface.y < 0)
            tryCompare(dropdown, "reveal", 1)
            compare(surface.y, 0)
            wait(40)
            // Popup delegates are parented through the window overlay.
            mouseClick(dropdown.popup.contentItem, 80, 87)
            tryCompare(dropdown.popup, "visible", false)
            compare(dialog.formValues.security, "open")
            compare(findChild(dialog, "systemPromptField_password").visible, false)
            dialog.setField("ssid", "Open Hidden")
            compare(dialog.inputValid, true)
            dialog.respond("accepted")
            compare(backend.values.password, "")
            compare(backend.values.security, "open")
        }
        function test_form_dropdown_reduced_motion_and_cancel() {
            dialog.reduceMotion = true
            networkForm()
            tryCompare(dialog, "phase", "open")
            const dropdown = findChild(dialog, "systemPromptField_security")
            dropdown.popup.open()
            tryCompare(dropdown, "reveal", 1)
            dropdown.popup.close()
            tryCompare(dropdown, "reveal", 0)
            dialog.respond("cancelled")
            compare(backend.result, "cancelled")
            compare(Object.keys(backend.values).length, 0)
        }
        function test_dropdown_escape_only_closes_choices() {
            networkForm()
            tryCompare(dialog, "phase", "open")
            const dropdown = findChild(dialog, "systemPromptField_security")
            dropdown.popup.open()
            tryCompare(dropdown, "reveal", 1)
            keyClick(Qt.Key_Escape)
            tryCompare(dropdown.popup, "visible", false)
            compare(backend.replies, 0)
            compare(dialog.phase, "open")
            keyClick(Qt.Key_Escape)
            compare(backend.result, "cancelled")
            tryCompare(dialog, "phase", "hidden")
        }
        function test_dropdown_scales_with_prompt() {
            screen.width = 544; screen.height = 960
            networkForm()
            tryCompare(dialog, "phase", "open")
            const dropdown = findChild(dialog, "systemPromptField_security")
            dropdown.popup.open()
            tryCompare(dropdown, "reveal", 1)
            const surface = findChild(dropdown.popup, "systemPromptDropdownSurface")
            compare(dropdown.popup.width, dropdown.width)
            compare(surface.width, dropdown.width)
            const list = surface.children[1]
            const entry = list.itemAtIndex(0)
            verify(entry !== null)
            compare(entry.width, dropdown.width)
            fuzzyCompare(entry.height, 58 * dialog.uiScale, .01)
            compare(entry.font.pixelSize, dropdown.font.pixelSize)
            dropdown.popup.close()
            dialog.respond("cancelled")
        }
        function test_dropdown_keeps_outer_viewport_scale() {
            screen.scale = .7
            networkForm()
            tryCompare(dialog, "phase", "open")
            const dropdown = findChild(dialog, "systemPromptField_security")
            dropdown.popup.open()
            tryCompare(dropdown, "reveal", 1)
            fuzzyCompare(dropdown.popup.scale, .7, .001)
            compare(dropdown.popup.transformOrigin, Item.TopLeft)
            dropdown.popup.close()
            dialog.respond("cancelled")
            tryCompare(dialog, "phase", "hidden")
            screen.scale = 1
        }
        function test_animation_and_shield() {
            open("confirmation")
            compare(dialog.phase, "opening")
            verify(!dialog.interactive)
            mouseClick(screen, 10, 10)
            compare(screen.behindClicks, 0)
            tryCompare(dialog, "phase", "open")
            const panel = findChild(dialog, "systemPromptPanel")
            compare(panel.width, 622)
            compare(panel.x + panel.width / 2, screen.width / 2)
            mouseClick(findChild(dialog, "systemPromptCancel"))
            compare(backend.result, "cancelled")
            compare(dialog.phase, "closing")
            mouseClick(screen, 10, 10)
            compare(screen.behindClicks, 0)
            verify(dialog.visible)
            tryCompare(dialog, "phase", "hidden")
            mouseClick(screen, 10, 10)
            compare(screen.behindClicks, 1)
        }
        function test_contents_unfold_with_panel() {
            open("text_input", {initialText: "Natural size"})
            const panel = findChild(dialog, "systemPromptPanel")
            const surface = findChild(dialog, "systemPromptSurface")
            const title = findChild(dialog, "systemPromptTitle")
            compare(panel.transform[0].xScale, 1)
            compare(panel.transform[0].yScale, 0.02)
            compare(panel.opacity, 0)
            compare(dialog.backdropOpacity, 0)
            wait(65)
            const center = panel.y + panel.height / 2
            const top = surface.mapToItem(screen, 0, 0).y
            const bottom = surface.mapToItem(screen, 0, surface.height).y
            verify(top < center && bottom > center)
            fuzzyCompare((top + bottom) / 2, center, 0.01)
            fuzzyCompare(surface.mapToItem(screen, surface.width, 0).x
                - surface.mapToItem(screen, 0, 0).x, panel.width, 0.01)
            verify(panel.opacity > 0 && panel.opacity < 1)
            compare(dialog.backdropOpacity, panel.opacity)
            const titleTop = title.mapToItem(screen, 0, 0)
            const titleBottom = title.mapToItem(screen, 0, title.height)
            const scale = panel.transform[0].yScale
            fuzzyCompare(titleTop.y, center + (27 - panel.height / 2) * scale, 0.01)
            fuzzyCompare(titleBottom.y - titleTop.y, title.height * scale, 0.01)
            const input = findChild(dialog, "systemPromptInput")
            const accept = findChild(dialog, "systemPromptAccept")
            for (const item of [input, accept]) {
                fuzzyCompare(item.mapToItem(screen, 0, item.height).y - item.mapToItem(screen, 0, 0).y,
                    item.height * scale, 0.01)
            }
            tryCompare(dialog, "phase", "open")
            compare(panel.transform[0].yScale, 1)
            compare(panel.opacity, 1)
            backend.request = ({}); backend.changed()
            compare(dialog.phase, "closing")
            compare(dialog.shownRequest.id, "request-1")
            wait(65)
            verify(dialog.visible)
            verify(panel.opacity > 0 && panel.opacity < 1)
            verify(panel.transform[0].yScale > 0.02 && panel.transform[0].yScale < 1)
            fuzzyCompare(title.mapToItem(screen, 0, title.height).y - title.mapToItem(screen, 0, 0).y,
                title.height * panel.transform[0].yScale, 0.01)
            tryCompare(dialog, "phase", "hidden")
            compare(panel.transform[0].yScale, 0.02)
            compare(dialog.backdropOpacity, 0)
            compare(dialog.shownRequest.id, undefined)
        }
        function test_content_opacity_stays_synced_data() {
            return [
                {tag: "message", type: "message", extra: {}, actionOpacity: 1},
                {tag: "confirmation", type: "confirmation", extra: {}, actionOpacity: 1},
                {tag: "valid input", type: "text_input", extra: {initialText: "notes.txt", minLength: 1}, actionOpacity: 1},
                {tag: "empty input", type: "text_input", extra: {minLength: 1}, actionOpacity: 0.5}
            ]
        }
        function test_content_opacity_stays_synced(data) {
            open(data.type, data.extra)
            const panel = findChild(dialog, "systemPromptPanel")
            const accept = findChild(dialog, "systemPromptAccept")
            const title = findChild(dialog, "systemPromptTitle")
            wait(65)
            verify(!accept.enabled)
            compare(accept.opacity, data.actionOpacity)
            compare(title.opacity, 1)
            fuzzyCompare(panel.opacity, (panel.transform[0].yScale - 0.02) / 0.98, 0.001)
            tryCompare(dialog, "phase", "open")
            compare(accept.opacity, data.actionOpacity)
            backend.request = ({}); backend.changed()
            wait(65)
            verify(!accept.enabled)
            compare(accept.opacity, data.actionOpacity)
            compare(title.opacity, 1)
            fuzzyCompare(panel.opacity, (panel.transform[0].yScale - 0.02) / 0.98, 0.001)
            tryCompare(dialog, "phase", "hidden")
        }
        function test_reduced_motion_fades_without_scaling() {
            dialog.reduceMotion = true
            open("confirmation")
            const panel = findChild(dialog, "systemPromptPanel")
            compare(dialog.phase, "opening")
            compare(panel.transform[0].yScale, 1)
            compare(panel.opacity, 0)
            wait(30)
            verify(panel.opacity > 0 && panel.opacity < 1)
            compare(panel.transform[0].yScale, 1)
            tryCompare(dialog, "phase", "open")
            backend.request = ({}); backend.changed()
            compare(dialog.phase, "closing")
            compare(panel.transform[0].yScale, 1)
            verify(dialog.visible)
            tryCompare(dialog, "phase", "hidden")
            compare(panel.opacity, 0)
        }
        function test_reduced_motion_changed_during_opening() {
            open("confirmation")
            wait(40)
            dialog.reduceMotion = true
            const panel = findChild(dialog, "systemPromptPanel")
            compare(panel.transform[0].yScale, 1)
            tryCompare(dialog, "phase", "open")
            backend.request = ({}); backend.changed()
            tryCompare(dialog, "phase", "hidden")
        }
        function test_waits_for_originating_app() {
            dialog.canPresent = false
            open("confirmation")
            wait(260)
            verify(!dialog.visible)
            compare(backend.replies, 0)
            dialog.canPresent = true
            tryCompare(dialog, "phase", "open")
            compare(dialog.shownRequest.id, "request-1")
            dialog.canPresent = false
            compare(dialog.phase, "closing")
            verify(dialog.visible)
            tryCompare(dialog, "phase", "hidden")
            wait(260)
            verify(!dialog.visible)
            compare(backend.replies, 0)
        }
        function test_app_dimming_tracks_transition() {
            open("confirmation")
            const dimmer = findChild(dialog, "systemPromptDimmer")
            compare(dimmer.width, screen.width)
            compare(dimmer.height, screen.height)
            compare(dimmer.opacity, 0)
            wait(65)
            verify(dimmer.opacity > 0 && dimmer.opacity < dialog.dimOpacity)
            fuzzyCompare(dimmer.opacity, dialog.dimOpacity * dialog.backdropOpacity, 0.001)
            tryCompare(dialog, "phase", "open")
            compare(dimmer.opacity, dialog.dimOpacity)
            backend.request = ({}); backend.changed()
            wait(65)
            verify(dimmer.opacity > 0 && dimmer.opacity < dialog.dimOpacity)
            fuzzyCompare(dimmer.opacity, dialog.dimOpacity * dialog.backdropOpacity, 0.001)
            tryCompare(dialog, "phase", "hidden")
            compare(dimmer.opacity, 0)
        }
        function test_prompt_resizes_with_app() {
            dialog.reduceMotion = true
            open("text_input", {message: "", initialText: "notes.txt"})
            tryCompare(dialog, "phase", "open")
            const panel = findChild(dialog, "systemPromptPanel")
            const title = findChild(dialog, "systemPromptTitle")
            const input = findChild(dialog, "systemPromptInput")
            screen.width = 384; screen.height = 683
            waitForRendering(dialog)
            compare(panel.width, 311)
            compare(title.font.pixelSize, 18)
            compare(input.height, 26)
            compare(panel.x + panel.width / 2, screen.width / 2)
            verify(panel.y >= dialog.safeTop)
            screen.width = 1152; screen.height = 2049
            waitForRendering(dialog)
            compare(panel.width, 933)
            compare(title.font.pixelSize, 54)
            compare(input.height, 78)
            compare(panel.x + panel.width / 2, screen.width / 2)
            const dimmer = findChild(dialog, "systemPromptDimmer")
            compare(dimmer.width, screen.width)
            compare(dimmer.height, screen.height)
        }
        function test_compact_naming_prompt_matches_reference() {
            open("text_input", {title: "Rename file", message: "", initialText: "notes.txt",
                placeholder: "type new file name", acceptLabel: "Rename", minLength: 1})
            tryCompare(dialog, "phase", "open")
            const panel = findChild(dialog, "systemPromptPanel")
            const input = findChild(dialog, "systemPromptInput")
            const title = findChild(dialog, "systemPromptTitle")
            verify(panel.height >= 210 && panel.height <= 235)
            compare(panel.width, 622)
            compare(title.font.weight, Font.ExtraLight)
            compare(input.font.weight, Font.ExtraLight)
            compare(input.placeholderText, "type new file name")
            compare(input.selectedText, "notes.txt")
            compare(findChild(dialog, "systemPromptAccept").text, "Rename")
            mouseClick(findChild(dialog, "systemPromptCancel"))
            compare(backend.result, "cancelled")
            tryCompare(dialog, "phase", "hidden")
        }
        function test_input_validation_and_plain_text() {
            dialog.reduceMotion = true
            open("text_input", {title: "<b>Untrusted</b>", minLength: 2, maxLength: 4, validationPattern: "[A-Z]{2,4}"})
            tryCompare(dialog, "phase", "open")
            compare(findChild(dialog, "systemPromptTitle").textFormat, Text.PlainText)
            const input = findChild(dialog, "systemPromptInput")
            input.text = "a"
            verify(!dialog.inputValid)
            dialog.respond("accepted")
            compare(backend.replies, 0)
            input.text = "ABC"
            verify(dialog.inputValid)
            dialog.respond("accepted")
            compare(backend.text, "ABC")
            tryCompare(dialog, "phase", "hidden")
        }
        function test_loss_and_replacement() {
            open("confirmation")
            wait(40)
            backend.request = ({}); backend.changed()
            compare(dialog.phase, "closing")
            backend.request = {id: "request-2", type: "message", title: "Next", message: ""}; backend.changed()
            tryCompare(dialog, "phase", "open")
            compare(dialog.shownRequest.id, "request-2")
            compare(backend.replies, 0)
            verify(!findChild(dialog, "systemPromptCancel").visible)
            dialog.respond("accepted")
            compare(backend.result, "accepted")
            tryCompare(dialog, "phase", "hidden")
        }
        function test_orientation_and_long_text() {
            screen.width = 900; screen.height = 500
            dialog.reduceMotion = true
            open("text_input", {title: "Long title ".repeat(8), message: "Body ".repeat(300), acceptLabel: "A".repeat(48), cancelLabel: "B".repeat(48)})
            tryCompare(dialog, "phase", "open")
            waitForRendering(dialog)
            const panel = findChild(dialog, "systemPromptPanel")
            verify(panel.y >= dialog.safeTop)
            verify(panel.y + panel.height <= screen.height - dialog.safeBottom)
            const accept = findChild(dialog, "systemPromptAccept")
            const position = accept.mapToItem(panel, 0, 0)
            verify(position.y + accept.height <= panel.height)
            verify(position.x + accept.width <= panel.width)
        }
    }
}
