import QtQuick
import QtQuick.Window
import QtTest
import ".." as Ocean

Item {
    QtObject {
        id: backend
        property string deviceName: "KB Phone"
        property string localHostname: deviceName.toLowerCase().replace(/[^a-z0-9]/g, "") + ".local"
        property bool reduceMotion: false
        property bool largeText: false
        property bool powerActionsAvailable: true
        property var powerActions: []
        property string bluetoothStatus: "No Bluetooth adapter found."
        property string wifiStatus: "No Wi-Fi adapter found."
        property var systemInfo: ({os: "OpenWave", architecture: "x86_64", kernel: "test-kernel",
                                    processors: 4, storageTotal: 2147483648, storageAvailable: 1073741824})
        signal errorOccurred(string message)
        signal homeRequested()
        function saveDeviceName(name) { deviceName = name.trim(); return true }
        function setReduceMotion(value) { reduceMotion = value; return true }
        function setLargeText(value) { largeText = value; return true }
        function refreshRadios() {}
        function closeApp(home) { homeRequested() }
        function requestPowerAction(action) { powerActions = powerActions.concat([action]); return true }
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
        id: environment
        property string deviceName: backend.deviceName
        property bool largeText: backend.largeText
        property bool reduceMotion: backend.reduceMotion
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
        id: generalService
        property string deviceName: backend.deviceName
        property var systemInfo: backend.systemInfo
        property bool powerActionsAvailable: backend.powerActionsAvailable
        property bool powerBusy: false
        property bool timeServiceAvailable: timeBackend.serviceAvailable
        property bool timeBusy: timeBackend.busy
        property string timezone: timeBackend.timezone
        property var zones: timeBackend.zones
        signal changed()
        signal errorOccurred(string message)
        function saveDeviceName(name) {
            if (!backend.saveDeviceName(name))
                return false
            connectivity.setDeviceName(backend.deviceName)
            return true
        }
        function setTimezone(zone) {
            if (!timeBackend.serviceAvailable || timeBackend.zones.indexOf(zone) < 0)
                return false
            timeBackend.setTimezone(zone)
            return true
        }
        function requestPowerAction(action) { return backend.requestPowerAction(action) }
        function refreshTimeZones() { timeBackend.refresh() }
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
    QtObject {
        id: fileChooser
        property bool busy: false
        property int openCount: 0
        property int openPhotoCount: 0
        property var lastOptions: ({})
        signal completed(var grants)
        signal cancelled()
        signal errorOccurred(string message)
        function openFile(options) {
            openCount++
            lastOptions = options
            busy = true
        }
        function openPhoto(options) {
            openPhotoCount++
            lastOptions = Object.assign({}, options, {
                mimeTypes: ["image/*"], imageSource: "gallery"
            })
            busy = true
        }
    }
    QtObject {
        id: appearances
        property var presets: [
            {id: "space", name: "Space", user: false, favorite: false, hue: 0.58, thumbnail: "", wallpaper: "", scrim: "#80101418",
             activeColor: "#3a6ea5", inactiveColor: "#998a8d94", sliderTrack: "#59ffffff", sliderFill: "#ffffff",
             sliderHandle: "#e2e2e2", warningColor: "#bb4b56", topColor: "#202830", bottomColor: "#3a6ea5"},
            {id: "mine", name: "Mine", user: true, favorite: false, hue: 0.2, thumbnail: "", wallpaper: "", scrim: "#80101418",
             activeColor: "#a5803a", inactiveColor: "#998a8d94", sliderTrack: "#59ffffff", sliderFill: "#ffffff",
             sliderHandle: "#e2e2e2", warningColor: "#bb4b56", topColor: "#282420", bottomColor: "#a5803a"}
        ]
        property string current: "space"
        property bool busy: false
        property var calls: []
        signal finished(string operation, string id)
        signal failed(string operation, string message)
        function set(id) { calls = calls.concat([["set", id]]); current = id }
        function save(id, name, hue, image, x, y, width, height) {
            calls = calls.concat([["save", id, name, Math.round(hue * 1000) / 1000, image.toString(), x, y, width, height]])
        }
        function remove(id) { calls = calls.concat([["remove", id]]) }
        function setFavorite(id, value) {
            calls = calls.concat([["favorite", id, value]])
            presets = presets.map(item => item.id === id ? Object.assign({}, item, {favorite: value}) : item)
            return true
        }
        function accentForHue(hue) { return Qt.hsla(hue, 0.7, 0.4, 1) }
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
            app.showNormal()
            app.width = 768
            app.height = 1366
            app.connectivityBackend = null
            app.portalClient = fileChooser
            fileChooser.busy = false
            fileChooser.openCount = 0
            fileChooser.openPhotoCount = 0
            fileChooser.lastOptions = ({})
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
            appearances.calls = []
            appearances.current = "space"
            appearances.busy = false
            const appearanceHost = findChild(page, "settingsAppearancePageHost")
            appearanceHost.manager = appearances
            appearanceHost.editing = false
            appearanceHost.menuId = ""
            TestHooks.provide("Connectivity", connectivity)
            TestHooks.provide("Prompts", prompts)
            TestHooks.provide("SettingsEnvironment", environment)
            TestHooks.provide("General", generalService)
            connectivity.requestedDeviceName = ""
            connectivity.networks = [
                {ssid: "Open Network", security: "Open", strength: 80, connected: false, saved: false},
                {ssid: "Protected Network", security: "WPA", strength: 65, connected: false, saved: false}
            ]
            backend.reduceMotion = false
            backend.largeText = false
            backend.deviceName = "KB Phone"
            backend.powerActionsAvailable = true
            backend.powerActions = []
            page.selectTab(page.accessibilityTab)
            findChild(page, "settingsTabs").contentX = 0
            exitSpy.clear()
            wait(50)
        }

        function wifiTab() {
            return page.tabs.indexOf("Wi-Fi")
        }
        function clickWifiTab() {
            const strip = findChild(page, "settingsTabs")
            strip.contentX = 0
            wait(30)
            mouseClick(findChild(page, "settingsTab_Wi-Fi"))
        }
        function bluetoothTab() {
            return page.tabs.indexOf("Bluetooth")
        }
        function bluetoothHost() {
            return findChild(page, "settingsModuleHost")
        }
        function generalTab() {
            return page.tabs.indexOf("General")
        }
        function wifiHost() {
            return findChild(page, "settingsModuleHost")
        }
        function test_root_back_minimizes_and_keeps_settings_alive() {
            page.selectTab(page.accessibilityTab)
            app.requestActivate()
            wait(30)
            try {
                keyClick(Qt.Key_Back)
                tryCompare(app, "visibility", Window.Minimized)
                compare(page.currentTab, page.accessibilityTab)
                compare(backend.deviceName, "KB Phone")
            } finally {
                app.showNormal()
            }
        }
        function test_appsTabAndBackNavigation() {
            page.selectTab(page.appsTab)
            const apps = findChild(page, "settingsAppsPage")
            verify(apps.visible)
            apps.openApplication({appId: "browser", name: "Browser", trusted: false})
            verify(page.back()); compare(apps.depth, 0)
            page.selectTab(page.accessibilityTab)
            verify(!apps.visible)
            compare(apps.applicationId, "")
            verify(findChild(page, "settingsTab_apps"))
        }
        function appearanceHost() { return findChild(page, "settingsAppearancePageHost") }
        function openAppearanceTab() {
            app.width = 544
            app.height = 960
            wait(30)
            page.selectTab(page.appearanceTab)
            wait(400)
        }
        function longPress(item) {
            mousePress(item, item.width / 2, item.height / 2)
            wait(1000)
            mouseRelease(item, item.width / 2, item.height / 2)
        }
        function test_changeImageInTheAppearanceEditorOffersPhotosAndFiles() {
            page.selectTab(page.appearanceTab)
            app.width = 544
            app.height = 960
            wait(30)
            mouseClick(findChild(page, "settingsAppearanceAdd"), 40, 28)
            tryVerify(() => appearanceHost().editing && findChild(page, "settingsAppearanceChangeImage").visible)
            tryCompare(appearanceHost(), "slideProgress", 1)
            const edit = findChild(page, "settingsAppearanceChangeImage")
            const menu = findChild(page, "settingsWallpaperSourceMenu")
            const pane = findChild(page, "settingsPaneViewport")
            verify(edit !== null)
            verify(menu !== null)
            verify(pane !== null)
            mouseClick(edit, 40, 28)
            verify(menu.visible)
            wait(220)
            const paneOrigin = pane.mapToItem(menu.parent, 0, 0)
            const paneEnd = pane.mapToItem(menu.parent, pane.width, pane.height)
            const buttonAnchor = edit.mapToItem(menu.parent, edit.pressX, edit.pressY)
            verify(menu.revealProgress > 0.99)
            verify(menu.x >= paneOrigin.x)
            verify(menu.x + menu.width <= paneEnd.x)
            verify(menu.y >= paneOrigin.y)
            verify(menu.y + menu.height <= paneEnd.y)
            verify(menu.x <= buttonAnchor.x)
            verify(menu.x + menu.width >= buttonAnchor.x)
            verify(Math.abs(menu.x + menu.sourceX - buttonAnchor.x) < 2)
            verify(Math.abs(menu.y + menu.sourceY - buttonAnchor.y) < 2)
            const photos = findChild(page, "settingsWallpaperChoosePhotos")
            const files = findChild(page, "settingsWallpaperChooseFiles")
            photos.clicked()
            compare(fileChooser.openPhotoCount, 1)
            compare(fileChooser.openCount, 0)
            compare(fileChooser.lastOptions.mimeTypes, ["image/*"])
            compare(fileChooser.lastOptions.imageSource, "gallery")
            verify(!menu.visible)
            fileChooser.busy = false
            edit.clicked()
            verify(menu.visible)
            files.clicked()
            compare(fileChooser.openCount, 1)
            compare(fileChooser.openPhotoCount, 1)
            compare(fileChooser.lastOptions.imageSource, "files")
            verify(!menu.visible)
            app.width = 768
            app.height = 1366
        }
        function test_cropSlidesTheAppearancePageAndHandsTheCropToTheEditor() {
            page.selectTab(page.appearanceTab)
            let cropper = findChild(page, "settingsWallpaperCropper")
            let cropPage = findChild(page, "settingsCropPage")
            let viewport = findChild(page, "settingsCropViewport")
            let host = appearanceHost()
            verify(cropper !== null)
            verify(cropPage !== null)
            verify(viewport !== null)
            host.beginCreate()
            cropper.showCropImage(Qt.resolvedUrl("../assets/wallpaper.png"),
                                  {width: 768, height: 1366})
            compare(cropper.visible, true)
            compare(cropPage.y, 0)
            compare(cropPage.height, app.height)
            compare(viewport.y, page.topInset + 47)
            verify(Math.abs(viewport.width / viewport.height - 768 / 1366) < 0.001)
            tryCompare(cropper, "cropReady", true)
            compare(cropPage.x, 0)
            tryCompare(host, "x", -768)
            compare(findChild(page, "settingsCropApply").enabled, true)
            cropper.setZoom(1.5)
            compare(findChild(page, "settingsCropZoom").value, 1.5)
            verify(cropper.imageX < 0 && cropper.imageY < 0)
            findChild(page, "settingsCropApply").clicked()
            verify(host.editCrop !== null)
            verify(host.editCrop.width >= 767.5 && host.editCrop.height >= 1365.5)
            compare(host.editImage.toString().endsWith("wallpaper.png"), true)
            tryCompare(cropper, "visible", false)
            tryCompare(host, "x", 0)
        }
        function test_appearanceTabListsTheAppearancesAndMarksTheCurrentOne() {
            openAppearanceTab()
            verify(page.tabs.indexOf("appearance") >= 0)
            compare(page.tabs.indexOf("wallpaper"), -1)
            const space = findChild(page, "settingsAppearanceCard_space")
            const mine = findChild(page, "settingsAppearanceCard_mine")
            verify(space !== null)
            verify(mine !== null)
            compare(findChild(space, "settingsAppearanceName").text, "Space")
            compare(findChild(mine, "settingsAppearanceName").text, "Mine")
            compare(findChild(space, "settingsAppearanceSelection").opacity, 1)
            compare(findChild(mine, "settingsAppearanceSelection").opacity, 0)
            appearances.current = "mine"
            tryCompare(findChild(mine, "settingsAppearanceSelection"), "opacity", 1)
            tryCompare(findChild(space, "settingsAppearanceSelection"), "opacity", 0)
            compare(findChild(page, "settingsAppearanceEmpty").visible, false)
            verify(page.searchEntries.some(entry => entry.title === "Appearance" && entry.tab === page.appearanceTab))
            compare(page.searchEntries.filter(entry => entry.title === "Wallpaper").length, 0)
        }
        function test_tappingACardAppliesItImmediately() {
            openAppearanceTab()
            mouseClick(findChild(page, "settingsAppearanceCard_mine"))
            compare(appearances.calls, [["set", "mine"]])
            compare(appearances.current, "mine")
        }
        function menuEntry(key) { return findChild(findChild(page, "settingsAppearanceMenuLayer"), "settingsAppearanceMenu_" + key) }
        function test_holdingACardOpensTheMenuWithEveryAction() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_space"))
            compare(appearanceHost().menuId, "space")
            for (const key of ["set", "edit", "favorite", "delete"])
                verify(menuEntry(key) !== null)
            compare(menuEntry("set").modelData.available, true)
            compare(menuEntry("edit").modelData.available, false)
            compare(menuEntry("delete").modelData.available, false)
            compare(menuEntry("favorite").modelData.available, true)
            verify(page.back())
            compare(appearanceHost().menuId, "")
            tryCompare(findChild(page, "settingsAppearanceMenuLayer"), "visible", false)
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            compare(appearanceHost().menuId, "mine")
            compare(menuEntry("edit").modelData.available, true)
            compare(menuEntry("delete").modelData.available, true)
            menuEntry("set").clicked()
            compare(appearances.calls, [["set", "mine"]])
            compare(appearanceHost().menuId, "")
        }
        function test_theMenuDarkensTheBackgroundThenSlidesDownLikeADrawer() {
            openAppearanceTab()
            const layer = findChild(page, "settingsAppearanceMenuLayer")
            tryVerify(() => !layer.visible)
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            compare(layer.visible, true)
            tryVerify(() => layer.dim > 0.99)
            verify(findChild(page, "settingsAppearanceDim").opacity > 0.4)
            tryVerify(() => layer.drawer > 0.99)
            const drawer = findChild(page, "settingsAppearanceMenu")
            verify(drawer.height > 200)
            tryVerify(() => layer.itemsOpacity > 0.99)
            const card = findChild(page, "settingsAppearanceMenuCard")
            verify(card !== null)
            verify(drawer.mapToItem(null, 0, 0).y > card.mapToItem(null, 0, 0).y)
            page.back()
            tryVerify(() => layer.drawer < 0.01)
            tryVerify(() => !layer.visible)
            compare(layer.dim, 0)
        }
        function test_theCardOptionsButtonOpensTheMenuWithoutApplyingTheAppearance() {
            openAppearanceTab()
            const button = findChild(findChild(page, "settingsAppearanceCard_mine"), "settingsAppearanceMenuButton")
            verify(button !== null)
            mouseClick(button)
            compare(appearanceHost().menuId, "mine")
            compare(appearances.calls, [])
            tryVerify(() => findChild(page, "settingsAppearanceMenuLayer").dim > 0.99)
        }
        function test_tappingTheDimmedBackgroundClosesTheMenu() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            tryVerify(() => findChild(page, "settingsAppearanceMenuLayer").dim > 0.99)
            mouseClick(findChild(page, "settingsAppearanceMenuDismiss"), 4, 4)
            compare(appearanceHost().menuId, "")
            compare(appearances.calls, [])
        }
        function test_favoritingFromTheMenuTogglesTheMark() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_space"))
            compare(menuEntry("favorite").contentItem.text, "Add to favorite")
            menuEntry("favorite").clicked()
            compare(appearances.calls, [["favorite", "space", true]])
            compare(appearanceHost().menuId, "")
            tryVerify(() => findChild(findChild(page, "settingsAppearanceCard_space"), "settingsAppearanceFavoriteMark").visible)
            longPress(findChild(page, "settingsAppearanceCard_space"))
            compare(menuEntry("favorite").contentItem.text, "Remove from favorite")
            menuEntry("favorite").clicked()
            compare(appearances.calls[1], ["favorite", "space", false])
            tryVerify(() => !findChild(findChild(page, "settingsAppearanceCard_space"), "settingsAppearanceFavoriteMark").visible)
        }
        function test_editAndDeleteDoNothingForSystemAppearances() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_space"))
            menuEntry("edit").clicked()
            menuEntry("delete").clicked()
            compare(appearanceHost().editing, false)
            compare(prompts.kind, "")
            compare(appearanceHost().menuId, "space")
        }
        function test_deletingAnAppearanceAsksFirst() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            menuEntry("delete").clicked()
            compare(prompts.kind, "confirmation")
            compare(appearances.calls, [])
            prompts.finished("r1", "cancelled", "")
            compare(appearances.calls, [])
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            menuEntry("delete").clicked()
            prompts.finished("r2", "accepted", "")
            compare(appearances.calls, [["remove", "mine"]])
        }
        function test_aNewAppearanceNeedsANameAndAnImage() {
            openAppearanceTab()
            mouseClick(findChild(page, "settingsAppearanceAdd"))
            const host = appearanceHost()
            verify(host.editing)
            tryCompare(host, "slideProgress", 1)
            compare(findChild(page, "settingsAppearanceEditorTitle").text, "New Appearance")
            const save = findChild(page, "settingsAppearanceSave")
            compare(save.enabled, false)
            host.editName = "Autumn"
            compare(save.enabled, false)
            host.cropChosen(Qt.resolvedUrl("../assets/wallpaper.png"), -10, -20, 800, 1400)
            compare(save.enabled, true)
            host.editHue = 0.25
            save.clicked()
            compare(appearances.calls.length, 1)
            compare(appearances.calls[0].slice(0, 4), ["save", "", "Autumn", 0.25])
            verify(appearances.calls[0][4].endsWith("wallpaper.png"))
            compare(appearances.calls[0].slice(5), [-10, -20, 800, 1400])
            appearances.finished("save", "autumn")
            compare(host.editing, false)
            tryCompare(host, "slideProgress", 0)
        }
        function test_editingKeepsTheImageUnlessItWasChanged() {
            openAppearanceTab()
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            menuEntry("edit").clicked()
            const host = appearanceHost()
            verify(host.editing)
            compare(host.editId, "mine")
            compare(host.editName, "Mine")
            compare(host.editHue, 0.2)
            compare(findChild(page, "settingsAppearanceEditorTitle").text, "Edit Appearance")
            compare(findChild(page, "settingsAppearanceNameField").text, "Mine")
            const save = findChild(page, "settingsAppearanceSave")
            compare(save.enabled, true)
            save.clicked()
            compare(appearances.calls[0].slice(0, 5), ["save", "mine", "Mine", 0.2, ""])
        }
        function test_theHueSliderSetsTheHueAndTheAccentFollowsIt() {
            openAppearanceTab()
            appearanceHost().beginCreate()
            tryCompare(appearanceHost(), "slideProgress", 1)
            const track = findChild(page, "settingsAppearanceHue")
            mouseClick(track, track.width * 0.25, track.height / 2)
            verify(Math.abs(appearanceHost().editHue - 0.25) < 0.01)
            const first = appearanceHost().editAccent.toString()
            mouseClick(track, track.width * 0.8, track.height / 2)
            verify(Math.abs(appearanceHost().editHue - 0.8) < 0.01)
            verify(first !== appearanceHost().editAccent.toString())
            mouseClick(track, 0, track.height / 2)
            verify(appearanceHost().editHue < 0.01)
            mouseClick(track, track.width, track.height / 2)
            verify(appearanceHost().editHue > 0.99)
        }
        function test_theQuickMenuRowOpensTheQuickMenuPageInsideAppearance() {
            openAppearanceTab()
            const host = appearanceHost()
            const currentTab = page.currentTab
            mouseClick(findChild(page, "settingsAppearanceQuickMenu"))
            verify(host.quickMenuOpen)
            tryCompare(host, "quickMenuProgress", 1)
            compare(page.currentTab, currentTab)
            compare(findChild(page, "settingsQuickMenuTitle").text, "Quick Menu")
            verify(findChild(page, "quickMenuPage") !== null)
            verify(page.back())
            compare(host.quickMenuOpen, false)
            tryCompare(host, "quickMenuProgress", 0)
            compare(page.back(), false)
        }
        function test_theQuickMenuBackArrowReturnsToTheAppearanceList() {
            openAppearanceTab()
            const host = appearanceHost()
            host.openQuickMenu()
            tryCompare(host, "quickMenuProgress", 1)
            mouseClick(findChild(page, "settingsQuickMenuBack"))
            compare(host.quickMenuOpen, false)
            tryCompare(host, "quickMenuProgress", 0)
            verify(findChild(page, "settingsAppearanceList").visible)
        }
        function test_quickMenuIsNotATopLevelTab() {
            compare(page.tabs.indexOf("Quick Menu"), -1)
            verify(page.tabs.indexOf("appearance") >= 0)
        }
        function test_backClosesTheMenuThenTheEditor() {
            openAppearanceTab()
            const host = appearanceHost()
            longPress(findChild(page, "settingsAppearanceCard_mine"))
            verify(host.menuId.length > 0)
            verify(page.back())
            compare(host.menuId, "")
            host.beginCreate()
            verify(page.back())
            compare(host.editing, false)
            compare(page.back(), false)
        }
        function test_failuresAreReportedToTheUser() {
            openAppearanceTab()
            const spy = createTemporaryObject(messageSpyComponent, page, {target: appearanceHost()})
            appearances.failed("save", "The name is invalid.")
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "The name is invalid.")
            page.showMessage("")
        }
        Component {
            id: messageSpyComponent
            SignalSpy { signalName: "message" }
        }
        function test_settingsHasExactlyOneWifiEntryFromTheRegistry() {
            const wifiNames = page.tabs.filter(name => name.toLowerCase().replace(/[^a-z]/g, "") === "wifi")
            compare(wifiNames.length, 1)
            compare(page.builtinTabs.indexOf("wifi"), -1)
            compare(findChild(page, "settingsTab_wifi"), null)
            verify(findChild(page, "settingsTab_Wi-Fi") !== null)
            compare(page.moduleEntries.length, 3)
            compare(page.moduleEntries.filter(entry => entry.id === "com.openwave.settings.wifi").length, 1)
            compare(page.moduleEntries.filter(entry => entry.name === "Wi-Fi").length, 1)
            compare(page.tabs[0], "Wi-Fi")
            compare(page.tabEntries[0].moduleId, "com.openwave.settings.wifi")
            compare(wifiTab(), 0)
            compare(page.tabs.slice(1).join(","), "Bluetooth,General,accessibility,appearance,apps")
            const wifiSearch = page.searchEntries.filter(entry => entry.title === "Wi-Fi")
            compare(wifiSearch.length, 1)
            compare(wifiSearch[0].key, "com.openwave.settings.wifi")
            compare(wifiSearch[0].tab, wifiTab())
        }
        function test_noBuiltInWifiPageRemains() {
            for (const name of ["wifiInfoOpen", "wifiScanActive", "wifiInfo", "selectedWifiSsid", "addNetworkPending",
                                "wifiPromptAction", "openSavedWifiInfo", "requestWifiPassword", "openAddNetwork",
                                "openIpConfig", "startWifiScan"])
                compare(page[name], undefined)
            for (const name of ["settingsWifiHeading", "settingsAddNetwork", "settingsWifiInfoPane", "settingsWifiIpConfig",
                                "settingsForgetWifi", "settingsTab_wifi", "wifiNetwork_Open Network"])
                compare(findChild(page, name), null)
        }
        function test_otherThreeBuiltInTabsRemain() {
            compare(page.builtinTabs.length, 3)
            compare(page.builtinTabs.join(","), "accessibility,appearance,apps")
            compare(page.tabs.length, 6)
            const selected = [page.accessibilityTab, page.appearanceTab, page.appsTab]
            for (const index of selected) {
                page.selectTab(index)
                compare(page.currentTab, index)
                compare(page.moduleTabActive, false)
                compare(wifiHost().loaded, false)
            }
            for (const name of page.builtinTabs)
                verify(findChild(page, "settingsTab_" + name) !== null)
        }
        function test_bluetoothIsTheSecondTabFromTheRegistryAndHasNoBuiltInDuplicate() {
            const names = page.tabs.filter(name => name.toLowerCase() === "bluetooth")
            compare(names.length, 1)
            compare(page.tabs[1], "Bluetooth")
            compare(page.tabEntries[1].moduleId, "com.openwave.settings.bluetooth")
            compare(page.builtinTabs.indexOf("bluetooth"), -1)
            compare(findChild(page, "settingsTab_bluetooth"), null)
            verify(findChild(page, "settingsTab_Bluetooth") !== null)
            const search = page.searchEntries.filter(entry => entry.title === "Bluetooth")
            compare(search.length, 1)
            compare(search[0].key, "com.openwave.settings.bluetooth")
            compare(search[0].tab, bluetoothTab())
            for (const name of ["bluetoothInfoOpen", "bluetoothDevices", "selectedBluetoothPath", "bluetoothScanWanted",
                                "openBluetoothInfo", "requestBluetoothPrompt", "maybeStartBluetoothDiscovery", "refreshRadio"])
                compare(page[name], undefined)
            for (const name of ["settingsBluetoothHeading", "settingsBluetoothInfoPane", "settingsBluetoothSeparator",
                                "settingsRadioSwitch", "settingsRefresh"])
                compare(findChild(page, name), null)
        }
        function test_selectingBluetoothLoadsTheRegistryModule() {
            page.selectTab(bluetoothTab())
            compare(page.activeModuleId, "com.openwave.settings.bluetooth")
            tryCompare(bluetoothHost(), "loaded", true)
            verify(findChild(page, "bluetoothModule") !== null)
            verify(findChild(page, "bluetoothSwitch") !== null)
            compare(findChild(page, "settingsMainPane").visible, false)
        }
        function test_leavingBluetoothStopsDiscoveryAndCancelsPrompts() {
            connectivity.bluetoothDevices = [
                {path: "/org/bluez/hci0/dev_01_02_03_04_05_06", address: "01:02:03:04:05:06",
                 name: "WH-1000XM5", paired: true, connected: true}]
            page.selectTab(bluetoothTab())
            tryCompare(bluetoothHost(), "loaded", true)
            tryVerify(() => connectivity.bluetoothDiscovering)
            const module = findChild(page, "bluetoothModule")
            module.openInfo(connectivity.bluetoothDevices[0])
            module.requestPrompt("forget")
            compare(prompts.busy, true)
            page.selectTab(page.accessibilityTab)
            compare(prompts.busy, false)
            compare(connectivity.bluetoothDiscovering, false)
            tryVerify(() => findChild(page, "bluetoothModule") === null)
        }
        function test_switchingBetweenWifiAndBluetoothModules() {
            for (const index of [wifiTab(), bluetoothTab(), wifiTab(), bluetoothTab()]) {
                page.selectTab(index)
                tryCompare(wifiHost(), "loaded", true)
                compare(wifiHost().loader.activeModuleId, page.tabEntries[index].moduleId)
            }
            tryVerify(() => findChild(page, "wifiModule") === null)
        }
        function test_generalIsTheThirdTabFromTheRegistryAndHasNoBuiltInPage() {
            compare(page.tabs.join(","), "Wi-Fi,Bluetooth,General,accessibility,appearance,apps")
            compare(page.tabEntries[2].moduleId, "com.openwave.settings.general")
            compare(page.builtinTabs.indexOf("general"), -1)
            compare(findChild(page, "settingsTab_general"), null)
            verify(findChild(page, "settingsTab_General") !== null)
            for (const name of ["generalTab", "generalPromptAction", "editName", "editTimezone", "requestPowerAction", "timeBackend", "deviceName"])
                compare(page[name], undefined)
            for (const name of ["settingsGeneralPage", "settingsGeneralCategories", "settingsDeviceName", "settingsRestart",
                                "settingsShutdown", "settingsTimezone", "settingsAccentPalette", "settingsGeneral_about"])
                compare(findChild(page, name), null)
        }
        function test_generalSearchEntriesComeFromTheModuleAndSkipAppearance() {
            const titles = page.searchEntries.map(entry => entry.title)
            for (const title of ["General", "About", "Device name", "Time zone", "Storage", "Locale & Languages", "Power options"])
                verify(titles.indexOf(title) >= 0, title)
            compare(page.searchEntries.filter(entry => entry.tab === generalTab()).map(entry => entry.title).indexOf("Appearance"), -1)
            const storage = page.searchEntries.filter(entry => entry.title === "Storage")
            compare(storage.length, 1)
            compare(storage[0].tab, generalTab())
            compare(storage[0].entryKey, "storage")
        }
        function test_generalSearchOpensCategoryInsideTheModule() {
            mouseClick(findChild(page, "settingsSearch"))
            findChild(page, "settingsSearchField").text = "Storage"
            compare(page.searchResults.length, 1)
            keyClick(Qt.Key_Return)
            tryCompare(page, "currentTab", generalTab())
            tryCompare(wifiHost(), "loaded", true)
            tryCompare(findChild(page, "generalModule"), "category", "storage")
            compare(page.searching, false)
        }
        function test_deviceNameSearchOpensTheNamePrompt() {
            mouseClick(findChild(page, "settingsSearch"))
            findChild(page, "settingsSearchField").text = "Device name"
            compare(page.searchResults.length, 1)
            keyClick(Qt.Key_Return)
            tryCompare(page, "currentTab", generalTab())
            tryCompare(findChild(page, "generalModule"), "category", "about")
            compare(prompts.kind, "input")
            compare(prompts.title, "Device name")
            compare(prompts.options.initialText, "KB Phone")
        }
        function test_generalModuleHandlesBackAndCleansUpOnLeaving() {
            page.selectTab(generalTab())
            tryCompare(wifiHost(), "loaded", true)
            const module = findChild(page, "generalModule")
            module.openCategory("about")
            module.editName()
            compare(prompts.busy, true)
            verify(page.back())
            compare(prompts.busy, false)
            compare(module.category, "about")
            module.editName()
            page.selectTab(page.accessibilityTab)
            compare(prompts.busy, false)
            compare(wifiHost().loaded, false)
            prompts.respond("accepted", "Stale Name")
            compare(backend.deviceName, "KB Phone")
            tryVerify(() => findChild(page, "generalModule") === null)
            page.selectTab(generalTab())
            tryCompare(wifiHost(), "loaded", true)
            compare(findChild(page, "generalModule").category, "")
        }
        function test_powerActionsGoThroughTheServiceAfterConfirmation() {
            backend.reduceMotion = true
            page.selectTab(generalTab())
            tryCompare(wifiHost(), "loaded", true)
            findChild(page, "generalModule").openCategory("power")
            tryCompare(findChild(page, "generalDetails"), "enabled", true)
            wait(40)
            mouseClick(findChild(page, "generalRestart"))
            compare(prompts.kind, "confirmation")
            compare(backend.powerActions.length, 0)
            prompts.respond("accepted", "")
            compare(backend.powerActions, ["restart"])
            mouseClick(findChild(page, "generalShutdown"))
            prompts.respond("cancelled", "")
            compare(backend.powerActions, ["restart"])
            backend.powerActionsAvailable = false
            verify(!findChild(page, "generalRestart").enabled)
        }
        function test_timeZoneChangesGoThroughTheService() {
            timeBackend.timezone = "Europe/Istanbul"
            backend.reduceMotion = true
            page.selectTab(generalTab())
            tryCompare(wifiHost(), "loaded", true)
            findChild(page, "generalModule").openCategory("locale")
            tryCompare(findChild(page, "generalDetails"), "enabled", true)
            wait(40)
            mouseClick(findChild(page, "generalTimezone"))
            compare(prompts.kind, "selection")
            prompts.respond("accepted", "Europe/London")
            compare(timeBackend.timezone, "Europe/London")
            compare(findChild(page, "generalTimezone").detail, "Europe/London")
        }
        function test_selectingWifiLoadsTheRegistryModule() {
            compare(wifiHost().loaded, false)
            page.selectTab(wifiTab())
            compare(page.moduleTabActive, true)
            compare(page.activeModuleId, "com.openwave.settings.wifi")
            tryCompare(wifiHost(), "loaded", true)
            compare(wifiHost().loader.activeModuleId, "com.openwave.settings.wifi")
            verify(findChild(page, "wifiModule") !== null)
            verify(findChild(page, "wifiSwitch") !== null)
            compare(findChild(page, "settingsMainPane").visible, false)
            compare(findChild(page, "settingsAppsPage").visible, false)
        }
        function test_navigatingBetweenBuiltInTabsAndWifiLoadsAndUnloads() {
            for (const index of [page.accessibilityTab, wifiTab(), page.appsTab, bluetoothTab(), generalTab(), wifiTab(), page.appearanceTab]) {
                page.selectTab(index)
                compare(page.currentTab, index)
                if (index === wifiTab() || index === bluetoothTab() || index === generalTab())
                    tryCompare(wifiHost(), "loaded", true)
                else
                    compare(wifiHost().loaded, false)
            }
            tryVerify(() => findChild(page, "wifiModule") === null)
        }
        function test_leavingWifiCleansUpPromptsScansAndPolling() {
            connectivity.networks = [
                {ssid: "Protected Network", security: "WPA", strength: 65, connected: false, saved: false}
            ]
            page.selectTab(wifiTab())
            tryCompare(wifiHost(), "loaded", true)
            tryVerify(() => connectivity.scanCount > 0)
            wait(80)
            mouseClick(findChild(page, "wifiNetwork_Protected Network"))
            compare(prompts.kind, "password")
            compare(prompts.busy, true)
            const moduleItem = findChild(page, "wifiModule")
            page.selectTab(page.accessibilityTab)
            compare(prompts.busy, false)
            compare(wifiHost().loaded, false)
            tryVerify(() => findChild(page, "wifiModule") === null)
            const scans = connectivity.scanCount
            const refreshes = connectivity.refreshCount
            wait(1700)
            compare(connectivity.scanCount, scans)
            compare(connectivity.connectedSsid, "")
            verify(connectivity.refreshCount - refreshes <= 2)
        }
        function test_wifiModuleHandlesBackBeforeSettings() {
            connectivity.networks = [
                {ssid: "Saved Network", security: "WPA", strength: 65, connected: false, saved: true}
            ]
            page.selectTab(wifiTab())
            tryCompare(wifiHost(), "loaded", true)
            const module = findChild(page, "wifiModule")
            module.openSavedInfo(connectivity.networks[0])
            compare(module.infoOpen, true)
            verify(page.back())
            compare(module.infoOpen, false)
            compare(page.back(), false)
            page.selectTab(page.accessibilityTab)
            compare(page.back(), false)
        }
        function test_wifiSearchResultOpensTheModule() {
            mouseClick(findChild(page, "settingsSearch"))
            compare(page.searching, true)
            const field = findChild(page, "settingsSearchField")
            field.text = "wi-fi"
            compare(page.searchResults.length, 1)
            compare(page.searchResults[0].key, "com.openwave.settings.wifi")
            keyClick(Qt.Key_Return)
            tryCompare(page, "currentTab", wifiTab())
            compare(page.searching, false)
            tryCompare(wifiHost(), "loaded", true)
        }
        function test_wifiErrorsAreShownOnceByTheModule() {
            app.connectivityBackend = connectivity
            page.selectTab(wifiTab())
            tryCompare(wifiHost(), "loaded", true)
            connectivity.errorOccurred("Association failed")
            const banner = findChild(page, "wifiErrorBanner")
            verify(banner.visible)
            compare(findChild(page, "wifiErrorText").text, "Association failed")
            compare(findChild(page, "settingsNotice").text, "")
        }
        function test_tabLineCrossfade() {
            let oldLine = findChild(page, "settingsTabLine_accessibility")
            let newLine = findChild(page, "settingsTabLine_Wi-Fi")
            tryCompare(oldLine, "opacity", 1)
            tryCompare(newLine, "opacity", 0)
            page.selectTab(wifiTab())
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
            clickWifiTab()
            tryCompare(page, "currentTab", wifiTab())
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
            tabs.contentX = 0
            wait(30)
            mouseClick(findChild(page, "settingsTab_Bluetooth"))
            compare(page.currentTab, bluetoothTab())
            compare(outgoing.visible, false)
            compare(incoming.opacity, 1)
        }
        function test_rapidTabSelectionKeepsLatestTab() {
            let outgoing = findChild(page, "settingsOutgoingTabFrame")
            clickWifiTab()
            const strip = findChild(page, "settingsTabs")
            strip.contentX = 0
            wait(30)
            const general = findChild(page, "settingsTab_General")
            const generalRight = general.mapToItem(strip, general.width, 0).x
            if (generalRight > strip.width)
                strip.contentX = generalRight - strip.width + 20
            wait(30)
            mouseClick(general)
            tryCompare(page, "currentTab", generalTab())
            wait(290)
            compare(outgoing.visible, false)
            compare(page.tabIncomingOpacity, 1)
            tryCompare(wifiHost(), "loaded", true)
            compare(findChild(page, "generalCategory_about").visible, true)
        }
        function openSettings() {
            page.selectTab(page.accessibilityTab)
        }
        function test_exitControls() {
            openSettings()
            compare(page.currentTab, page.accessibilityTab)
            keyClick(Qt.Key_Back)
            compare(exitSpy.count, 1)
            compare(exitSpy.signalArguments[0][0], false)
            keyClick(Qt.Key_Home)
            compare(exitSpy.count, 1)
        }
        function test_tabsSearchAndBack() {
            openSettings()
            clickWifiTab()
            tryCompare(page, "currentTab", wifiTab())
            tryCompare(findChild(page, "settingsModuleHost"), "loaded", true)
            mouseClick(findChild(page, "settingsSearch"))
            compare(page.searching, true)
            let field = findChild(page, "settingsSearchField")
            field.text = "motion"
            compare(page.searchResults.length, 1)
            keyClick(Qt.Key_Return)
            tryCompare(page, "currentTab", page.accessibilityTab)
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
            page.selectTab(generalTab())
            tryCompare(wifiHost(), "loaded", true)
            findChild(page, "generalModule").openCategory("about")
            wait(260)
            mouseClick(findChild(page, "generalDeviceName"))
            compare(prompts.kind, "input")
            const pattern = new RegExp("^(?:" + prompts.options.validationPattern + ")$")
            verify(!pattern.test("   "))
            verify(pattern.test("Ocean Phone"))
            prompts.respond("accepted", "Ocean Phone")
            compare(backend.deviceName, "Ocean Phone")
            compare(backend.localHostname, "oceanphone.local")
            compare(connectivity.requestedDeviceName, "Ocean Phone")
            compare(findChild(page, "settingsLocalHostname"), null)
            compare(prompts.busy, false)
            page.selectTab(page.accessibilityTab)
            wait(50)
            mouseClick(findChild(page, "settingsReduceMotion"))
            compare(backend.reduceMotion, true)
            mouseClick(findChild(page, "settingsLargeText"))
            compare(backend.largeText, true)
            openSettings()
            compare(environment.deviceName, "Ocean Phone")
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
            page.selectTab(page.accessibilityTab)
            wait(50)
            mouseClick(findChild(page, "settingsLargeText"))
            compare(page.largeText, true)
        }
    }
}
