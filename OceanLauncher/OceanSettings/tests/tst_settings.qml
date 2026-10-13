import QtQuick
import QtTest
import ".." as Ocean

Item {
    QtObject {
        id: backend
        property string deviceName: "KB Phone"
        property string localHostname: deviceName.toLowerCase().replace(/[^a-z0-9]/g, "") + ".local"
        property bool reduceMotion: false
        property bool largeText: false
        property string bluetoothStatus: "No Bluetooth adapter found."
        property string wifiStatus: "No Wi-Fi adapter found."
        property var systemInfo: ({os: "OpenWave", architecture: "x86_64", kernel: "test-kernel",
                                    processors: 4, storageTotal: 2147483648, storageAvailable: 1073741824})
        signal errorOccurred(string message)
        function saveDeviceName(name) { deviceName = name.trim(); return true }
        function setReduceMotion(value) { reduceMotion = value; return true }
        function setLargeText(value) { largeText = value; return true }
        function refreshRadios() {}
        function closeApp(home) {}
    }
    QtObject {
        id: connectivity
        property bool serviceAvailable: true
        property bool wifiDeviceAvailable: true
        property bool wifiEnabled: true
        property bool wifiScanAvailable: true
        property bool busy: false
        property bool bluetoothAdapterAvailable: true
        property bool bluetoothEnabled: true
        property bool bluetoothDiscovering: false
        property string bluetoothStatus: "No devices found. Tap refresh to search."
        property var bluetoothDevices: []
        property string bluetoothAction: ""
        property string bluetoothActionPath: ""
        property string bluetoothActionState: ""
        property string wifiStatus: "2 networks available"
        property var networks: [
            {ssid: "Open Network", security: "Open", strength: 80, connected: false, saved: false},
            {ssid: "Protected Network", security: "WPA", strength: 65, connected: false, saved: false}
        ]
        property var savedWifiInfo: ({})
        property bool savedWifiInfoLoading: false
        property string requestedInfoSsid: ""
        property int scanCount: 0
        property int refreshCount: 0
        property string connectedSsid: ""
        property string connectedPassword: ""
        property int disconnectCount: 0
        property string forgottenSsid: ""
        property var addedNetwork: ({})
        property var savedIpConfig: ({})
        property string requestedDeviceName: ""
        signal changed()
        signal scanDeferred()
        signal errorOccurred(string message)
        signal configurationSaved(bool applied)
        signal networkAdded()
        function setDeviceName(name) { requestedDeviceName = name }
        function setBluetoothEnabled(enabled) { bluetoothEnabled = enabled; bluetoothAction = "power" }
        function startBluetoothDiscovery() { bluetoothDiscovering = true; bluetoothAction = "discover" }
        function stopBluetoothDiscovery() { bluetoothDiscovering = false; bluetoothAction = "stop" }
        function pairBluetoothDevice(path) {
            bluetoothAction = "pair:" + path
            bluetoothActionPath = path; bluetoothActionState = "pairing"; busy = true
        }
        function connectBluetoothDevice(path) {
            bluetoothAction = "connect:" + path
            bluetoothActionPath = path; bluetoothActionState = "connecting"; busy = true
        }
        function disconnectBluetoothDevice(path) { bluetoothAction = "disconnect:" + path }
        function renameBluetoothDevice(path, nickname) { bluetoothAction = "rename:" + path + ":" + nickname }
        function forgetBluetoothDevice(path) { bluetoothAction = "forget:" + path }
        function refresh() { refreshCount++ }
        function requestScan() { scanCount++ }
        function setWifiEnabled(enabled) { wifiEnabled = enabled }
        function connectWifi(ssid, password) {
            connectedSsid = ssid
            connectedPassword = password
        }
        function addWifiNetwork(ssid, secured, password) {
            addedNetwork = ({ssid: ssid, secured: secured, password: password})
            networkAdded()
        }
        function disconnectWifi() { disconnectCount++ }
        function forgetWifi(ssid) { forgottenSsid = ssid }
        function loadSavedWifiInfo(ssid) {
            requestedInfoSsid = ssid
            savedWifiInfo = ({ssid: ssid, security: "WPA Personal",
                              password: "saved-secret", autoConnect: true,
                              connected: false, strength: 65, frequency: 2412,
                              bssid: "00:11:22:33:44:55"})
        }
        function clearSavedWifiInfo() { savedWifiInfo = ({}); requestedInfoSsid = "" }
        function saveSavedWifiIpConfig(ssid, manualIp, address, prefix, gateway, manualDns, dns) {
            savedIpConfig = ({ssid: ssid, manualIp: manualIp, address: address,
                              prefix: prefix, gateway: gateway, manualDns: manualDns, dns: dns})
            savedWifiInfo = Object.assign({}, savedWifiInfo,
                {ipManual: manualIp, configuredAddress: address, configuredPrefix: prefix,
                 configuredGateway: gateway, dnsManual: manualDns, configuredDns: dns})
            configurationSaved(true)
        }
    }
    QtObject {
        id: timeBackend
        property bool serviceAvailable: true
        property bool busy: false
        property string timezone: "Europe/Istanbul"
        property var zones: ["Europe/Istanbul", "Europe/London", "UTC"]
        signal changed()
        signal errorOccurred(string message)
        function refresh() {}
        function setTimezone(zone) {
            busy = true
            changed()
            timezone = zone
            busy = false
            changed()
        }
    }
    QtObject {
        id: prompts
        property bool busy: false
        property string kind: ""
        property string title: ""
        property var options: ({})
        signal finished(string requestId, string result, string text)
        signal fieldsFinished(string requestId, string result, var values)
        signal errorOccurred(string message)
        function textInput(heading, body, config) {
            kind = "input"; title = heading; options = config; busy = true
        }
        function passwordInput(heading, body, config) {
            kind = "password"; title = heading; options = config; busy = true
        }
        function confirmation(heading, body, config) {
            kind = "confirmation"; title = heading; options = config; busy = true
        }
        function selection(heading, body, config) { kind = "selection"; title = heading; options = config; busy = true }
        function form(heading, body, config) { kind = "form"; title = heading; options = config; busy = true }
        function respondFields(result, values) { busy = false; fieldsFinished("test", result, values) }
        function respond(result, text) { busy = false; finished("test", result, text || "") }
        function cancel() { if (kind === "form") respondFields("cancelled", {}); else respond("cancelled", "") }
    }
    Ocean.Main { id: app; settingsBackend: backend; promptClient: prompts }

    TestCase {
        name: "Settings"
        when: windowShown
        property var page
        SignalSpy { id: exitSpy; target: app; signalName: "exitRequested" }

        function initTestCase() {
            app.requestActivate()
            page = findChild(app, "settingsPage")
            verify(page !== null)
            waitForRendering(app.contentItem)
        }
        function init() {
            app.width = 768
            app.height = 1366
            app.connectivityBackend = null
            app.timeBackend = null
            page.wifiScanDurationMs = 30000
            page.wifiScanIntervalMs = 12000
            page.wifiRefreshIntervalMs = 1500
            connectivity.scanCount = 0
            connectivity.refreshCount = 0
            connectivity.wifiScanAvailable = true
            connectivity.connectedSsid = ""
            connectivity.connectedPassword = ""
            connectivity.disconnectCount = 0
            connectivity.forgottenSsid = ""
            connectivity.addedNetwork = ({})
            connectivity.savedIpConfig = ({})
            connectivity.savedWifiInfo = ({})
            connectivity.requestedInfoSsid = ""
            connectivity.bluetoothAdapterAvailable = true
            connectivity.bluetoothEnabled = true
            connectivity.bluetoothDiscovering = false
            connectivity.bluetoothDevices = []
            connectivity.bluetoothAction = ""
            connectivity.bluetoothActionPath = ""
            connectivity.bluetoothActionState = ""
            connectivity.busy = false
            prompts.busy = false
            prompts.kind = ""
            page.bluetoothPromptAction = ""
            page.addNetworkPending = false
            page.generalPromptAction = ""
            connectivity.requestedDeviceName = ""
            page.closeBluetoothInfo()
            connectivity.networks = [
                {ssid: "Open Network", security: "Open", strength: 80, connected: false, saved: false},
                {ssid: "Protected Network", security: "WPA", strength: 65, connected: false, saved: false}
            ]
            backend.reduceMotion = false
            backend.largeText = false
            backend.deviceName = "KB Phone"
            page.selectTab(1)
            findChild(page, "settingsTabs").contentX = 0
            exitSpy.clear()
            wait(50)
        }
        function test_wallpaperCropSlidesAndKeepsOutputAspect() {
            page.selectTab(4)
            let cropper = findChild(page, "settingsWallpaperCropper")
            let cropPage = findChild(page, "settingsCropPage")
            let viewport = findChild(page, "settingsCropViewport")
            let wallpaperPage = findChild(page, "settingsWallpaperPageContent")
            verify(cropper !== null)
            verify(cropPage !== null)
            verify(viewport !== null)
            verify(wallpaperPage !== null)
            cropper.showCropImage(Qt.resolvedUrl("../assets/wallpaper.png"),
                                  {width: 768, height: 1366})
            compare(cropper.visible, true)
            compare(cropPage.y, 0)
            compare(cropPage.height, app.height)
            compare(viewport.y, page.topInset + 47)
            verify(Math.abs(viewport.width / viewport.height - 768 / 1366) < 0.001)
            tryCompare(cropper, "cropReady", true)
            compare(cropPage.x, 0)
            tryCompare(wallpaperPage, "x", -768)
            compare(findChild(page, "settingsCropApply").enabled, true)
            cropper.setZoom(1.5)
            compare(findChild(page, "settingsCropZoom").value, 1.5)
            verify(cropper.imageX < 0 && cropper.imageY < 0)
            cropper.beginClose()
            tryCompare(cropper, "visible", false)
            tryCompare(wallpaperPage, "x", 0)
        }
        function test_timezonePickerChangesGeneralSetting() {
            app.timeBackend = timeBackend
            timeBackend.timezone = "Europe/Istanbul"
            page.selectTab(2)
            wait(50)
            mouseClick(findChild(page, "settingsTimezone"))
            compare(prompts.kind, "selection")
            compare(prompts.title, "Time zone")
            compare(prompts.options.initialText, "Europe/Istanbul")
            compare(prompts.options.choices["Europe/London"], "Europe/London")
            prompts.respond("cancelled", "Europe/London")
            compare(timeBackend.timezone, "Europe/Istanbul")
            page.editTimezone()
            prompts.respond("accepted", "Unknown/Zone")
            compare(timeBackend.timezone, "Europe/Istanbul")
            page.editTimezone()
            prompts.respond("accepted", "Europe/London")
            compare(timeBackend.timezone, "Europe/London")
            compare(findChild(page, "settingsTimezone").detail, "Europe/London")
            compare(page.generalPromptAction, "")
        }
        function test_deviceNameUsesSharedPromptAndUpdatesBluetooth() {
            app.connectivityBackend = connectivity
            page.selectTab(2)
            tryCompare(page, "currentTab", 2)
            page.editName()
            compare(prompts.kind, "input")
            compare(prompts.title, "Device name")
            compare(prompts.options.initialText, "KB Phone")
            compare(prompts.options.maxLength, 64)
            prompts.respond("cancelled", "Other phone")
            compare(backend.deviceName, "KB Phone")
            compare(connectivity.requestedDeviceName, "")
            page.editName()
            prompts.respond("accepted", "My Phone")
            compare(backend.deviceName, "My Phone")
            compare(connectivity.requestedDeviceName, "My Phone")
            compare(page.generalPromptAction, "")
        }
        function test_generalPromptsCancelWhenLeavingPage() {
            page.selectTab(2)
            page.editName()
            compare(page.back(), true)
            compare(prompts.busy, false)
            compare(backend.deviceName, "KB Phone")
            page.editName()
            page.selectTab(0)
            compare(prompts.busy, false)
            compare(page.generalPromptAction, "")
            // An accepted result arriving after the page changed cannot act.
            page.selectTab(2)
            page.editName()
            page.currentTab = 0
            prompts.respond("accepted", "Stale Name")
            compare(backend.deviceName, "KB Phone")
        }
        function test_wifiNetworksAndConnection() {
            app.connectivityBackend = connectivity
            page.selectTab(0)
            wait(100)
            verify(connectivity.scanCount > 0)
            compare(findChild(page, "settingsRadioStatus").text, "2 networks available")
            let openNetwork = findChild(page, "wifiNetwork_Open Network")
            mouseClick(openNetwork)
            compare(connectivity.connectedSsid, "Open Network")
            compare(connectivity.connectedPassword, "")
            mouseClick(findChild(page, "wifiNetwork_Protected Network"))
            compare(prompts.kind, "password")
            compare(prompts.title, "Protected Network")
            compare(prompts.options.acceptLabel, "Connect")
            compare(connectivity.connectedSsid, "Open Network")
            prompts.respond("accepted", "test-password")
            compare(connectivity.connectedSsid, "Protected Network")
            compare(connectivity.connectedPassword, "test-password")
            compare(findChild(page, "settingsWifiPasswordDialog"), null)
        }
        function test_wifiPasswordCancellationAndStaleResult() {
            app.connectivityBackend = connectivity
            page.selectTab(0)
            page.requestWifiPassword(connectivity.networks[1])
            compare(prompts.kind, "password")
            compare(page.back(), true)
            compare(connectivity.connectedSsid, "")
            page.requestWifiPassword(connectivity.networks[1])
            page.selectTab(1)
            compare(prompts.busy, false)
            prompts.respond("accepted", "ignored-password")
            compare(connectivity.connectedSsid, "")
            page.selectTab(0)
            page.requestWifiPassword(connectivity.networks[1])
            prompts.respond("renderer_lost", "ignored-password")
            compare(connectivity.connectedSsid, "")
        }
        function test_forgetDisconnectedWifiRequiresConfirmation() {
            app.connectivityBackend = connectivity
            page.selectTab(0)
            page.openSavedWifiInfo({ssid: "Remembered", saved: true, connected: false})
            wait(350)
            const forget = findChild(page, "settingsForgetWifi")
            verify(forget.visible && forget.enabled)
            compare(page.wifiInfo.connected, false)
            page.requestForgetWifi()
            compare(prompts.kind, "confirmation")
            compare(prompts.options.acceptLabel, "Forget")
            compare(prompts.options.cancelLabel, "Cancel")
            prompts.respond("cancelled", "")
            compare(connectivity.forgottenSsid, "")
            compare(page.wifiInfoOpen, true)
            page.requestForgetWifi()
            page.closeSavedWifiInfo()
            prompts.respond("accepted", "")
            compare(connectivity.forgottenSsid, "")
            page.openSavedWifiInfo({ssid: "Remembered", saved: true, connected: false})
            page.requestForgetWifi()
            prompts.respond("accepted", "")
            compare(connectivity.forgottenSsid, "Remembered")
            compare(page.wifiInfoOpen, false)
        }
        function test_wifiPageLayoutAndAddNetwork() {
            connectivity.networks = [
                {ssid: "KNETZ", security: "WPA", strength: 90, connected: true, saved: true},
                {ssid: "SUPERONLINE-5G-1834", security: "WPA", strength: 86,
                 connected: false, saved: false},
                {ssid: "OpenWifiPublic", security: "Open", strength: 82,
                 connected: false, saved: false}
            ]
            app.connectivityBackend = connectivity
            page.selectTab(0)
            wait(350)
            let connected = findChild(page, "wifiNetwork_KNETZ")
            let other = findChild(page, "wifiNetwork_SUPERONLINE-5G-1834")
            let open = findChild(page, "wifiNetwork_OpenWifiPublic")
            let divider = findChild(page, "wifiDivider_KNETZ")
            let add = findChild(page, "settingsAddNetwork")
            verify(connected !== null && other !== null && open !== null && divider !== null)
            compare(connected.height, 94)
            compare(divider.y, connected.height)
            compare(divider.parent.height, 132)
            compare(other.height, 66)
            verify(add.visible)
            verify(add.y > open.y + open.height)
            compare(findChild(page, "settingsWifiToggle"), null)
            mouseClick(divider)
            compare(connectivity.disconnectCount, 0)
            mouseClick(connected)
            compare(connectivity.disconnectCount, 1)

            mouseClick(add)
            compare(prompts.kind, "form")
            compare(prompts.title, "Add Network")
            compare(prompts.options.fields.password.secret, true)
            compare(prompts.options.fields.security.choices.open, "Open")
            compare(prompts.options.fields.security.choices.wpa, "WPA Personal")
            compare(prompts.options.fields.ssid.maxBytes, 32)
            prompts.respondFields("cancelled", {})
            compare(Object.keys(connectivity.addedNetwork).length, 0)
            compare(page.addNetworkPending, false)

            mouseClick(add)
            prompts.respondFields("accepted", {ssid: "New Network", security: "wpa", password: "test-password"})
            compare(connectivity.addedNetwork.ssid, "New Network")
            compare(connectivity.addedNetwork.secured, true)
            compare(connectivity.addedNetwork.password, "test-password")
            compare(page.addNetworkPending, false)

            mouseClick(add)
            prompts.respondFields("accepted", {ssid: "Open Hidden", security: "open", password: ""})
            compare(connectivity.addedNetwork.ssid, "Open Hidden")
            compare(connectivity.addedNetwork.secured, false)
            compare(connectivity.addedNetwork.password, "")
        }
        function test_savedWifiRemainsVisibleWithoutScanResults() {
            connectivity.networks = [
                {ssid: "Remembered", security: "WPA Personal", strength: 0,
                 connected: false, saved: true}
            ]
            app.connectivityBackend = connectivity
            page.selectTab(0)
            wait(350)
            const saved = findChild(page, "wifiNetwork_Remembered")
            verify(saved !== null)
            compare(findChild(page, "settingsRadioStatus").visible, false)
            mouseClick(saved)
            compare(connectivity.connectedSsid, "Remembered")

            connectivity.networks = [
                {ssid: "Remembered", security: "WPA Personal", strength: 0,
                 connecting: true, connected: false, saved: true}
            ]
            const connecting = findChild(page, "wifiNetwork_Remembered")
            compare(connecting.connecting, true)
            compare(connecting.height, 94)
            compare(connecting.enabled, false)

            connectivity.networks = [
                {ssid: "Remembered", security: "WPA Personal", strength: 0,
                 connected: true, saved: true}
            ]
            compare(findChild(page, "settingsRadioStatus").visible, false)
            compare(findChild(page, "wifiNetwork_Remembered").connected, true)
        }
        function test_wifiHeadingTogglesRadio() {
            app.connectivityBackend = connectivity
            page.selectTab(0)
            wait(350)
            let heading = findChild(page, "settingsWifiHeading")
            let radioSwitch = findChild(page, "settingsRadioSwitch")
            let add = findChild(page, "settingsAddNetwork")
            compare(radioSwitch.color.toString(), "#00d9e7")
            mouseClick(heading)
            compare(connectivity.wifiEnabled, false)
            compare(radioSwitch.color.toString(), "#777e83")
            compare(add.enabled, false)
            verify(findChild(page, "settingsRadioStatus").visible)
            mouseClick(heading)
            compare(connectivity.wifiEnabled, true)
            compare(radioSwitch.color.toString(), "#00d9e7")
            compare(add.enabled, true)
        }
        function test_bluetoothHeadingShowsDeviceNameAndSwitch() {
            app.connectivityBackend = connectivity
            page.selectTab(1)
            wait(350)
            const heading = findChild(page, "settingsWifiHeading")
            const radioSwitch = findChild(page, "settingsRadioSwitch")
            compare(radioSwitch.color.toString(), "#0759fa")
            backend.deviceName = "KB's Phone"
            verify(heading.contentItem.children[0].text.includes("KB's Phone"))
            mouseClick(heading)
            compare(connectivity.bluetoothEnabled, false)
            compare(radioSwitch.color.toString(), "#777e83")
            backend.deviceName = "KB Phone"
        }
        function test_savedWifiHoldShowsDetails() {
            connectivity.networks = [
                {ssid: "Saved Network", security: "WPA", strength: 65, connected: false, saved: true}
            ]
            app.connectivityBackend = connectivity
            page.selectTab(0)
            let strip = findChild(page, "settingsTabs")
            let mainPane = findChild(page, "settingsMainPane")
            let infoPane = findChild(page, "settingsWifiInfoPane")
            let saved = findChild(page, "wifiNetwork_Saved Network")
            verify(saved !== null)
            let stripX = strip.x
            mousePress(saved)
            wait(750)
            compare(page.wifiInfoOpen, true)
            compare(connectivity.requestedInfoSsid, "Saved Network")
            compare(strip.x, stripX)
            verify(strip.visible)
            mouseRelease(saved)
            compare(connectivity.connectedSsid, "")
            wait(330)
            compare(mainPane.x, -mainPane.width)
            compare(infoPane.x, 0)
            compare(findChild(page, "settingsWifiInfoSsid").text, "Saved Network")
            let password = findChild(page, "settingsWifiInfoPassword")
            compare(password.detail, "••••••••")
            mouseClick(password)
            compare(password.detail, "saved-secret")
            mouseClick(findChild(page, "settingsWifiInfoBack"))
            wait(330)
            compare(page.wifiInfoOpen, false)
            compare(mainPane.x, 0)
            compare(infoPane.x, infoPane.width)
            compare(connectivity.requestedInfoSsid, "")
        }
        function test_manualWifiIpAndDnsSettings() {
            verify(page.validIpAddress("2001:4860:4860::8888"))
            verify(page.validIpAddress("::1"))
            verify(!page.validIpAddress("2001:::1"))
            verify(!page.validIpAddress("999.1.1.1"))
            verify(!page.validIpAddress("001.1.1.1"))
            connectivity.networks = [
                {ssid: "Saved Network", security: "WPA", strength: 65, connected: false, saved: true}
            ]
            app.connectivityBackend = connectivity
            backend.reduceMotion = true
            page.selectTab(0)
            page.openSavedWifiInfo(connectivity.networks[0])
            let pane = findChild(page, "settingsWifiInfoPane")
            pane.contentY = 400
            mouseClick(findChild(page, "settingsWifiIpConfig"))
            let dialog = findChild(page, "settingsIpConfigDialog")
            tryCompare(dialog, "opened", true)
            wait(150)
            mouseClick(findChild(page, "settingsIpMode"))
            compare(dialog.manualIp, true)
            compare(dialog.manualDns, false)
            mouseClick(findChild(page, "settingsDnsMode"))
            compare(dialog.manualDns, true)
            compare(dialog.dnsServers.count, 1)
            verify(dialog.dnsRepeater.itemAt(0) !== null)
            let address = findChild(page, "settingsIpAddress")
            address.text = "invalid"
            findChild(page, "settingsIpPrefix").text = "24"
            findChild(page, "settingsIpGateway").text = "192.168.1.1"
            dialog.dnsRepeater.itemAt(0).field.text = "1.1.1.1"
            dialog.scrollView.contentY = dialog.scrollView.contentHeight - dialog.scrollView.height
            wait(50)
            mouseClick(findChild(page, "settingsAddDnsServer"))
            compare(dialog.dnsServers.count, 2)
            dialog.dnsRepeater.itemAt(1).field.text = "2001:4860:4860::8888"
            dialog.scrollView.contentY = dialog.scrollView.contentHeight - dialog.scrollView.height
            wait(50)
            mouseClick(findChild(page, "settingsAddDnsServer"))
            compare(dialog.dnsServers.count, 3)
            dialog.dnsRepeater.itemAt(2).field.text = "9.9.9.9"
            mouseClick(dialog.dnsRepeater.itemAt(2).removeButton)
            mouseClick(findChild(page, "settingsSaveIpConfig"))
            compare(dialog.opened, true)
            compare(Object.keys(connectivity.savedIpConfig).length, 0)
            address.text = "192.168.1.50"
            dialog.dnsRepeater.itemAt(1).field.text = "invalid-dns"
            mouseClick(findChild(page, "settingsSaveIpConfig"))
            compare(dialog.opened, true)
            compare(Object.keys(connectivity.savedIpConfig).length, 0)
            dialog.dnsRepeater.itemAt(1).field.text = "2001:4860:4860::8888"
            mouseClick(findChild(page, "settingsSaveIpConfig"))
            compare(dialog.visible, false)
            compare(connectivity.savedIpConfig.ssid, "Saved Network")
            compare(connectivity.savedIpConfig.manualIp, true)
            compare(connectivity.savedIpConfig.address, "192.168.1.50")
            compare(connectivity.savedIpConfig.prefix, 24)
            compare(connectivity.savedIpConfig.gateway, "192.168.1.1")
            compare(connectivity.savedIpConfig.manualDns, true)
            compare(connectivity.savedIpConfig.dns.length, 2)
            compare(connectivity.savedIpConfig.dns[1], "2001:4860:4860::8888")
            compare(connectivity.savedWifiInfo.configuredAddress, "192.168.1.50")

            page.openIpConfig()
            mouseClick(findChild(page, "settingsIpMode"))
            compare(dialog.manualIp, false)
            compare(dialog.manualDns, true)
            mouseClick(findChild(page, "settingsSaveIpConfig"))
            compare(connectivity.savedIpConfig.manualIp, false)
            compare(connectivity.savedIpConfig.manualDns, true)
            compare(connectivity.savedIpConfig.dns.length, 2)

            page.openIpConfig()
            mouseClick(findChild(page, "settingsDnsMode"))
            compare(dialog.manualDns, false)
            mouseClick(findChild(page, "settingsSaveIpConfig"))
            compare(connectivity.savedIpConfig.manualIp, false)
            compare(connectivity.savedIpConfig.manualDns, false)
            compare(connectivity.savedIpConfig.dns.length, 0)
        }
        function test_wifiScanWindowAndRefresh() {
            compare(page.wifiScanDurationMs, 30000)
            page.wifiScanDurationMs = 500
            page.wifiScanIntervalMs = 100
            app.connectivityBackend = connectivity
            let symbol = findChild(page, "settingsRefreshSymbol")
            page.selectTab(0)
            compare(page.wifiScanActive, true)
            compare(symbol.kind, "scan")
            compare(connectivity.scanCount, 1)
            wait(220)
            verify(connectivity.scanCount >= 2)
            wait(350)
            compare(page.wifiScanActive, false)
            compare(symbol.kind, "refresh")
            let completedScans = connectivity.scanCount
            wait(170)
            compare(connectivity.scanCount, completedScans)
            verify(findChild(page, "wifiNetwork_Open Network") !== null)
            mouseClick(findChild(page, "settingsRefresh"))
            compare(page.wifiScanActive, true)
            compare(symbol.kind, "scan")
            verify(connectivity.scanCount > completedScans)
            page.selectTab(1)
            compare(page.wifiScanActive, false)
            let beforeReentering = connectivity.scanCount
            page.selectTab(0)
            compare(page.wifiScanActive, true)
            compare(connectivity.scanCount, beforeReentering + 1)
        }
        function test_wifiWaitsForReadiness() {
            connectivity.wifiScanAvailable = false
            app.connectivityBackend = connectivity
            page.selectTab(0)
            compare(page.wifiScanActive, true)
            compare(connectivity.scanCount, 0)
            verify(connectivity.refreshCount > 0)
            connectivity.wifiScanAvailable = true
            connectivity.changed()
            compare(connectivity.scanCount, 1)
            connectivity.wifiScanAvailable = false
            connectivity.changed()
            connectivity.scanDeferred()
            compare(connectivity.scanCount, 1)
            connectivity.wifiScanAvailable = true
            connectivity.changed()
            compare(connectivity.scanCount, 2)
        }
        function test_wifiRefreshesAfterScanRequestCompletes() {
            page.wifiScanDurationMs = 250
            page.wifiRefreshIntervalMs = 100
            connectivity.networks = [
                {ssid: "KNETZ", security: "WPA Personal", strength: 0,
                 connected: false, saved: true}
            ]
            app.connectivityBackend = connectivity
            page.selectTab(0)
            const initialRefreshes = connectivity.refreshCount
            tryVerify(() => connectivity.refreshCount > initialRefreshes)
            connectivity.networks = [
                {ssid: "KNETZ", security: "WPA Personal", strength: 80,
                 connected: true, saved: true},
                {ssid: "Nearby", security: "Open", strength: 60,
                 connected: false, saved: false}
            ]
            tryVerify(() => findChild(page, "wifiNetwork_Nearby") !== null)
            compare(findChild(page, "wifiNetwork_KNETZ").connected, true)
            tryCompare(page, "wifiScanActive", false)
            const afterScan = connectivity.refreshCount
            tryVerify(() => connectivity.refreshCount > afterScan)
        }
        function test_tabLineCrossfade() {
            let oldLine = findChild(page, "settingsTabLine_bluetooth")
            let newLine = findChild(page, "settingsTabLine_wifi")
            tryCompare(oldLine, "opacity", 1)
            tryCompare(newLine, "opacity", 0)
            page.selectTab(0)
            wait(95)
            verify(oldLine.opacity > 0 && oldLine.opacity < 1)
            verify(newLine.opacity > 0 && newLine.opacity < 1)
            wait(220)
            compare(oldLine.opacity, 0)
            compare(newLine.opacity, 1)
        }
        function test_tabContentCrossfade() {
            let outgoing = findChild(page, "settingsOutgoingTabFrame")
            let incoming = findChild(page, "settingsMainPane")
            let tabs = findChild(page, "settingsTabs")
            compare(incoming.opacity, 1)
            mouseClick(findChild(page, "settingsTab_wifi"))
            tryCompare(page, "currentTab", 0)
            compare(tabs.visible, true)
            wait(70)
            verify(outgoing.visible)
            compare(outgoing.status, Image.Ready)
            verify(outgoing.opacity > 0 && outgoing.opacity < 1)
            verify(incoming.opacity > 0 && incoming.opacity < 1)
            wait(220)
            compare(outgoing.visible, false)
            compare(incoming.opacity, 1)

            backend.reduceMotion = true
            mouseClick(findChild(page, "settingsTab_bluetooth"))
            compare(page.currentTab, 1)
            compare(outgoing.visible, false)
            compare(incoming.opacity, 1)
        }
        function test_rapidTabSelectionKeepsLatestTab() {
            let outgoing = findChild(page, "settingsOutgoingTabFrame")
            mouseClick(findChild(page, "settingsTab_wifi"))
            mouseClick(findChild(page, "settingsTab_general"))
            tryCompare(page, "currentTab", 2)
            wait(290)
            compare(outgoing.visible, false)
            compare(page.tabIncomingOpacity, 1)
            compare(findChild(page, "settingsDeviceName").visible, true)
        }
        function openSettings() {
            page.selectTab(1)
        }
        function test_exitControls() {
            openSettings()
            compare(page.currentTab, 1)
            compare(findChild(page, "settingsRadioStatus").text, backend.bluetoothStatus)
            keyClick(Qt.Key_Back)
            compare(exitSpy.count, 1)
            compare(exitSpy.signalArguments[0][0], false)
            keyClick(Qt.Key_Home)
            compare(exitSpy.count, 1)
        }
        function test_bluetoothDevicesUseConnectivityService() {
            const path = "/org/bluez/hci0/dev_01_02_03_04_05_06"
            connectivity.bluetoothDevices = [{path: path, name: "Test headphones",
                alias: "Test headphones", address: "01:02:03:04:05:06",
                paired: false, connected: false}]
            app.connectivityBackend = connectivity
            page.selectTab(1)
            tryVerify(() => findChild(page, "bluetoothDevice_01:02:03:04:05:06") !== null)
            compare(findChild(page, "settingsRadioStatus").visible, false)
            waitForRendering(app.contentItem)
            wait(40)
            mouseClick(findChild(page, "bluetoothDevice_01:02:03:04:05:06"))
            compare(connectivity.bluetoothAction, "pair:" + path)
            compare(findChild(page, "bluetoothDevice_01:02:03:04:05:06").statusText, "Pairing…")
            connectivity.bluetoothActionState = "connecting"
            connectivity.bluetoothDevices = [Object.assign({}, connectivity.bluetoothDevices[0], {paired: true})]
            compare(page.rememberedBluetoothDevices.length, 1)
            compare(page.discoveredBluetoothDevices.length, 0)
            tryCompare(findChild(page, "bluetoothDevice_01:02:03:04:05:06"), "statusText", "Connecting…")
            connectivity.bluetoothActionState = ""
            connectivity.busy = false
            connectivity.bluetoothDevices = [Object.assign({}, connectivity.bluetoothDevices[0], {connected: true})]
            tryCompare(findChild(page, "bluetoothDevice_01:02:03:04:05:06"), "statusText", "Connected")
        }
        function test_bluetoothDiscoveryStartsWhenPowerBecomesReady() {
            page.stopBluetoothDiscovery()
            page.bluetoothScanStarted = false
            page.bluetoothUserDisabled = false
            connectivity.bluetoothEnabled = false
            app.connectivityBackend = connectivity
            page.selectTab(1)
            compare(connectivity.bluetoothAction, "power")
            compare(page.bluetoothScanWanted, true)
            compare(page.bluetoothScanRequested, false)
            connectivity.changed()
            compare(connectivity.bluetoothAction, "discover")
            compare(page.bluetoothScanRequested, true)
        }
        function bluetoothFixture() {
            connectivity.bluetoothDevices = [
                {path: "/org/bluez/hci0/dev_01_02_03_04_05_06", address: "01:02:03:04:05:06",
                 name: "WH-1000XM5", paired: true, connected: true},
                {path: "/org/bluez/hci0/dev_02_02_03_04_05_06", address: "02:02:03:04:05:06",
                 name: "JBL Go 4", paired: true, connected: false},
                {path: "/org/bluez/hci0/dev_03_02_03_04_05_06", address: "03:02:03:04:05:06",
                 name: "My VW 1841", paired: true, connected: false},
                {path: "/org/bluez/hci0/dev_04_02_03_04_05_06", address: "04:02:03:04:05:06",
                 name: "", paired: false, connected: false}]
            app.connectivityBackend = connectivity
            page.selectTab(1)
            wait(100)
        }
        function test_bluetoothGroupsAndHoldSlideWithoutDisconnect() {
            bluetoothFixture()
            let connected = findChild(page, "bluetoothDevice_01:02:03:04:05:06")
            let saved = findChild(page, "bluetoothDevice_02:02:03:04:05:06")
            let discovered = findChild(page, "bluetoothDevice_04:02:03:04:05:06")
            let divider = findChild(page, "settingsBluetoothSeparator")
            verify(connected.mapToItem(page, 0, 0).y < divider.mapToItem(page, 0, 0).y)
            verify(saved.mapToItem(page, 0, 0).y < divider.mapToItem(page, 0, 0).y)
            verify(discovered.mapToItem(page, 0, 0).y > divider.mapToItem(page, 0, 0).y)
            compare(saved.statusText, "")
            compare(discovered.deviceLabel, "04:02:03:04:05:06")
            mousePress(connected, connected.width / 2, connected.height / 2)
            wait(600)
            mouseRelease(connected, connected.width / 2, connected.height / 2)
            compare(page.bluetoothInfoOpen, true)
            compare(connectivity.bluetoothAction, "")
            tryCompare(findChild(page, "settingsBluetoothInfoPane"), "x", 0)
            tryCompare(findChild(page, "settingsMainPane"), "x", -672)
            compare(findChild(page, "settingsBluetoothInfoName").text, "WH-1000XM5")
            compare(findChild(page, "settingsBluetoothDisconnect").visible, true)
            compare(page.back(), true)
            tryCompare(findChild(page, "settingsMainPane"), "x", 0)
        }
        function test_bluetoothNicknameForgetAndDisconnect() {
            bluetoothFixture()
            page.openBluetoothInfo(connectivity.bluetoothDevices[0])
            tryCompare(findChild(page, "settingsBluetoothInfoPane"), "x", 0)
            findChild(page, "settingsBluetoothNickname").clicked()
            compare(prompts.kind, "input")
            compare(prompts.options.initialText, "WH-1000XM5")
            prompts.respond("cancelled", "Other")
            compare(connectivity.bluetoothAction, "")
            findChild(page, "settingsBluetoothNickname").clicked()
            prompts.respond("accepted", "My headphones")
            compare(connectivity.bluetoothAction, "rename:" + connectivity.bluetoothDevices[0].path + ":My headphones")
            connectivity.bluetoothAction = ""
            findChild(page, "settingsBluetoothForget").clicked()
            compare(prompts.kind, "confirmation")
            prompts.respond("cancelled", "")
            compare(connectivity.bluetoothAction, "")
            findChild(page, "settingsBluetoothDisconnect").clicked()
            compare(prompts.busy, false)
            compare(connectivity.bluetoothAction, "disconnect:" + connectivity.bluetoothDevices[0].path)
            connectivity.bluetoothAction = ""
            findChild(page, "settingsBluetoothForget").clicked()
            prompts.respond("accepted", "")
            compare(connectivity.bluetoothAction, "forget:" + connectivity.bluetoothDevices[0].path)
            compare(page.bluetoothInfoOpen, false)
        }
        function test_bluetoothStalePromptAndReducedMotion() {
            bluetoothFixture()
            backend.reduceMotion = true
            page.openBluetoothInfo(connectivity.bluetoothDevices[1])
            wait(40)
            compare(findChild(page, "settingsBluetoothInfoPane").x, 0)
            compare(findChild(page, "settingsBluetoothDisconnect").visible, false)
            compare(findChild(page, "settingsBluetoothConnect").visible, true)
            page.requestBluetoothPrompt("forget")
            connectivity.bluetoothDevices = []
            prompts.respond("accepted", "")
            compare(connectivity.bluetoothAction, "")
            compare(page.bluetoothInfoOpen, false)
            wait(40)
            compare(findChild(page, "settingsMainPane").x, 0)
        }
        function test_tabsSearchAndBack() {
            openSettings()
            mouseClick(findChild(page, "settingsTab_wifi"))
            tryCompare(page, "currentTab", 0)
            compare(findChild(page, "settingsRadioStatus").text, backend.wifiStatus)
            mouseClick(findChild(page, "settingsSearch"))
            compare(page.searching, true)
            let field = findChild(page, "settingsSearchField")
            field.text = "motion"
            compare(page.searchResults.length, 1)
            keyClick(Qt.Key_Return)
            tryCompare(page, "currentTab", 3)
            compare(page.searching, false)
            mouseClick(findChild(page, "settingsSearch"))
            field.text = "nonexistent setting"
            compare(page.searchResults.length, 0)
            keyClick(Qt.Key_Escape)
            compare(page.searching, false)
            compare(exitSpy.count, 0)
            keyClick(Qt.Key_Back)
            compare(exitSpy.count, 1)
            compare(exitSpy.signalArguments[0][0], false)
        }
        function test_renameAndPreferences() {
            app.connectivityBackend = connectivity
            openSettings()
            page.selectTab(2)
            wait(50)
            mouseClick(findChild(page, "settingsDeviceName"))
            compare(prompts.kind, "input")
            const pattern = new RegExp("^(?:" + prompts.options.validationPattern + ")$")
            verify(!pattern.test("   "))
            verify(pattern.test("Ocean Phone"))
            prompts.respond("accepted", "Ocean Phone")
            compare(backend.deviceName, "Ocean Phone")
            compare(backend.localHostname, "oceanphone.local")
            compare(connectivity.requestedDeviceName, "Ocean Phone")
            compare(findChild(page, "settingsLocalHostname"), null)
            compare(page.generalPromptAction, "")
            page.selectTab(3)
            wait(50)
            mouseClick(findChild(page, "settingsReduceMotion"))
            compare(backend.reduceMotion, true)
            mouseClick(findChild(page, "settingsLargeText"))
            compare(backend.largeText, true)
            openSettings()
            compare(page.deviceName, "Ocean Phone")
            compare(page.largeText, true)
        }
        function test_smallScreenAndTouchTabs() {
            app.width = 384
            app.height = 648
            waitForRendering(app.contentItem)
            openSettings()
            const strip = findChild(page, "settingsTabs")
            let touch = touchEvent(strip)
            touch.press(0, strip, 550, 40).commit()
            for (let x = 510; x >= 150; x -= 40) {
                touch.move(0, strip, x, 40).commit()
                wait(10)
            }
            touch.release(0, strip, 120, 40).commit()
            wait(300)
            verify(strip.contentX > 0)
            page.selectTab(3)
            wait(50)
            mouseClick(findChild(page, "settingsLargeText"))
            compare(page.largeText, true)
        }
    }
}
