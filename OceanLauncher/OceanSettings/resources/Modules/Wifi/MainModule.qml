import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0
import OpenWave.Controls 1.0
import OceanSettings 1.0
import "Components"
import "Components/WifiValidation.js" as Validation

Item {
    id: root
    objectName: "wifiModule"
    readonly property var backend: typeof Connectivity !== "undefined" ? Connectivity : null
    readonly property var prompts: typeof Prompts !== "undefined" ? Prompts : null
    property int scanDurationMs: 30000
    property int scanIntervalMs: 12000
    property int refreshIntervalMs: 1500
    property string pendingAction: ""
    property string pendingSsid: ""
    property string errorMessage: ""
    property string noticeMessage: ""
    property bool scanActive: false
    property bool scanStarted: false
    property bool scanRequested: false
    property bool infoOpen: false
    property string selectedSsid: ""
    property bool passwordVisible: false
    readonly property bool serviceReady: !!backend && backend.serviceAvailable
    readonly property bool adapterReady: serviceReady && backend.wifiDeviceAvailable
    readonly property bool radioOn: adapterReady && backend.wifiEnabled
    readonly property var savedInfo: backend && backend.savedWifiInfo ? backend.savedWifiInfo : ({})
    readonly property bool infoLoading: !!backend && !!backend.savedWifiInfoLoading
    readonly property var sortedNetworks: sortNetworks(backend ? backend.networks : [])
    readonly property string statusText: backend ? backend.wifiStatus : qsTr("Connectivity service unavailable.")
    readonly property bool infoActionsEnabled: !!backend && !backend.busy && !infoLoading
                                               && savedInfo.ssid === selectedSsid
                                               && !(prompts && prompts.busy)

    function sortNetworks(list) {
        const copy = []
        for (let index = 0; index < list.length; ++index)
            copy.push(list[index])
        copy.sort((left, right) => {
            if (!!left.connected !== !!right.connected)
                return left.connected ? -1 : 1
            if (!!left.connecting !== !!right.connecting)
                return left.connecting ? -1 : 1
            const byStrength = (Number(right.strength) || 0) - (Number(left.strength) || 0)
            if (byStrength !== 0)
                return byStrength
            return String(left.ssid).localeCompare(String(right.ssid))
        })
        return copy
    }

    function nameOf(network) {
        const ssid = network && network.ssid ? String(network.ssid).trim() : ""
        return ssid.length > 0 ? ssid : qsTr("Hidden network")
    }

    function showError(message) {
        noticeMessage = ""
        errorMessage = message
        errorTimer.restart()
    }

    function showNotice(message) {
        errorMessage = ""
        noticeMessage = message
        noticeTimer.restart()
    }

    function clearPending() {
        pendingAction = ""
        pendingSsid = ""
    }

    function cancelPrompt() {
        const waiting = pendingAction.length > 0
        clearPending()
        if (waiting && prompts && prompts.busy)
            prompts.cancel()
    }

    function stopScan(reset) {
        scanActive = false
        scanRequested = false
        scanTimeout.stop()
        if (reset)
            scanStarted = false
    }

    function maybeStartScan() {
        if (scanActive || scanStarted || !visible || !radioOn)
            return
        startScan()
    }

    function startScan() {
        if (!radioOn)
            return
        scanStarted = true
        scanActive = true
        scanRequested = false
        scanTimeout.restart()
        scanRepeat.restart()
        requestScanIfReady()
    }

    function requestScanIfReady() {
        if (!scanActive || !backend)
            return
        if (backend.wifiScanAvailable && !backend.busy) {
            scanRequested = true
            backend.requestScan()
        } else if (!backend.wifiScanAvailable) {
            scanRequested = false
            backend.refresh()
        }
    }

    function handleBackendChanged() {
        if (!radioOn) {
            stopScan(true)
            return
        }
        maybeStartScan()
        if (scanActive && backend.wifiScanAvailable && !scanRequested && !backend.busy)
            requestScanIfReady()
    }

    function refreshNetworks() {
        if (!backend)
            return
        if (!adapterReady) {
            backend.refresh()
            return
        }
        if (!backend.wifiEnabled) {
            backend.setWifiEnabled(true)
            return
        }
        startScan()
    }

    function requestPassword(network) {
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        if (prompts.busy)
            return
        pendingSsid = network.ssid
        pendingAction = "connect"
        prompts.passwordInput(network.ssid, network.security || "", {
            placeholder: qsTr("type password"), minLength: 8, maxLength: 63,
            acceptLabel: qsTr("Connect"), cancelLabel: qsTr("Cancel")
        })
    }

    function activate(network) {
        if (!backend || backend.busy || network.connecting || network.connected)
            return
        if (!network.ssid || String(network.ssid).trim().length === 0) {
            showError(qsTr("Hidden networks cannot be joined from this list."))
            return
        }
        errorMessage = ""
        if (network.saved || network.security === "Open")
            backend.connectWifi(network.ssid, "")
        else if (network.security === "WEP")
            showError(qsTr("WEP networks are not supported."))
        else
            requestPassword(network)
    }

    function disconnectActive() {
        if (backend && !backend.busy)
            backend.disconnectWifi()
    }

    function openSavedInfo(network) {
        if (!network || !network.saved || !backend)
            return
        selectedSsid = network.ssid
        passwordVisible = false
        infoOpen = true
        backend.loadSavedWifiInfo(network.ssid)
    }

    function closeSavedInfo() {
        if (!infoOpen)
            return
        infoOpen = false
        if (pendingAction === "forget" || pendingAction === "ipConfig")
            cancelPrompt()
        passwordVisible = false
        if (backend)
            backend.clearSavedWifiInfo()
        maybeStartScan()
    }

    function requestForget() {
        if (!backend || backend.busy || !infoOpen || infoLoading || !selectedSsid
                || savedInfo.ssid !== selectedSsid || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        pendingSsid = selectedSsid
        pendingAction = "forget"
        prompts.confirmation(qsTr("Forget network?"),
            qsTr("Are you sure you want to forget “%1”? Its saved password and settings will be removed.").arg(selectedSsid),
            {acceptLabel: qsTr("Forget"), cancelLabel: qsTr("Cancel")})
    }

    function openAddNetwork() {
        if (!backend || backend.busy || !radioOn || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        pendingAction = "add"
        pendingSsid = ""
        prompts.form(qsTr("Add Network"), "", {
            fieldOrder: "ssid,security,password",
            fields: {
                ssid: {placeholder: qsTr("Network name (SSID)"), minLength: 1, maxLength: 32, maxBytes: 32},
                security: {label: qsTr("Security"), initialText: "wpa",
                    choices: {wpa: qsTr("WPA Personal"), open: qsTr("Open")}},
                password: {placeholder: qsTr("Password"), secret: true, minLength: 8, maxLength: 63,
                    activeWhenField: "security", activeWhenValue: "wpa"}
            }, acceptLabel: qsTr("Connect"), cancelLabel: qsTr("Cancel")})
    }

    function openIpConfig(values, error) {
        if (!backend || backend.busy || !infoOpen || infoLoading || !selectedSsid
                || savedInfo.ssid !== selectedSsid || !visible || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        const initial = values || {
            ipMode: savedInfo.ipManual ? "manual" : "automatic",
            address: savedInfo.configuredAddress || "",
            prefix: String(savedInfo.configuredPrefix === undefined ? 24 : savedInfo.configuredPrefix),
            gateway: savedInfo.configuredGateway || "",
            dnsMode: savedInfo.dnsManual ? "manual" : "automatic",
            dnsServers: (savedInfo.configuredDns || []).join(", ")
        }
        pendingSsid = selectedSsid
        pendingAction = "ipConfig"
        prompts.form(qsTr("IP and DNS settings"),
            (error ? error + "\n\n" : "") + selectedSsid + "\n"
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
                || !["automatic", "manual"].includes(values.dnsMode))
            return
        const address = manualIp ? (values.address || "").trim() : ""
        const prefixText = (values.prefix || "").trim()
        const prefix = manualIp ? Number(prefixText) : 0
        const gateway = manualIp ? (values.gateway || "").trim() : ""
        const dns = manualDns ? (values.dnsServers || "").trim().split(/[\s,;]+/).filter(value => value.length) : []
        let error = ""
        if (manualIp && (!Validation.validIpv4(address) || !/^\d{1,2}$/.test(prefixText)
                || prefix < 0 || prefix > 32 || (gateway && !Validation.validIpv4(gateway))))
            error = qsTr("Enter a valid IPv4 address, prefix (0–32), and gateway.")
        else if (manualDns && (!dns.length || dns.some(value => !Validation.validIpAddress(value))))
            error = qsTr("Enter at least one valid IPv4 or IPv6 DNS server.")
        if (error) {
            openIpConfig(values, error)
            return
        }
        backend.saveSavedWifiIpConfig(ssid, manualIp, address, prefix, gateway, manualDns, dns)
    }

    function back() {
        if (pendingAction.length > 0 && prompts && prompts.busy) {
            cancelPrompt()
            return true
        }
        if (infoOpen) {
            closeSavedInfo()
            return true
        }
        return false
    }

    onRadioOnChanged: {
        if (radioOn)
            maybeStartScan()
        else
            stopScan(true)
    }

    onVisibleChanged: {
        if (visible) {
            maybeStartScan()
        } else {
            cancelPrompt()
            stopScan(true)
            closeSavedInfo()
        }
    }

    onBackendChanged: {
        if (backend)
            backend.refresh()
        maybeStartScan()
    }

    Component.onCompleted: {
        if (backend)
            backend.refresh()
        maybeStartScan()
    }

    Component.onDestruction: {
        cancelPrompt()
        if (infoOpen && backend)
            backend.clearSavedWifiInfo()
    }

    Timer {
        id: scanTimeout
        interval: root.scanDurationMs
        onTriggered: root.stopScan(false)
    }
    Timer {
        id: scanRepeat
        interval: root.scanIntervalMs
        repeat: true
        running: root.scanActive
        onTriggered: root.requestScanIfReady()
    }
    Timer {
        interval: root.refreshIntervalMs
        repeat: true
        running: root.visible && !!root.backend
        onTriggered: root.backend.refresh()
    }
    Timer {
        id: errorTimer
        interval: 3000
        onTriggered: root.errorMessage = ""
    }
    Timer {
        id: noticeTimer
        interval: 3000
        onTriggered: root.noticeMessage = ""
    }

    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onErrorOccurred(message) { root.showError(message) }
        function onNetworkAdded() { root.showNotice(qsTr("Connecting to network…")) }
        function onConfigurationSaved(applied) {
            root.showNotice(applied ? qsTr("Network settings applied.") : qsTr("Saved. Reconnect to apply."))
        }
        function onScanDeferred() { root.scanRequested = false }
        function onChanged() { root.handleBackendChanged() }
    }

    Connections {
        target: root.prompts
        ignoreUnknownSignals: true
        function onFinished(requestId, result, text) {
            const action = root.pendingAction
            const ssid = root.pendingSsid
            root.clearPending()
            if (result !== "accepted" || action.length === 0)
                return
            if (action === "connect") {
                if (!root.backend || !root.radioOn)
                    return
                if (root.backend.busy) {
                    root.showError(qsTr("Wi-Fi is busy. Please try again."))
                    return
                }
                root.backend.connectWifi(ssid, text)
            } else if (action === "forget") {
                if (!root.infoOpen || ssid !== root.selectedSsid || root.savedInfo.ssid !== ssid || !root.backend)
                    return
                root.backend.forgetWifi(ssid)
                root.closeSavedInfo()
            }
        }
        function onFieldsFinished(requestId, result, values) {
            const action = root.pendingAction
            const ssid = root.pendingSsid
            if (action !== "add" && action !== "ipConfig")
                return
            root.clearPending()
            if (result !== "accepted" || !root.visible || !root.backend)
                return
            if (action === "add") {
                const name = (values.ssid || "").trim()
                if (!name.length) {
                    root.showError(qsTr("Enter a network name."))
                    return
                }
                const secured = values.security === "wpa"
                root.backend.addWifiNetwork(name, secured, secured ? values.password : "")
            } else {
                if (!root.infoOpen || ssid !== root.selectedSsid || root.savedInfo.ssid !== ssid
                        || root.backend.busy || root.backend.savedWifiInfoLoading)
                    return
                root.saveIpConfig(ssid, values)
            }
        }
    }

    Rectangle {
        id: toast
        visible: root.errorMessage.length > 0 || root.noticeMessage.length > 0
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        width: Math.min(parent.width - 32, toastText.implicitWidth + 40)
        height: toastText.implicitHeight + 32
        color: "#ed25313a"
        radius: 10
        Text {
            id: toastText
            objectName: "wifiToastText"
            width: parent.width - 40
            anchors.centerIn: parent
            text: root.errorMessage.length > 0 ? root.errorMessage : root.noticeMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Fonts.family
            font.pixelSize: 26
            color: "white"
        }
    }
    Item {
        objectName: "wifiErrorBanner"
        visible: root.errorMessage.length > 0
        width: 0
        height: 0
        Text { objectName: "wifiErrorText"; text: root.errorMessage }
    }
    Item {
        objectName: "wifiNoticeBanner"
        visible: root.noticeMessage.length > 0 && root.errorMessage.length === 0
        width: 0
        height: 0
        Text { objectName: "wifiNotice"; text: root.noticeMessage }
    }

    Flickable {
        id: scroller
        visible: !root.infoOpen
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.height - (addButton.visible ? addButton.height + 16 : 0)
        contentHeight: column.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: scroller.width

            Item {
                width: parent.width
                height: 100
                WRSwitch {
                    id: radioHeading
                    objectName: "wifiSwitch"
                    padding: 0
                    indicator: Item {}
                    checked: root.radioOn
                    Accessible.role: Accessible.CheckBox
                    Accessible.checked: checked
                    Accessible.name: root.radioOn ? qsTr("Turn off Wi-Fi") : qsTr("Turn on Wi-Fi")
                    x: 26
                    y: 0
                    width: Math.min(headingText.implicitWidth, 200) + 120
                    height: 56
                    enabled: root.adapterReady
                    onToggled: {
                        root.backend.setWifiEnabled(checked)
                        checked = Qt.binding(() => root.radioOn)
                    }
                    contentItem: Item {
                        Text {
                            id: headingText
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 120
                            text: qsTr("Wi-Fi")
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.family: Fonts.family
                            font.pixelSize: 36
                            font.weight: Font.DemiBold
                            color: "white"
                        }
                        Rectangle {
                            id: radioSwitch
                            objectName: "wifiSwitchTrack"
                            x: headingText.width + 20
                            anchors.verticalCenter: parent.verticalCenter
                            width: 100; height: 50; radius: 25
                            color: root.radioOn ? "#00d9e7" : "#777e83"
                            Rectangle {
                                x: root.radioOn ? 50 : 0
                                width: 50; height: 50; radius: 25
                                color: "white"
                            }
                        }
                    }
                    background: Item {}
                }
                AbstractButton {
                    objectName: "wifiRefresh"
                    anchors.right: parent.right
                    y: -6; width: 56; height: 56
                    enabled: !!root.backend
                    Accessible.name: root.backend && root.adapterReady && !root.backend.wifiEnabled
                        ? qsTr("Turn on Wi-Fi") : qsTr("Scan for Wi-Fi networks")
                    onClicked: root.refreshNetworks()
                    contentItem: Item {
                        SettingsSymbol {
                            objectName: "wifiRefreshSymbol"
                            anchors.centerIn: parent
                            width: 44; height: 44
                            kind: root.scanActive ? "scan" : "refresh"
                            color: "#cbd0d3"
                        }
                    }
                    background: Item {}
                }
            }

            Repeater {
                model: root.radioOn ? root.sortedNetworks : []
                Item {
                    required property var modelData
                    required property int index
                    width: parent.width
                    height: networkRow.height + (modelData.connected ? 38 : 0)
                    NetworkRow {
                        id: networkRow
                        objectName: "wifiNetwork_" + (String(modelData.ssid || "").trim().length > 0
                                                      ? modelData.ssid : "hidden" + index)
                        network: modelData
                        displayName: root.nameOf(modelData)
                        enabled: !!root.backend && !root.backend.busy && !modelData.connecting
                        onLongPressed: root.openSavedInfo(modelData)
                        onClicked: {
                            if (longPressTriggered) {
                                longPressTriggered = false
                                return
                            }
                            if (modelData.connected)
                                root.disconnectActive()
                            else
                                root.activate(modelData)
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

            Text {
                objectName: "wifiStatusText"
                visible: !root.radioOn || root.sortedNetworks.length === 0
                width: parent.width - 94
                x: 47
                text: root.adapterReady && !root.radioOn
                      ? qsTr("Wi-Fi is off. Tap Wi-Fi or the scan icon to turn it on.")
                      : root.statusText
                textFormat: Text.PlainText
                color: "#d4dadd"
                font.family: Fonts.family
                font.pixelSize: 28
                wrapMode: Text.Wrap
            }
        }
    }

    AbstractButton {
        id: addButton
        objectName: "wifiAddNetwork"
        visible: !root.infoOpen
        x: 45
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: parent.width - 90
        height: 90
        enabled: root.radioOn && !!root.backend && !root.backend.busy
        Accessible.name: qsTr("Add Network")
        onClicked: root.openAddNetwork()
        background: Item {
            Rectangle {
                anchors.fill: parent
                color: addButton.down ? "#18ffffff" : "transparent"
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
                text: qsTr("Add Network")
                font.family: Fonts.family
                font.pixelSize: 34
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

    SavedNetworkInfo {
        id: infoView
        objectName: "wifiInfoPane"
        anchors.fill: parent
        visible: root.infoOpen
        info: root.savedInfo
        ssid: root.selectedSsid
        loading: root.infoLoading
        passwordVisible: root.passwordVisible
        actionsEnabled: root.infoActionsEnabled
        onBackRequested: root.closeSavedInfo()
        onPasswordToggled: root.passwordVisible = !root.passwordVisible
        onIpConfigRequested: root.openIpConfig()
        onForgetRequested: root.requestForget()
    }
}
