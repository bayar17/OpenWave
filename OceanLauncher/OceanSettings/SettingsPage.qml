import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root
    objectName: "settingsPage"
    property var backend: null
    property var connectivityBackend: null
    property var timeBackend: null
    property var promptClient: null
    property var permissionClient: null
    property var applicationsBackend: null
    property bool permissionsOpen: false
    property Item wallpaper
    property real renderScale: 1
    property real topInset: 0
    property int currentTab: 1
    property bool searching: false
    readonly property var bluetoothDevices: connectivityBackend
        ? connectivityBackend.bluetoothDevices : []
    readonly property var rememberedBluetoothDevices: bluetoothDevices.filter(device =>
        device.connected || device.paired || device.trusted)
    readonly property var discoveredBluetoothDevices: bluetoothDevices.filter(device =>
        !device.connected && !device.paired && !device.trusted)
    property bool bluetoothInfoOpen: false
    property string selectedBluetoothPath: ""
    readonly property var selectedBluetoothDevice: bluetoothDevices.find(device =>
        device.path === selectedBluetoothPath) || ({})
    readonly property string selectedBluetoothName: selectedBluetoothDevice.alias
        || selectedBluetoothDevice.name || selectedBluetoothDevice.address || ""
    property string bluetoothPromptAction: ""
    property string bluetoothPromptPath: ""
    property bool addNetworkPending: false
    property string wifiPromptAction: ""
    property string wifiPromptSsid: ""
    readonly property bool discovering: connectivityBackend
        ? connectivityBackend.bluetoothDiscovering : false
    property bool bluetoothScanWanted: false
    property bool bluetoothScanRequested: false
    property bool bluetoothScanStarted: false
    property bool bluetoothUserDisabled: false
    property bool wifiScanActive: false
    property bool wifiScanStarted: false
    property bool wifiScanRequested: false
    property bool wifiInfoOpen: false
    property bool cropToolOpen: false
    property bool tabSwitching: false
    property bool tabCapturePending: false
    property int tabCaptureToken: 0
    property real tabIncomingOpacity: 1
    property var tabCapture: null
    property int pendingTab: -1
    property string selectedWifiSsid: ""
    property string generalPromptAction: ""
    property var pendingSearchEntry: null
    property bool wifiPasswordVisible: false
    readonly property var wifiInfo: connectivityBackend && connectivityBackend.savedWifiInfo
                                    ? connectivityBackend.savedWifiInfo : ({})
    property int wifiScanDurationMs: 30000
    property int wifiScanIntervalMs: 12000
    property int wifiRefreshIntervalMs: 1500
    readonly property bool largeText: backend ? backend.largeText : false
    readonly property string deviceName: backend ? backend.deviceName : "KB Phone"
    readonly property var tabs: ["wifi", "bluetooth", "general", "accessibility", "wallpaper", "apps"]
    readonly property var searchEntries: [
        {title: "Wi-Fi", detail: "Wireless networks", tab: 0, key: "wifi"},
        {title: "Bluetooth", detail: "Nearby devices", tab: 1, key: "bluetooth"},
        {title: "Device name", detail: deviceName, tab: 2, key: "name"},
        {title: "Time zone", detail: timeBackend ? timeBackend.timezone : "", tab: 2, key: "timezone"},
        {title: qsTr("About"), detail: qsTr("Operating system, architecture, kernel and processors"), tab: 2, key: "about"},
        {title: qsTr("Storage"), detail: qsTr("Available space and file permissions"), tab: 2, key: "storage"},
        {title: qsTr("Locale & Languages"), detail: qsTr("Time zone"), tab: 2, key: "locale"},
        {title: qsTr("Power options"), detail: qsTr("Restart or shut down"), tab: 2, key: "power"},
        {title: qsTr("Permissions"), detail: qsTr("Application access to files and folders"), tab: 2, key: "permissions"},
        {title: "Reduce motion", detail: "Launcher and drawer animations", tab: 3, key: "motion"},
        {title: "Larger text", detail: "Increase text size in Settings", tab: 3, key: "text"},
        {title: "Wallpaper", detail: "Choose and crop a background", tab: 4, key: "wallpaper"},
        {title: qsTr("Apps"), detail: qsTr("Installed apps and application permissions"), tab: 5, key: "apps"}
    ]
    readonly property var searchResults: searchEntries.filter(entry =>
        (entry.title + " " + entry.detail).toLowerCase().includes(searchField.text.trim().toLowerCase()))
    signal closed()
    onCurrentTabChanged: {
        if (currentTab !== 0 && wifiPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
        if (currentTab !== 2) permissionsOpen = false
        if (currentTab !== 2 && generalPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
        if (currentTab !== 0 && addNetworkPending && promptClient && promptClient.busy) promptClient.cancel()
        if (currentTab !== 1) closeBluetoothInfo()
        if (currentTab !== 0)
            closeSavedWifiInfo()
        if (currentTab === 0)
            maybeStartWifiScan()
        else
            stopWifiScan(true)
        if (currentTab === 1) {
            bluetoothScanStarted = false
            maybeStartBluetoothDiscovery()
        } else
            stopBluetoothDiscovery()
    }
    onVisibleChanged: {
        if (visible)
            {
                bluetoothScanStarted = false
                maybeStartWifiScan()
                maybeStartBluetoothDiscovery()
            }
        else {
            if (wifiPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
            if (generalPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
            if (addNetworkPending && promptClient && promptClient.busy) promptClient.cancel()
            closeBluetoothInfo()
            stopWifiScan(true)
            stopBluetoothDiscovery()
            closeSavedWifiInfo()
            resetTabTransition()
        }
    }
    onSearchingChanged: {
        if (searching) {
            if (generalPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
            if (wifiPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
            closeBluetoothInfo()
            stopWifiScan(true)
            stopBluetoothDiscovery()
            closeSavedWifiInfo()
        } else {
            maybeStartWifiScan()
            maybeStartBluetoothDiscovery()
        }
    }
    onConnectivityBackendChanged: {
        maybeStartWifiScan()
        maybeStartBluetoothDiscovery()
    }
    Component.onCompleted: maybeStartBluetoothDiscovery()
    Timer {
        id: bluetoothScanTimeout
        interval: 30000
        onTriggered: root.stopBluetoothDiscovery()
    }
    Timer {
        interval: 2000
        repeat: true
        running: root.visible && root.currentTab === 1 && !root.searching
        onTriggered: if (root.connectivityBackend) root.connectivityBackend.refresh()
    }
    Timer {
        id: wifiScanTimeout
        interval: root.wifiScanDurationMs
        onTriggered: root.stopWifiScan(false)
    }
    Timer {
        id: wifiScanRepeat
        interval: root.wifiScanIntervalMs
        repeat: true
        running: root.wifiScanActive
        onTriggered: root.requestWifiScanIfReady()
    }
    Timer {
        interval: root.wifiRefreshIntervalMs
        repeat: true
        running: root.visible && root.currentTab === 0 && !root.searching
                 && root.connectivityBackend
        onTriggered: root.connectivityBackend.refresh()
    }

    FontLoader { source: "assets/fonts/Inter-ExtraLight.otf" }
    FontLoader { source: "assets/fonts/Inter-SemiBold.otf" }

    component ActionButton: Button {
        id: action
        font.family: "Inter"
        font.pixelSize: 26
        implicitWidth: contentItem.implicitWidth + 32
        implicitHeight: 56
        contentItem: Text {
            text: action.text
            font: action.font
            color: action.enabled ? "white" : "#8a979f"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: action.down ? "#087da0" : "#263740"
            radius: 6
            border.color: action.enabled ? "#00b7f0" : "#657781"
            border.width: action.visualFocus ? 2 : 1
        }
    }

    function selectTab(index, animated) {
        const next = Math.max(0, Math.min(tabs.length - 1, index))
        if (animated && next !== currentTab && visible
                && !(backend && backend.reduceMotion) && paneViewport.width > 0) {
            pendingTab = next
            if (tabCapturePending)
                return
            tabCrossfade.stop()
            tabIncomingOpacity = 1
            outgoingTabFrame.opacity = 0
            tabCapture = null
            tabCapturePending = true
            const captureToken = ++tabCaptureToken
            if (paneViewport.grabToImage(function(result) {
                if (captureToken !== tabCaptureToken)
                    return
                tabCapturePending = false
                const target = pendingTab
                pendingTab = -1
                if (target < 0 || !result) {
                    if (target >= 0) commitTab(target)
                    return
                }
                tabCapture = result
                outgoingTabFrame.opacity = 1
                tabIncomingOpacity = 0
                tabSwitching = true
                commitTab(target)
                tabCrossfade.start()
            }))
                return
            tabCapturePending = false
            pendingTab = -1
        }
        ++tabCaptureToken
        tabCapturePending = false
        tabCrossfade.stop()
        pendingTab = -1
        outgoingTabFrame.opacity = 0
        tabCapture = null
        tabIncomingOpacity = 1
        tabSwitching = false
        commitTab(next)
    }

    function resetTabTransition() {
        pendingSearchEntry = null
        ++tabCaptureToken
        tabCapturePending = false
        pendingTab = -1
        tabCrossfade.stop()
        outgoingTabFrame.opacity = 0
        tabCapture = null
        tabIncomingOpacity = 1
        tabSwitching = false
    }

    function commitTab(index) {
        tabStrip.cancelFlick()
        currentTab = index
        searching = false
        searchField.text = ""
        const item = tabRepeater.itemAt(currentTab)
        if (item) {
            if (item.x < tabStrip.contentX)
                tabStrip.contentX = item.x
            else if (item.x + item.width > tabStrip.contentX + tabStrip.width)
                tabStrip.contentX = item.x + item.width - tabStrip.width
        }
        root.forceActiveFocus()
        const entry = pendingSearchEntry
        pendingSearchEntry = null
        if (entry && entry.tab === index && index === 2) {
            const category = entry.key === "name" ? "about"
                : entry.key === "timezone" ? "locale"
                : entry.key === "permissions" ? "storage" : entry.key
            generalPage.openCategory(category)
            if (entry.key === "name") editName()
            if (entry.key === "timezone") editTimezone()
            if (entry.key === "permissions") {
                permissionsOpen = true
                if (permissionClient) permissionClient.refreshApplications()
            }
        }
    }

    function openSearchResult(entry) {
        pendingSearchEntry = entry
        selectTab(entry.tab, true)
    }

    ParallelAnimation {
        id: tabCrossfade
        NumberAnimation {
            target: root; property: "tabIncomingOpacity"
            from: 0; to: 1; duration: 240; easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: outgoingTabFrame; property: "opacity"
            from: 1; to: 0; duration: 240; easing.type: Easing.InOutQuad
        }
        onFinished: {
            root.tabCapture = null
            root.tabSwitching = false
        }
    }

    function back() {
        if (currentTab === 5 && appsPage.back()) return true
        if (permissionsOpen) { permissionsOpen = false; return true }
        if (wifiPromptAction.length && promptClient && promptClient.busy) { promptClient.cancel(); return true }
        if (generalPromptAction.length && promptClient && promptClient.busy) { promptClient.cancel(); return true }
        if (addNetworkPending && promptClient && promptClient.busy) { promptClient.cancel(); return true }
        if (bluetoothPromptAction.length && promptClient && promptClient.busy) {
            promptClient.cancel(); return true
        }
        if (wallpaperCropDialog.visible) { wallpaperCropDialog.beginClose(); return true }
        if (bluetoothInfoOpen) { closeBluetoothInfo(); return true }
        if (wifiInfoOpen) { closeSavedWifiInfo(); return true }
        if (searching) { selectTab(currentTab); return true }
        if (currentTab === 2 && generalPage.back()) return true
        return false
    }

    function openBluetoothInfo(device) {
        if (!connectivityBackend || connectivityBackend.busy || !device.path
                || !(device.connected || device.paired || device.trusted)) return
        selectedBluetoothPath = device.path
        bluetoothInfoOpen = true
    }
    function closeBluetoothInfo() {
        bluetoothInfoOpen = false
        if (bluetoothPromptAction.length && promptClient && promptClient.busy) promptClient.cancel()
        bluetoothInfoClearTimer.restart()
    }
    Timer {
        id: bluetoothInfoClearTimer
        interval: root.backend && root.backend.reduceMotion ? 0 : 310
        onTriggered: if (!root.bluetoothInfoOpen) root.selectedBluetoothPath = ""
    }
    onSelectedBluetoothDeviceChanged: {
        if (bluetoothInfoOpen && !selectedBluetoothDevice.path) closeBluetoothInfo()
    }
    function activateBluetoothDevice(device) {
        if (!connectivityBackend || connectivityBackend.busy) return
        if (device.connected) openBluetoothInfo(device)
        else if (device.paired || device.trusted) connectivityBackend.connectBluetoothDevice(device.path)
        else connectivityBackend.pairBluetoothDevice(device.path)
    }
    function requestBluetoothPrompt(action) {
        if (!connectivityBackend || connectivityBackend.busy || !selectedBluetoothDevice.path
                || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        bluetoothPromptPath = selectedBluetoothPath
        bluetoothPromptAction = action
        if (action === "nickname")
            promptClient.textInput(qsTr("Edit nickname"), "",
                {initialText: selectedBluetoothName, placeholder: qsTr("type device nickname"),
                 acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel"),
                 minLength: 1, maxLength: 248, validationPattern: "[^\\x00-\\x1f\\x7f]+"})
        else
            promptClient.confirmation(qsTr("Forget device?"),
                qsTr("Forget “%1”? You will need to pair it again to reconnect.").arg(selectedBluetoothName),
                {acceptLabel: qsTr("Forget"), cancelLabel: qsTr("Cancel")})
    }
    Connections {
        target: root.promptClient
        function onErrorOccurred(message) { root.showMessage(message) }
        function onFinished(requestId, result, text) {
            const action = root.bluetoothPromptAction
            const path = root.bluetoothPromptPath
            root.bluetoothPromptAction = ""
            root.bluetoothPromptPath = ""
            if (result !== "accepted" || !action.length || !root.bluetoothInfoOpen
                    || root.currentTab !== 1 || path !== root.selectedBluetoothPath
                    || !root.selectedBluetoothDevice.path || !root.connectivityBackend) return
            if (action === "nickname") root.connectivityBackend.renameBluetoothDevice(path, text)
            else if (action === "forget") {
                root.connectivityBackend.forgetBluetoothDevice(path)
                root.closeBluetoothInfo()
            }
        }
    }

    function openSavedWifiInfo(network) {
        if (!network.saved || !connectivityBackend)
            return
        selectedWifiSsid = network.ssid
        wifiInfoPane.contentY = 0
        wifiPasswordVisible = false
        wifiInfoOpen = true
        connectivityBackend.loadSavedWifiInfo(network.ssid)
    }

    function closeSavedWifiInfo() {
        if (!wifiInfoOpen)
            return
        wifiInfoOpen = false
        if ((wifiPromptAction === "forget" || wifiPromptAction === "ipConfig")
                && promptClient && promptClient.busy) promptClient.cancel()
        wifiPasswordVisible = false
        wifiInfoClearTimer.restart()
    }

    function requestWifiPassword(network) {
        if (!connectivityBackend || connectivityBackend.busy || !network.ssid
                || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        wifiPromptSsid = network.ssid
        wifiPromptAction = "connect"
        promptClient.passwordInput(network.ssid, network.security || "", {
            placeholder: qsTr("type password"), minLength: 8, maxLength: 63,
            acceptLabel: qsTr("Connect"), cancelLabel: qsTr("Cancel")
        })
    }

    function requestForgetWifi() {
        if (!connectivityBackend || connectivityBackend.busy || !wifiInfoOpen
                || connectivityBackend.savedWifiInfoLoading || !selectedWifiSsid
                || wifiInfo.ssid !== selectedWifiSsid || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        wifiPromptSsid = selectedWifiSsid
        wifiPromptAction = "forget"
        promptClient.confirmation(qsTr("Forget network?"),
            qsTr("Are you sure you want to forget “%1”? Its saved password and settings will be removed.").arg(selectedWifiSsid),
            {acceptLabel: qsTr("Forget"), cancelLabel: qsTr("Cancel")})
    }

    Connections {
        target: root.promptClient
        function onFinished(requestId, result, text) {
            const action = root.wifiPromptAction
            const ssid = root.wifiPromptSsid
            root.wifiPromptAction = ""
            root.wifiPromptSsid = ""
            if (result !== "accepted" || !action || !root.visible || root.searching
                    || root.currentTab !== 0 || !root.connectivityBackend
                    || root.connectivityBackend.busy) return
            if (action === "connect" && root.connectivityBackend.wifiEnabled)
                root.connectivityBackend.connectWifi(ssid, text)
            else if (action === "forget" && root.wifiInfoOpen && ssid === root.selectedWifiSsid
                    && root.wifiInfo.ssid === ssid) {
                root.connectivityBackend.forgetWifi(ssid)
                root.closeSavedWifiInfo()
            }
        }
        function onFieldsFinished(requestId, result, values) {
            if (root.wifiPromptAction !== "ipConfig") return
            const ssid = root.wifiPromptSsid
            root.wifiPromptAction = ""
            root.wifiPromptSsid = ""
            if (result !== "accepted" || !root.visible || root.searching
                    || root.currentTab !== 0 || !root.wifiInfoOpen
                    || ssid !== root.selectedWifiSsid || root.wifiInfo.ssid !== ssid
                    || !root.connectivityBackend || root.connectivityBackend.busy
                    || root.connectivityBackend.savedWifiInfoLoading) return
            root.saveIpConfig(ssid, values)
        }
    }

    Timer {
        id: wifiInfoClearTimer
        interval: root.backend && root.backend.reduceMotion ? 0 : 310
        onTriggered: {
            if (!root.wifiInfoOpen && root.connectivityBackend)
                root.connectivityBackend.clearSavedWifiInfo()
            if (!root.wifiInfoOpen)
                root.selectedWifiSsid = ""
        }
    }

    function editName() {
        if (!backend || generalPromptAction.length || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        generalPromptAction = "name"
        promptClient.textInput(qsTr("Device name"), "", {
            initialText: deviceName, placeholder: qsTr("Type device name"),
            minLength: 1, maxLength: 64, validationPattern: "[\\s\\S]*\\S[\\s\\S]*",
            acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel")})
    }

    function editTimezone() {
        if (!timeBackend || !timeBackend.serviceAvailable || timeBackend.busy
                || generalPromptAction.length || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        if (!timeBackend.zones.length) { timeBackend.refresh(); showMessage(qsTr("Time zones are still loading. Try again.")); return }
        const choices = ({})
        for (const zone of timeBackend.zones) choices[zone] = zone.split("_").join(" ")
        generalPromptAction = "timezone"
        promptClient.selection(qsTr("Time zone"), "", {
            choices: choices, initialText: choices[timeBackend.timezone] ? timeBackend.timezone : "",
            placeholder: qsTr("Search city or region"), acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel")})
    }
    function requestPowerAction(action) {
        if (!["restart", "shutdown"].includes(action) || !backend || !backend.powerActionsAvailable
                || generalPromptAction.length || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        generalPromptAction = action
        promptClient.confirmation(action === "restart" ? qsTr("Restart OpenWave?") : qsTr("Shut down OpenWave?"),
            qsTr("Open applications will close. Save your work before continuing."), {
                acceptLabel: action === "restart" ? qsTr("Restart") : qsTr("Shut down"), cancelLabel: qsTr("Cancel")
            })
    }
    Connections {
        target: root.promptClient
        function onFinished(requestId, result, text) {
            const action = root.generalPromptAction
            root.generalPromptAction = ""
            if (result !== "accepted" || !action.length || !root.visible || root.currentTab !== 2) return
            if (action === "name" && root.backend && root.backend.saveDeviceName(text)) {
                if (root.connectivityBackend) root.connectivityBackend.setDeviceName(root.backend.deviceName)
            } else if (action === "timezone" && root.timeBackend && root.timeBackend.serviceAvailable
                    && root.timeBackend.zones.indexOf(text) >= 0) root.timeBackend.setTimezone(text)
            else if (["restart", "shutdown"].includes(action) && root.backend) root.backend.requestPowerAction(action)
        }
    }

    function openAddNetwork() {
        if (!connectivityBackend || connectivityBackend.busy || addNetworkPending
                || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        addNetworkPending = true
        promptClient.form(qsTr("Add Network"), "", {
            fieldOrder: "ssid,security,password",
            fields: {
                ssid: {placeholder: qsTr("Network name (SSID)"), minLength: 1, maxLength: 32, maxBytes: 32},
                security: {label: qsTr("Security"), initialText: "wpa",
                    choices: {wpa: qsTr("WPA Personal"), open: qsTr("Open")}},
                password: {placeholder: qsTr("Password"), secret: true, minLength: 8, maxLength: 63,
                    activeWhenField: "security", activeWhenValue: "wpa"}
            }, acceptLabel: qsTr("Connect"), cancelLabel: qsTr("Cancel")})
    }
    Connections {
        target: root.promptClient
        function onFieldsFinished(requestId, result, values) {
            if (!root.addNetworkPending) return
            root.addNetworkPending = false
            if (result !== "accepted" || !root.visible || !root.connectivityBackend) return
            const ssid = (values.ssid || "").trim()
            if (!ssid.length) { root.showMessage(qsTr("Enter a network name.")); return }
            const secured = values.security === "wpa"
            root.connectivityBackend.addWifiNetwork(ssid, secured, secured ? values.password : "")
        }
    }

    function validIpv4(text) {
        const parts = text.trim().split(".")
        if (parts.length !== 4)
            return false
        for (const part of parts) {
            if (!/^\d{1,3}$/.test(part) || Number(part) > 255
                    || (part.length > 1 && part[0] === "0"))
                return false
        }
        return true
    }

    function validIpAddress(text) {
        if (validIpv4(text))
            return true
        if (!text.includes(":"))
            return false
        const halves = text.split("::")
        if (halves.length > 2)
            return false
        const groups = []
        for (const half of halves) {
            if (!half.length)
                continue
            for (const group of half.split(":")) {
                if (!group.length)
                    return false
                groups.push(group)
            }
        }
        let count = 0
        for (let i = 0; i < groups.length; ++i) {
            if (groups[i].includes(".")) {
                if (i !== groups.length - 1 || !validIpv4(groups[i]))
                    return false
                count += 2
            } else {
                if (!/^[0-9a-fA-F]{1,4}$/.test(groups[i]))
                    return false
                count++
            }
        }
        return halves.length === 2 ? count < 8 : count === 8
    }

    function openIpConfig(values, error) {
        if (!connectivityBackend || connectivityBackend.busy || !wifiInfoOpen
                || connectivityBackend.savedWifiInfoLoading || !selectedWifiSsid
                || wifiInfo.ssid !== selectedWifiSsid || currentTab !== 0 || searching
                || !visible || (promptClient && promptClient.busy)) return
        if (!promptClient) { showMessage(qsTr("The prompt service is unavailable.")); return }
        const initial = values || {
            ipMode: wifiInfo.ipManual ? "manual" : "automatic",
            address: wifiInfo.configuredAddress || "",
            prefix: String(wifiInfo.configuredPrefix === undefined ? 24 : wifiInfo.configuredPrefix),
            gateway: wifiInfo.configuredGateway || "",
            dnsMode: wifiInfo.dnsManual ? "manual" : "automatic",
            dnsServers: (wifiInfo.configuredDns || []).join(", ")
        }
        wifiPromptSsid = selectedWifiSsid
        wifiPromptAction = "ipConfig"
        promptClient.form(qsTr("IP and DNS settings"),
            (error ? error + "\n\n" : "") + selectedWifiSsid + "\n"
                + qsTr("Changing the active network may briefly interrupt Wi-Fi."), {
            fieldOrder: "ipMode,address,prefix,gateway,dnsMode,dnsServers",
            fields: {
                ipMode: {label: qsTr("IP configuration"), initialText: initial.ipMode,
                    choices: {automatic: qsTr("Automatic (DHCP)"), manual: qsTr("Manual")}},
                address: {label: qsTr("IPv4 address"), placeholder: qsTr("e.g. 192.168.1.50"),
                    initialText: initial.address, minLength: 1, maxLength: 15,
                    activeWhenField: "ipMode", activeWhenValue: "manual"},
                prefix: {label: qsTr("Subnet prefix (0–32)"), placeholder: qsTr("e.g. 24"),
                    initialText: initial.prefix, minLength: 1, maxLength: 2,
                    activeWhenField: "ipMode", activeWhenValue: "manual"},
                gateway: {label: qsTr("Gateway (optional)"), placeholder: qsTr("e.g. 192.168.1.1"),
                    initialText: initial.gateway, maxLength: 15,
                    activeWhenField: "ipMode", activeWhenValue: "manual"},
                dnsMode: {label: qsTr("DNS configuration"), initialText: initial.dnsMode,
                    choices: {automatic: qsTr("Automatic"), manual: qsTr("Manual")}},
                dnsServers: {label: qsTr("DNS servers"), placeholder: qsTr("IPv4 or IPv6 addresses, separated by commas"),
                    initialText: initial.dnsServers, minLength: 1, maxLength: 512,
                    activeWhenField: "dnsMode", activeWhenValue: "manual"}
            }, acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel")})
    }

    function saveIpConfig(ssid, values) {
        const manualIp = values.ipMode === "manual"
        const manualDns = values.dnsMode === "manual"
        if (!["automatic", "manual"].includes(values.ipMode)
                || !["automatic", "manual"].includes(values.dnsMode)) return
        const address = manualIp ? (values.address || "").trim() : ""
        const prefixText = (values.prefix || "").trim()
        const prefix = manualIp ? Number(prefixText) : 0
        const gateway = manualIp ? (values.gateway || "").trim() : ""
        const dns = manualDns ? (values.dnsServers || "").trim().split(/[\s,;]+/).filter(value => value.length) : []
        let error = ""
        if (manualIp && (!validIpv4(address) || !/^\d{1,2}$/.test(prefixText)
                || prefix < 0 || prefix > 32 || (gateway && !validIpv4(gateway))))
            error = qsTr("Enter a valid IPv4 address, prefix (0–32), and gateway.")
        else if (manualDns && (!dns.length || dns.some(value => !validIpAddress(value))))
            error = qsTr("Enter at least one valid IPv4 or IPv6 DNS server.")
        if (error) {
            openIpConfig(values, error)
            return
        }
        connectivityBackend.saveSavedWifiIpConfig(ssid, manualIp,
                                                   address, prefix, gateway, manualDns, dns)
    }

    function showMessage(message) {
        notice.text = message
        noticeTimer.restart()
    }

    function stopWifiScan(reset) {
        wifiScanActive = false
        wifiScanRequested = false
        wifiScanTimeout.stop()
        if (reset)
            wifiScanStarted = false
    }

    function maybeStartWifiScan() {
        if (wifiScanActive || wifiScanStarted || !visible || searching || currentTab !== 0
                || !connectivityBackend || !connectivityBackend.serviceAvailable
                || !connectivityBackend.wifiDeviceAvailable || !connectivityBackend.wifiEnabled)
            return
        startWifiScan()
    }

    function startWifiScan() {
        if (!connectivityBackend || !connectivityBackend.serviceAvailable
                || !connectivityBackend.wifiDeviceAvailable || !connectivityBackend.wifiEnabled)
            return
        wifiScanStarted = true
        wifiScanActive = true
        wifiScanRequested = false
        wifiScanTimeout.restart()
        wifiScanRepeat.restart()
        requestWifiScanIfReady()
    }

    function requestWifiScanIfReady() {
        if (!wifiScanActive || !connectivityBackend)
            return
        if (connectivityBackend.wifiScanAvailable && !connectivityBackend.busy) {
            wifiScanRequested = true
            connectivityBackend.requestScan()
        } else if (!connectivityBackend.wifiScanAvailable) {
            wifiScanRequested = false
            connectivityBackend.refresh()
        }
    }

    function refreshRadio() {
        if (currentTab === 0 && connectivityBackend) {
            if (!connectivityBackend.wifiEnabled) {
                connectivityBackend.setWifiEnabled(true)
                return
            }
            startWifiScan()
            return
        }
        if (currentTab === 1 && connectivityBackend) {
            if (!connectivityBackend.bluetoothAdapterAvailable) {
                connectivityBackend.refresh()
                return
            }
            if (connectivityBackend.bluetoothDiscovering) {
                bluetoothScanTimeout.restart()
                connectivityBackend.refresh()
            } else {
                bluetoothScanStarted = false
                bluetoothScanWanted = false
                maybeStartBluetoothDiscovery()
            }
            return
        }
        if (backend)
            backend.refreshRadios()
        showMessage(currentTab === 1
            ? (backend ? backend.bluetoothStatus : "Bluetooth is unavailable.")
            : (backend ? backend.wifiStatus : "Wi-Fi is unavailable."))
    }

    function maybeStartBluetoothDiscovery() {
        if (!visible || searching || currentTab !== 1 || bluetoothScanWanted
                || bluetoothScanStarted || bluetoothUserDisabled
                || !connectivityBackend || !connectivityBackend.serviceAvailable
                || !connectivityBackend.bluetoothAdapterAvailable)
            return
        bluetoothScanWanted = true
        bluetoothScanStarted = true
        bluetoothScanRequested = false
        bluetoothScanTimeout.restart()
        if (!connectivityBackend.bluetoothEnabled)
            connectivityBackend.setBluetoothEnabled(true)
        else if (!connectivityBackend.bluetoothDiscovering) {
            bluetoothScanRequested = true
            connectivityBackend.startBluetoothDiscovery()
        }
    }

    function stopBluetoothDiscovery() {
        const shouldStop = bluetoothScanRequested ||
                           (connectivityBackend && connectivityBackend.bluetoothDiscovering)
        bluetoothScanTimeout.stop()
        bluetoothScanWanted = false
        bluetoothScanRequested = false
        if (connectivityBackend && shouldStop)
            connectivityBackend.stopBluetoothDiscovery()
    }

    Connections {
        target: root.backend
        function onErrorOccurred(message) { root.showMessage(message) }
    }
    Connections {
        target: Wallpaper
        function onErrorOccurred(message) { root.showMessage(message) }
        function onSaved() {
            wallpaperCropDialog.beginClose()
            root.showMessage("Wallpaper updated.")
        }
    }
    Connections {
        target: OpenWavePortals
        function onCompleted(grants) {
            if (!grants.length) { root.showMessage("No wallpaper was selected."); return }
            const url = grants[0].temporaryCopy()
            if (!url.toString().length) { root.showMessage("The selected image could not be opened."); return }
            wallpaperCropDialog.openImage(url)
        }
        function onErrorOccurred(message) { root.showMessage(message) }
    }
    Connections {
        target: root.timeBackend
        function onErrorOccurred(message) { root.showMessage(message) }
    }
    Connections {
        target: root.connectivityBackend
        function onErrorOccurred(message) { root.showMessage(message) }
        function onNetworkAdded() {
            root.showMessage("Connecting to network…")
        }
        function onConfigurationSaved(applied) {
            root.showMessage(applied ? "Network settings applied." : "Saved. Reconnect to apply.")
        }
        function onScanDeferred() { root.wifiScanRequested = false }
        function onChanged() {
            if (root.bluetoothInfoOpen && (!root.connectivityBackend.serviceAvailable
                    || !root.connectivityBackend.bluetoothAdapterAvailable
                    || !root.connectivityBackend.bluetoothEnabled)) root.closeBluetoothInfo()
            if (root.currentTab === 1 && root.visible && !root.searching) {
                root.maybeStartBluetoothDiscovery()
                if (root.bluetoothScanWanted && !root.bluetoothScanRequested
                        && root.connectivityBackend.bluetoothEnabled
                        && !root.connectivityBackend.busy
                        && !root.connectivityBackend.bluetoothDiscovering) {
                    root.bluetoothScanRequested = true
                    root.connectivityBackend.startBluetoothDiscovery()
                }
            }
            if (!root.connectivityBackend || !root.connectivityBackend.serviceAvailable
                    || !root.connectivityBackend.wifiDeviceAvailable
                    || !root.connectivityBackend.wifiEnabled)
                root.stopWifiScan(true)
            else {
                root.maybeStartWifiScan()
                if (root.wifiScanActive && root.connectivityBackend.wifiScanAvailable
                        && !root.wifiScanRequested && !root.connectivityBackend.busy)
                    root.requestWifiScanIfReady()
            }
        }
    }

    FrostedBackground { anchors.fill: parent; source: root.wallpaper; visible: root.wallpaper && root.wallpaper.status === Image.Ready }
    Item {
        id: controls
        y: root.topInset
        width: parent.width
        height: parent.height - y

        MouseArea { anchors.fill: parent }

        AbstractButton {
            id: searchButton
            objectName: "settingsSearch"
            x: 32; y: 41; width: 70; height: 80
            Accessible.name: root.searching ? "Close search" : "Search settings"
            onClicked: {
                root.searching = !root.searching
                searchField.text = ""
                if (root.searching) searchField.forceActiveFocus()
            }
            contentItem: Item {
                SettingsSymbol { anchors.centerIn: parent; color: root.searching ? "#00b7f0" : "white" }
            }
        }

        Flickable {
            id: tabStrip
            objectName: "settingsTabs"
            visible: !root.searching
            x: 116; y: 43; width: parent.width - x; height: 80
            contentWidth: tabRow.width + 32
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            Row {
                id: tabRow
                spacing: 26
                Repeater {
                    id: tabRepeater
                    model: root.tabs
                    AbstractButton {
                        id: tabButton
                        required property string modelData
                        required property int index
                        objectName: "settingsTab_" + modelData
                        width: tabText.implicitWidth + 12
                        height: 70
                        Accessible.name: modelData
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: root.currentTab === index
                        onClicked: root.selectTab(index, true)
                        contentItem: Text {
                            id: tabText
                            text: tabButton.modelData
                            font.family: "Inter"
                            font.weight: Font.ExtraLight
                            font.pixelSize: 46
                            color: root.currentTab === tabButton.index ? "#00b7f0" : "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Item {
                            Rectangle {
                                objectName: "settingsTabLine_" + tabButton.modelData
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 4
                                color: "#00b7f0"
                                opacity: root.currentTab === tabButton.index ? 1 : 0
                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: root.backend && root.backend.reduceMotion ? 0 : 240
                                        easing.type: Easing.InOutQuad
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        TextField {
            id: searchField
            objectName: "settingsSearchField"
            visible: root.searching
            x: 116; y: 48; width: parent.width - x - 42; height: 70
            placeholderText: "Search settings"
            color: "white"
            placeholderTextColor: "#b7c0c5"
            selectionColor: "#008ec0"
            font.family: "Inter"
            font.pixelSize: 34
            background: Item {}
            onAccepted: {
                if (root.searchResults.length === 1)
                    root.openSearchResult(root.searchResults[0])
            }
        }

        Rectangle { x: 38; y: 140; width: parent.width - 82; height: 2; color: "#d4dadd" }

        Item {
            id: paneViewport
            objectName: "settingsWifiPaneViewport"
            x: 48; y: 164; width: parent.width - 96; height: parent.height - y - 48
            clip: true
        Flickable {
            id: content
            objectName: "settingsMainPane"
            opacity: root.tabIncomingOpacity
            visible: (root.currentTab !== 5 && root.currentTab !== 2) || root.searching
            x: root.wifiInfoOpen || root.bluetoothInfoOpen || root.permissionsOpen ? -paneViewport.width : 0
            enabled: !root.wifiInfoOpen && !root.bluetoothInfoOpen && !root.permissionsOpen && !contentSlide.running
            y: 0; width: paneViewport.width
            height: paneViewport.height - (root.currentTab === 0 && !root.searching ? 106 : 0)
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Behavior on x {
                NumberAnimation {
                    id: contentSlide
                    duration: root.tabSwitching || (root.backend && root.backend.reduceMotion) ? 0 : 300
                    easing.type: Easing.InOutCubic
                }
            }
            onVisibleChanged: contentY = 0
            Connections { target: root; function onCurrentTabChanged() { content.contentY = 0 } }
            Column {
                id: body
                width: parent.width
                spacing: 0

                Column {
                    visible: root.searching
                    width: parent.width
                    Repeater {
                        model: root.searchResults
                        SettingsRow {
                            required property var modelData
                            heading: modelData.title
                            detail: modelData.detail
                            largeText: root.largeText
                            onClicked: root.openSearchResult(modelData)
                        }
                    }
                    Text {
                        visible: root.searchResults.length === 0
                        text: "No matching settings"
                        color: "#d4dadd"
                        font.family: "Inter"
                        font.pixelSize: 30
                    }
                }

                Column {
                    visible: !root.searching && (root.currentTab === 0 || root.currentTab === 1)
                    width: parent.width
                    Item {
                        width: parent.width
                        height: 100
                        Switch {
                            id: radioHeading
                            objectName: "settingsWifiHeading"
                            padding: 0
                            indicator: Item {}
                            checked: root.connectivityBackend
                                && (root.currentTab === 0 ? root.connectivityBackend.wifiEnabled
                                                          : root.connectivityBackend.bluetoothEnabled)
                            Accessible.role: Accessible.CheckBox
                            Accessible.checked: checked
                            x: root.currentTab === 0 ? 26 : 0
                            y: 0
                            width: Math.min(headingText.implicitWidth,
                                            root.currentTab === 0 ? 200 : parent.width - 176) + 120
                            height: 56
                            enabled: root.connectivityBackend
                                     && root.connectivityBackend.serviceAvailable
                                     && (root.currentTab === 0 || root.connectivityBackend.bluetoothAdapterAvailable)
                            Accessible.name: root.currentTab === 0
                                ? (root.connectivityBackend && root.connectivityBackend.wifiEnabled
                                   ? "Turn off Wi-Fi" : "Turn on Wi-Fi")
                                : (root.connectivityBackend && root.connectivityBackend.bluetoothEnabled
                                   ? "Turn off Bluetooth" : "Turn on Bluetooth")
                            onToggled: {
                                if (root.currentTab === 0)
                                    root.connectivityBackend.setWifiEnabled(checked)
                                else {
                                    const enable = checked
                                    root.bluetoothUserDisabled = !enable
                                    if (!enable) root.stopBluetoothDiscovery()
                                    else {
                                        root.bluetoothScanStarted = false
                                        root.bluetoothScanWanted = false
                                    }
                                    root.connectivityBackend.setBluetoothEnabled(enable)
                                }
                            }
                            contentItem: Item {
                                Text {
                                    id: headingText
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 120
                                    text: root.currentTab === 0 ? "Wi-Fi" : "Visible as " + root.deviceName
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font.family: "Inter"
                                    font.pixelSize: root.largeText ? 42 : 36
                                    font.weight: Font.DemiBold
                                    color: "white"
                                }
                                Rectangle {
                                    id: radioSwitch
                                    objectName: "settingsRadioSwitch"
                                    x: headingText.width + 20
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 100; height: 50; radius: 25
                                    readonly property bool on: root.connectivityBackend
                                        && (root.currentTab === 0 ? root.connectivityBackend.wifiEnabled
                                                                  : root.connectivityBackend.bluetoothEnabled)
                                    color: radioSwitch.on
                                           ? (root.currentTab === 0 ? "#00d9e7" : "#0759fa")
                                           : "#777e83"
                                    Rectangle {
                                        x: radioSwitch.on ? 50 : 0
                                        width: 50; height: 50; radius: 25
                                        color: "white"
                                        Behavior on x {
                                            NumberAnimation {
                                                duration: root.backend && root.backend.reduceMotion ? 0 : 180
                                                easing.type: Easing.InOutQuad
                                            }
                                        }
                                    }
                                }
                            }
                            background: Item {}
                        }
                        AbstractButton {
                            objectName: "settingsRefresh"
                            anchors.right: parent.right
                            y: -6; width: 56; height: 56
                            Accessible.name: root.currentTab === 0
                                ? (root.connectivityBackend && !root.connectivityBackend.wifiEnabled
                                   ? "Turn on Wi-Fi" : "Scan for Wi-Fi networks")
                                : "Search for Bluetooth devices"
                            onClicked: root.refreshRadio()
                            contentItem: Item {
                                SettingsSymbol {
                                    objectName: "settingsRefreshSymbol"
                                    anchors.centerIn: parent
                                    width: 44; height: 44
                                    kind: (root.currentTab === 0 ? root.wifiScanActive : root.discovering)
                                          ? "scan" : "refresh"
                                    color: "#cbd0d3"
                                    NumberAnimation on rotation {
                                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite
                                        running: (root.currentTab === 0 ? root.wifiScanActive : root.discovering)
                                                 && root.visible && !(root.backend && root.backend.reduceMotion)
                                    }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: root.currentTab === 0 && root.connectivityBackend
                               && root.connectivityBackend.wifiEnabled
                               ? root.connectivityBackend.networks : []
                        Item {
                            required property var modelData
                            width: parent.width
                            height: networkRow.height + (modelData.connected ? 38 : 0)
                            WifiNetworkRow {
                                id: networkRow
                                objectName: "wifiNetwork_" + modelData.ssid
                                network: modelData
                                largeText: root.largeText
                                enabled: root.connectivityBackend && root.connectivityBackend.wifiEnabled
                                         && !root.connectivityBackend.busy && !modelData.connecting
                                onLongPressed: root.openSavedWifiInfo(modelData)
                                onClicked: {
                                    if (longPressTriggered) {
                                        longPressTriggered = false
                                        return
                                    }
                                    if (modelData.connected) {
                                        root.connectivityBackend.disconnectWifi()
                                    } else if (modelData.saved || modelData.security === "Open") {
                                        root.connectivityBackend.connectWifi(modelData.ssid, "")
                                    } else if (modelData.security === "WEP") {
                                        root.showMessage("WEP networks are not supported.")
                                    } else {
                                        root.requestWifiPassword(modelData)
                                    }
                                }
                            }
                            Rectangle {
                                objectName: "wifiDivider_" + modelData.ssid
                                visible: !!modelData.connected
                                x: 47; y: networkRow.height
                                width: parent.width - 94; height: 2
                                color: "#d4dadd"
                            }
                        }
                    }
                    Column {
                        visible: root.currentTab === 1 && !!root.connectivityBackend
                                 && root.connectivityBackend.bluetoothEnabled
                        width: parent.width
                        Repeater {
                            model: root.rememberedBluetoothDevices
                            BluetoothDeviceRow {
                                required property var modelData
                                objectName: "bluetoothDevice_" + modelData.address
                                x: 47; width: parent.width - 67
                                device: modelData; largeText: root.largeText
                                actionState: root.connectivityBackend.bluetoothActionPath === modelData.path
                                    ? root.connectivityBackend.bluetoothActionState : ""
                                enabled: !root.connectivityBackend.busy
                                onClicked: root.activateBluetoothDevice(modelData)
                                onLongPressed: root.openBluetoothInfo(modelData)
                            }
                        }
                        Item {
                            objectName: "settingsBluetoothSeparator"
                            width: parent.width; height: 26
                            Rectangle {
                                x: 20; y: 8; width: parent.width - 40; height: 2
                                color: "#d4dadd"
                            }
                        }
                        Text {
                            objectName: "settingsBluetoothDiscoveredHeading"
                            x: 20; width: parent.width - 40
                            height: root.largeText ? 76 : 66
                            text: qsTr("Discovered devices")
                            color: "white"; font.family: "Inter"; font.weight: Font.ExtraLight
                            font.pixelSize: root.largeText ? 40 : 34
                        }
                        Repeater {
                            model: root.discoveredBluetoothDevices
                            BluetoothDeviceRow {
                                required property var modelData
                                objectName: "bluetoothDevice_" + modelData.address
                                x: 47; width: parent.width - 67
                                device: modelData; largeText: root.largeText
                                actionState: root.connectivityBackend.bluetoothActionPath === modelData.path
                                    ? root.connectivityBackend.bluetoothActionState : ""
                                enabled: !root.connectivityBackend.busy
                                onClicked: root.activateBluetoothDevice(modelData)
                            }
                        }
                    }
                    Text {
                        objectName: "settingsRadioStatus"
                        visible: root.currentTab === 0
                                 ? !root.connectivityBackend || !root.connectivityBackend.wifiEnabled
                                   || !root.connectivityBackend.networks.length
                                 : !root.connectivityBackend
                                   || !root.connectivityBackend.bluetoothEnabled
                                   || root.bluetoothDevices.length === 0
                        width: parent.width - 94
                        x: 47
                        text: root.currentTab === 1
                            ? (root.connectivityBackend ? root.connectivityBackend.bluetoothStatus
                               : (root.backend ? root.backend.bluetoothStatus
                                  : "Bluetooth is unavailable."))
                            : (root.connectivityBackend && !root.connectivityBackend.wifiEnabled
                               ? "Wi-Fi is off. Tap Wi-Fi or the scan icon to turn it on."
                               : root.connectivityBackend ? root.connectivityBackend.wifiStatus
                               : (root.backend ? root.backend.wifiStatus : "Wi-Fi is unavailable."))
                        color: "#d4dadd"
                        font.family: "Inter"
                        font.pixelSize: root.largeText ? 34 : 28
                        wrapMode: Text.Wrap
                    }
                }

                Column {
                    visible: !root.searching && root.currentTab === 3
                    width: parent.width
                    SettingsRow {
                        objectName: "settingsReduceMotion"
                        heading: "Reduce motion"
                        detail: "Skip launcher and drawer movement"
                        largeText: root.largeText
                        toggle: true
                        checked: root.backend ? root.backend.reduceMotion : false
                        enabled: root.backend !== null
                        onClicked: {
                            root.backend.setReduceMotion(checked)
                            checked = Qt.binding(() => root.backend.reduceMotion)
                        }
                    }
                    SettingsRow {
                        objectName: "settingsLargeText"
                        heading: "Larger text"
                        detail: "Increase text size in Settings"
                        largeText: root.largeText
                        toggle: true
                        checked: root.largeText
                        enabled: root.backend !== null
                        onClicked: {
                            root.backend.setLargeText(checked)
                            checked = Qt.binding(() => root.backend.largeText)
                        }
                    }
                }

                Item {
                    id: wallpaperPageContent
                    objectName: "settingsWallpaperPageContent"
                    visible: !root.searching && root.currentTab === 4
                    width: parent.width
                    height: 850
                    x: root.cropToolOpen ? -root.width : 0
                    Behavior on x {
                        NumberAnimation {
                            duration: root.backend && root.backend.reduceMotion ? 0 : 300
                            easing.type: Easing.InOutCubic
                        }
                    }
                    Text {
                        x: 0; y: 0; width: parent.width
                        text: "current wallpaper"
                        color: "white"
                        font.family: "Inter"
                        font.weight: Font.ExtraLight
                        font.pixelSize: 34
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Item {
                        id: phonePreview
                        x: (parent.width - width) / 2
                        y: 70; width: 280; height: 592
                        Image {
                            anchors.fill: parent
                            source: "assets/Pixel.png"
                            smooth: true
                        }
                        Rectangle {
                            x: 12; y: 48; width: 256; height: 500
                            radius: 14
                            color: "black"
                            Image {
                                id: phoneWallpaper
                                anchors.fill: parent
                                source: Wallpaper.source
                                fillMode: Image.PreserveAspectCrop
                                smooth: true
                                mipmap: true
                                visible: false
                                layer.enabled: true
                            }
                            Rectangle {
                                id: phoneScreenMask
                                anchors.fill: parent
                                radius: 14
                                color: "white"
                                visible: false
                                layer.enabled: true
                            }
                            MultiEffect {
                                anchors.fill: parent
                                source: phoneWallpaper
                                maskEnabled: true
                                maskSource: phoneScreenMask
                            }
                            border.color: "#303030"
                            border.width: 2
                        }
                        TapHandler {
                            onTapped: if (Wallpaper.source.toString().length)
                                          wallpaperCropDialog.openImage(Wallpaper.source)
                        }
                    }
                    AbstractButton {
                        objectName: "settingsEditWallpaper"
                        x: (parent.width - width) / 2
                        y: 686; width: 290; height: 74
                        Accessible.name: "Edit wallpaper"
                        enabled: !OpenWavePortals.busy
                        onClicked: OpenWavePortals.openFile({
                            title: "Choose wallpaper",
                            mutedTitle: qsTr("Choose"),
                            primaryTitle: qsTr("wallpaper"),
                            mimeTypes: ["image/png", "image/jpeg", "image/webp"],
                            requestIdentifier: "settings-wallpaper"
                        })
                        background: Item {}
                        contentItem: Text {
                            text: "✎  edit wallpaper"
                            color: "white"
                            font.family: "Inter"
                            font.weight: Font.ExtraLight
                            font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        AbstractButton {
            id: addNetworkButton
            objectName: "settingsAddNetwork"
            visible: root.currentTab === 0 && !root.searching && !root.wifiInfoOpen
            opacity: root.tabIncomingOpacity
            x: 45
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            width: parent.width - 90
            height: 90
            enabled: root.connectivityBackend && root.connectivityBackend.wifiEnabled
                     && !root.connectivityBackend.busy
            Accessible.name: "Add Network"
            onClicked: root.openAddNetwork()
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    color: addNetworkButton.down ? "#18ffffff" : "transparent"
                }
                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 2
                    color: "#d4dadd"
                }
            }
            contentItem: Item {
                Text {
                    x: 4
                    y: 25
                    text: "Add Network"
                    font.family: "Inter"
                    font.pixelSize: root.largeText ? 38 : 34
                    color: "white"
                }
                SettingsSymbol {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    y: 21
                    width: 48; height: 48
                    kind: "plus"
                }
            }
        }

        AppsPage {
            id: appsPage
            objectName: "settingsAppsPage"
            anchors.fill: parent
            visible: root.currentTab === 5 && !root.searching
            opacity: root.tabIncomingOpacity
            backend: root.applicationsBackend
            permissions: root.permissionClient
            prompts: root.promptClient
            largeText: root.largeText
            reducedMotion: root.backend ? root.backend.reduceMotion : false
        }
        GeneralSettingsPage {
            id: generalPage
            objectName: "settingsGeneralPage"
            x: root.permissionsOpen ? -paneViewport.width : 0
            width: paneViewport.width; height: paneViewport.height
            visible: root.currentTab === 2 && !root.searching
            enabled: !root.permissionsOpen
            opacity: root.tabIncomingOpacity
            backend: root.backend; timeBackend: root.timeBackend
            largeText: root.largeText
            reducedMotion: root.backend ? root.backend.reduceMotion : false
            onEditDeviceNameRequested: root.editName()
            onEditTimezoneRequested: root.editTimezone()
            onPermissionsRequested: {
                root.permissionsOpen = true
                if (root.permissionClient) root.permissionClient.refreshApplications()
            }
            onPowerRequested: action => root.requestPowerAction(action)
            Behavior on x {
                NumberAnimation {
                    duration: root.backend && root.backend.reduceMotion ? 0 : 240
                    easing.type: Easing.OutCubic
                }
            }
        }
        PermissionsPage {
            x: root.permissionsOpen ? 0 : paneViewport.width
            y: 0; width: paneViewport.width; height: paneViewport.height
            visible: root.currentTab === 2 && !root.searching
            enabled: root.permissionsOpen
            client: root.permissionClient
            prompts: root.promptClient
            largeText: root.largeText
            onBackRequested: root.permissionsOpen = false
            Behavior on x { NumberAnimation { duration: root.backend && root.backend.reduceMotion ? 0 : 240; easing.type: Easing.OutCubic } }
        }
        Flickable {
            id: bluetoothInfoPane
            objectName: "settingsBluetoothInfoPane"
            x: root.bluetoothInfoOpen ? 0 : paneViewport.width
            y: 0; width: paneViewport.width; height: paneViewport.height
            visible: root.currentTab === 1 && !root.searching
            enabled: root.bluetoothInfoOpen && !bluetoothInfoSlide.running
                     && !(root.promptClient && root.promptClient.busy)
            contentHeight: bluetoothInfoColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Behavior on x {
                NumberAnimation {
                    id: bluetoothInfoSlide
                    duration: root.tabSwitching || (root.backend && root.backend.reduceMotion) ? 0 : 300
                    easing.type: Easing.InOutCubic
                }
            }
            Column {
                id: bluetoothInfoColumn
                width: parent.width
                AbstractButton {
                    objectName: "settingsBluetoothInfoBack"
                    width: parent.width; height: 86
                    Accessible.name: qsTr("Back to Bluetooth devices")
                    onClicked: root.closeBluetoothInfo()
                    background: Item {}
                    contentItem: Text {
                        text: "‹ " + qsTr("Bluetooth")
                        color: "white"; font.family: "Inter"; font.pixelSize: 34
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    objectName: "settingsBluetoothInfoName"
                    width: parent.width; bottomPadding: 22
                    text: root.selectedBluetoothName; textFormat: Text.PlainText
                    wrapMode: Text.Wrap; color: "white"; font.family: "Inter"
                    font.weight: Font.DemiBold; font.pixelSize: root.largeText ? 44 : 40
                }
                SettingsRow {
                    heading: qsTr("Status")
                    detail: root.connectivityBackend && root.connectivityBackend.bluetoothActionPath === root.selectedBluetoothPath
                        && root.connectivityBackend.bluetoothActionState === "connecting" ? qsTr("Connecting…")
                        : root.selectedBluetoothDevice.connected ? qsTr("Connected") : qsTr("Previously connected")
                    largeText: root.largeText; enabled: false
                }
                SettingsRow {
                    objectName: "settingsBluetoothNickname"
                    heading: qsTr("Edit nickname")
                    detail: root.selectedBluetoothName
                    largeText: root.largeText
                    enabled: !!root.selectedBluetoothDevice.path && !root.connectivityBackend.busy
                    onClicked: root.requestBluetoothPrompt("nickname")
                }
                SettingsRow {
                    objectName: "settingsBluetoothDisconnect"
                    visible: !!root.selectedBluetoothDevice.connected
                    heading: qsTr("Disconnect")
                    detail: qsTr("Keep this device paired")
                    largeText: root.largeText
                    enabled: !!root.connectivityBackend && !root.connectivityBackend.busy
                    onClicked: root.connectivityBackend.disconnectBluetoothDevice(root.selectedBluetoothPath)
                }
                SettingsRow {
                    objectName: "settingsBluetoothConnect"
                    visible: !!root.selectedBluetoothDevice.path && !root.selectedBluetoothDevice.connected
                    heading: qsTr("Connect")
                    largeText: root.largeText
                    enabled: !!root.connectivityBackend && !root.connectivityBackend.busy
                    onClicked: root.connectivityBackend.connectBluetoothDevice(root.selectedBluetoothPath)
                }
                SettingsRow {
                    objectName: "settingsBluetoothForget"
                    heading: qsTr("Forget this device")
                    detail: qsTr("Remove its saved pairing")
                    largeText: root.largeText
                    enabled: !!root.selectedBluetoothDevice.path && !root.connectivityBackend.busy
                    onClicked: root.requestBluetoothPrompt("forget")
                }
            }
        }

        Flickable {
            id: wifiInfoPane
            objectName: "settingsWifiInfoPane"
            opacity: root.tabIncomingOpacity
            x: root.wifiInfoOpen ? 0 : paneViewport.width
            y: 0; width: paneViewport.width; height: paneViewport.height
            visible: root.currentTab === 0 && !root.searching
            contentHeight: wifiInfoColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Behavior on x {
                NumberAnimation {
                    duration: root.tabSwitching || (root.backend && root.backend.reduceMotion) ? 0 : 300
                    easing.type: Easing.InOutCubic
                }
            }
            Column {
                id: wifiInfoColumn
                width: parent.width
                spacing: 0
                AbstractButton {
                    objectName: "settingsWifiInfoBack"
                    width: parent.width
                    height: 86
                    Accessible.name: "Back to Wi-Fi networks"
                    onClicked: root.closeSavedWifiInfo()
                    contentItem: Text {
                        text: "‹  Saved network"
                        color: "white"
                        font.family: "Inter"
                        font.pixelSize: root.largeText ? 38 : 34
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? "#18ffffff" : "transparent"
                    }
                }
                Text {
                    objectName: "settingsWifiInfoSsid"
                    width: parent.width
                    text: root.selectedWifiSsid
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: "white"
                    font.family: "Inter"
                    font.weight: Font.DemiBold
                    font.pixelSize: root.largeText ? 44 : 40
                    bottomPadding: 22
                }
                Text {
                    visible: !!(root.connectivityBackend && root.connectivityBackend.savedWifiInfoLoading)
                    text: "Loading saved network details…"
                    color: "#d4dadd"
                    font.family: "Inter"
                    font.pixelSize: 27
                    bottomPadding: 18
                }
                SettingsRow {
                    heading: "Status"
                    detail: root.wifiInfo.connected ? "Connected" : "Saved"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    heading: "Security type"
                    detail: root.wifiInfo.security || "Unavailable"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    objectName: "settingsWifiInfoPassword"
                    heading: "Password"
                    detail: root.connectivityBackend && root.connectivityBackend.savedWifiInfoLoading
                            ? "Loading…"
                            : !root.wifiInfo.password
                              ? (root.wifiInfo.passwordStatus || "Unavailable")
                            : root.wifiPasswordVisible ? root.wifiInfo.password : "••••••••"
                    largeText: root.largeText
                    enabled: !!root.wifiInfo.password
                    Accessible.name: "Password, " + (root.wifiPasswordVisible ? "visible" : "hidden")
                    onClicked: root.wifiPasswordVisible = !root.wifiPasswordVisible
                }
                Text {
                    objectName: "settingsWifiInfoPasswordAction"
                    visible: !!root.wifiInfo.password
                    text: root.wifiPasswordVisible ? "Tap password to hide" : "Tap password to show"
                    color: "#00b7f0"
                    font.family: "Inter"
                    font.pixelSize: 24
                    topPadding: 8
                    bottomPadding: 10
                }
                SettingsRow {
                    heading: "Auto-connect"
                    detail: root.wifiInfo.autoConnect ? "On" : "Off"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    heading: "Signal"
                    detail: root.wifiInfo.strength > 0 ? root.wifiInfo.strength + "%" : "Out of range"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    heading: "Frequency"
                    detail: root.wifiInfo.frequency > 0 ? root.wifiInfo.frequency + " MHz" : "Unavailable"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    heading: "Access point (BSSID)"
                    detail: root.wifiInfo.bssid || "Unavailable"
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    objectName: "settingsWifiIpConfig"
                    heading: "IP and DNS settings"
                    detail: (root.wifiInfo.ipManual ? "Manual IPv4" : "Automatic IP")
                            + " • " + (root.wifiInfo.dnsManual ? "Manual DNS" : "Automatic DNS")
                    largeText: root.largeText
                    enabled: !!root.connectivityBackend && !root.connectivityBackend.busy
                             && !root.connectivityBackend.savedWifiInfoLoading
                             && root.wifiInfo.ssid === root.selectedWifiSsid
                             && !(root.promptClient && root.promptClient.busy)
                    onClicked: root.openIpConfig()
                }
                SettingsRow {
                    visible: !!root.wifiInfo.ipv4Address
                    heading: "IP address"
                    detail: root.wifiInfo.ipv4Address || ""
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    visible: !!root.wifiInfo.gateway
                    heading: "Gateway"
                    detail: root.wifiInfo.gateway || ""
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    visible: !!root.wifiInfo.dns
                    heading: "DNS"
                    detail: root.wifiInfo.dns || ""
                    largeText: root.largeText
                    enabled: false
                }
                SettingsRow {
                    objectName: "settingsForgetWifi"
                    heading: qsTr("Forget network")
                    detail: qsTr("Remove saved password and settings")
                    largeText: root.largeText
                    enabled: !!root.connectivityBackend && !root.connectivityBackend.busy
                             && !root.connectivityBackend.savedWifiInfoLoading
                             && root.wifiInfo.ssid === root.selectedWifiSsid
                             && !(root.promptClient && root.promptClient.busy)
                    onClicked: root.requestForgetWifi()
                }
            }
        }
        Image {
            id: outgoingTabFrame
            objectName: "settingsOutgoingTabFrame"
            anchors.fill: parent
            source: root.tabCapture ? root.tabCapture.url : ""
            fillMode: Image.Stretch
            smooth: true
            opacity: 0
            visible: opacity > 0
            z: 10
            MouseArea { anchors.fill: parent }
        }
        }

        Popup {
            id: wallpaperCropDialog
            objectName: "settingsWallpaperCropper"
            parent: Overlay.overlay
            width: parent.width; height: parent.height
            x: 0; y: 0
            modal: true; dim: false; padding: 0
            closePolicy: Popup.NoAutoClose
            property url imageUrl
            property var imageDimensions: ({width: 0, height: 0})
            property real imageX: 0
            property real imageY: 0
            property real previousZoom: 1
            property bool cropReady: false
            readonly property bool saving: typeof Wallpaper !== "undefined" && Wallpaper.busy
            readonly property real baseScale: imageDimensions.width > 0 && imageDimensions.height > 0
                ? Math.max(cropViewport.width / imageDimensions.width,
                           cropViewport.height / imageDimensions.height) : 1
            function constrain() {
                imageX = Math.max(cropViewport.width - cropImage.width, Math.min(0, imageX))
                imageY = Math.max(cropViewport.height - cropImage.height, Math.min(0, imageY))
            }
            function setZoom(value) {
                const next = Math.max(zoomSlider.from, Math.min(zoomSlider.to, value))
                const ratio = next / previousZoom
                zoomSlider.value = next
                imageX = cropViewport.width / 2 - (cropViewport.width / 2 - imageX) * ratio
                imageY = cropViewport.height / 2 - (cropViewport.height / 2 - imageY) * ratio
                previousZoom = next
                constrain()
            }
            function openImage(url) {
                const dimensions = Wallpaper.imageSize(url)
                if (dimensions.width <= 0 || dimensions.height <= 0) {
                    root.showMessage("This image cannot be opened.")
                    return
                }
                showCropImage(url, dimensions)
            }
            function showCropImage(url, dimensions) {
                imageUrl = url
                imageDimensions = dimensions
                zoomSlider.value = 1
                previousZoom = 1
                imageX = (cropViewport.width - cropImage.width) / 2
                imageY = (cropViewport.height - cropImage.height) / 2
                cropReady = false
                cropPage.x = root.width
                root.cropToolOpen = true
                open()
                if (root.backend && root.backend.reduceMotion) {
                    cropPage.x = 0
                    cropReady = true
                } else cropOpen.start()
            }
            function beginClose() {
                if (!visible || cropClose.running) return
                cropOpen.stop()
                cropReady = false
                root.cropToolOpen = false
                if (root.backend && root.backend.reduceMotion) close()
                else cropClose.start()
            }
            onClosed: {
                cropOpen.stop()
                cropClose.stop()
                cropReady = false
                root.cropToolOpen = false
                cropPage.x = root.width
            }
            NumberAnimation {
                id: cropOpen
                target: cropPage; property: "x"
                from: root.width; to: 0; duration: 300
                easing.type: Easing.OutCubic
                onFinished: wallpaperCropDialog.cropReady = true
            }
            NumberAnimation {
                id: cropClose
                target: cropPage; property: "x"
                to: root.width; duration: 260
                easing.type: Easing.InCubic
                onFinished: wallpaperCropDialog.close()
            }
            background: Item {}
            contentItem: Item {
                Rectangle {
                    id: cropPage
                    objectName: "settingsCropPage"
                    x: root.width; y: 0
                    width: parent.width; height: parent.height
                    color: "#dd27323b"
                    Item {
                        id: cropViewport
                        objectName: "settingsCropViewport"
                        height: Math.min(1108, cropPage.height - 188)
                        width: height * 768 / 1366
                        x: (parent.width - width) / 2; y: root.topInset + 47
                        clip: true
                        Rectangle { anchors.fill: parent; color: "black" }
                        Image {
                            id: cropImage
                            source: wallpaperCropDialog.imageUrl
                            x: wallpaperCropDialog.imageX; y: wallpaperCropDialog.imageY
                            width: wallpaperCropDialog.imageDimensions.width
                                   * wallpaperCropDialog.baseScale * zoomSlider.value
                            height: wallpaperCropDialog.imageDimensions.height
                                    * wallpaperCropDialog.baseScale * zoomSlider.value
                            fillMode: Image.Stretch; smooth: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: wallpaperCropDialog.cropReady
                            property real lastX: 0
                            property real lastY: 0
                            onPressed: mouse => { lastX = mouse.x; lastY = mouse.y }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                wallpaperCropDialog.imageX += mouse.x - lastX
                                wallpaperCropDialog.imageY += mouse.y - lastY
                                wallpaperCropDialog.constrain()
                                lastX = mouse.x; lastY = mouse.y
                            }
                        }
                        Repeater {
                            model: 2
                            Rectangle {
                                required property int index
                                objectName: "settingsCropGridLine"
                                x: (index + 1) * cropViewport.width / 3 - width / 2
                                width: 3; height: cropViewport.height
                                color: "white"; opacity: 0.9
                            }
                        }
                        Repeater {
                            model: 2
                            Rectangle {
                                required property int index
                                objectName: "settingsCropGridLine"
                                y: (index + 1) * cropViewport.height / 3 - height / 2
                                width: cropViewport.width; height: 3
                                color: "white"; opacity: 0.9
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"; border.color: "white"; border.width: 3
                        }
                    }
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            width: 14; height: 14; color: "#e5e5e5"
                            x: cropViewport.x + (index % 2 ? cropViewport.width : 0) - width / 2
                            y: cropViewport.y + (index > 1 ? cropViewport.height : 0) - height / 2
                        }
                    }
                    AbstractButton {
                        objectName: "settingsCropCancel"
                        x: 40; y: cropViewport.y + cropViewport.height + 31
                        width: 145; height: 55
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.beginClose()
                        background: Item {}
                        contentItem: Text {
                            text: "cancel"; color: "white"
                            font.family: "Inter"; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Slider {
                        id: zoomSlider
                        objectName: "settingsCropZoom"
                        x: 205; y: cropViewport.y + cropViewport.height + 37
                        width: 365; height: 44
                        from: 1; to: 3; value: 1
                        enabled: wallpaperCropDialog.cropReady
                        onMoved: wallpaperCropDialog.setZoom(value)
                        background: Rectangle {
                            y: parent.height / 2 - height / 2
                            width: parent.width; height: 3; color: "white"
                        }
                        handle: Rectangle {
                            x: zoomSlider.leftPadding + zoomSlider.visualPosition
                                * (zoomSlider.availableWidth - width)
                            y: (zoomSlider.height - height) / 2
                            width: 20; height: 20; radius: 10; color: "white"
                        }
                    }
                    AbstractButton {
                        objectName: "settingsCropApply"
                        x: 614; y: cropViewport.y + cropViewport.height + 31
                        width: 115; height: 55
                        enabled: wallpaperCropDialog.cropReady && !wallpaperCropDialog.saving
                        onClicked: {
                            const factor = 768 / cropViewport.width
                            Wallpaper.setCroppedWallpaper(wallpaperCropDialog.imageUrl,
                                cropImage.x * factor, cropImage.y * factor,
                                cropImage.width * factor, cropImage.height * factor)
                        }
                        background: Item {}
                        contentItem: Text {
                            text: wallpaperCropDialog.saving ? "saving…" : "crop"
                            color: "white"; font.family: "Inter"; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    AbstractButton {
                        x: 191; y: cropViewport.y + cropViewport.height + 80
                        width: 46; height: 46
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.setZoom(zoomSlider.value - 0.1)
                        background: Item {}
                        contentItem: Text {
                            text: "-"; color: "white"; font.family: "Inter"; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Text {
                        x: 338; y: cropViewport.y + cropViewport.height + 83
                        text: "zoom"; color: "white"; font.family: "Inter"; font.pixelSize: 34
                    }
                    AbstractButton {
                        x: 532; y: cropViewport.y + cropViewport.height + 80
                        width: 46; height: 46
                        enabled: wallpaperCropDialog.cropReady
                        onClicked: wallpaperCropDialog.setZoom(zoomSlider.value + 0.1)
                        background: Item {}
                        contentItem: Text {
                            text: "+"; color: "white"; font.family: "Inter"; font.pixelSize: 34
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        visible: !wallpaperCropDialog.cropReady
                        preventStealing: true
                    }
                }
            }
        }

        Rectangle {
            visible: noticeTimer.running
            z: 100
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 70
            width: Math.min(parent.width - 80, notice.implicitWidth + 40)
            height: notice.implicitHeight + 32
            color: "#ed25313a"
            radius: 10
            Text {
                id: notice
                objectName: "settingsNotice"
                width: parent.width - 40
                anchors.centerIn: parent
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.family: "Inter"
                font.pixelSize: 26
                color: "white"
            }
            Timer { id: noticeTimer; interval: 3000 }
        }
    }
}
