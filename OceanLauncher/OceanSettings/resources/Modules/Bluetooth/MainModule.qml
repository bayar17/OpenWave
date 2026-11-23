import QtQuick
import QtQuick.Controls
import OpenWave.Theme 1.0
import OpenWave.Fonts 1.0
import OpenWave.Controls 1.0
import OceanSettings 1.0
import "Components"

Item {
    id: root
    objectName: "bluetoothModule"
    readonly property var backend: typeof Connectivity !== "undefined" ? Connectivity : null
    readonly property var prompts: typeof Prompts !== "undefined" ? Prompts : null
    readonly property var environment: typeof SettingsEnvironment !== "undefined" ? SettingsEnvironment : null
    readonly property bool largeText: !!environment && environment.largeText
    readonly property string deviceName: environment ? environment.deviceName : ""
    property int scanDurationMs: 30000
    property int refreshIntervalMs: 2000
    property string pendingAction: ""
    property string pendingPath: ""
    property string errorMessage: ""
    property bool scanWanted: false
    property bool scanRequested: false
    property bool scanStarted: false
    property bool userDisabled: false
    property bool infoOpen: false
    property string selectedPath: ""
    readonly property var devices: backend && backend.bluetoothDevices ? backend.bluetoothDevices : []
    readonly property var rememberedDevices: devices.filter(device => device.connected || device.paired || device.trusted)
    readonly property var discoveredDevices: devices.filter(device => !device.connected && !device.paired && !device.trusted)
    readonly property var selectedDevice: devices.find(device => device.path === selectedPath) || ({})
    readonly property string selectedName: selectedDevice.alias || selectedDevice.name || selectedDevice.address || ""
    readonly property bool discovering: !!backend && !!backend.bluetoothDiscovering
    readonly property bool adapterReady: !!backend && backend.serviceAvailable && backend.bluetoothAdapterAvailable
    readonly property bool radioOn: adapterReady && backend.bluetoothEnabled
    readonly property string statusText: backend ? backend.bluetoothStatus : qsTr("Bluetooth is unavailable.")

    function showError(message) {
        errorMessage = message
        errorTimer.restart()
    }

    function clearPending() {
        pendingAction = ""
        pendingPath = ""
    }

    function cancelPrompt() {
        const waiting = pendingAction.length > 0
        clearPending()
        if (waiting && prompts && prompts.busy)
            prompts.cancel()
    }

    function openInfo(device) {
        if (!backend || backend.busy || !device.path
                || !(device.connected || device.paired || device.trusted))
            return
        selectedPath = device.path
        infoOpen = true
    }

    function closeInfo() {
        const wasOpen = infoOpen
        infoOpen = false
        cancelPrompt()
        if (wasOpen)
            selectedPath = ""
    }

    function activate(device) {
        if (!backend || backend.busy)
            return
        if (device.connected)
            openInfo(device)
        else if (device.paired || device.trusted)
            backend.connectBluetoothDevice(device.path)
        else
            backend.pairBluetoothDevice(device.path)
    }

    function requestPrompt(action) {
        if (!backend || backend.busy || !selectedDevice.path || (prompts && prompts.busy))
            return
        if (!prompts) {
            showError(qsTr("The prompt service is unavailable."))
            return
        }
        pendingPath = selectedPath
        pendingAction = action
        if (action === "nickname")
            prompts.textInput(qsTr("Edit nickname"), "",
                {initialText: selectedName, placeholder: qsTr("type device nickname"),
                 acceptLabel: qsTr("Save"), cancelLabel: qsTr("Cancel"),
                 minLength: 1, maxLength: 248, validationPattern: "[^\\x00-\\x1f\\x7f]+"})
        else
            prompts.confirmation(qsTr("Forget device?"),
                qsTr("Forget “%1”? You will need to pair it again to reconnect.").arg(selectedName),
                {acceptLabel: qsTr("Forget"), cancelLabel: qsTr("Cancel")})
    }

    function refreshRadio() {
        if (!backend)
            return
        if (!backend.bluetoothAdapterAvailable) {
            backend.refresh()
            return
        }
        if (backend.bluetoothDiscovering) {
            scanTimeout.restart()
            backend.refresh()
        } else {
            scanStarted = false
            scanWanted = false
            maybeStartDiscovery()
        }
    }

    function maybeStartDiscovery() {
        if (!visible || scanWanted || scanStarted || userDisabled || !adapterReady)
            return
        scanWanted = true
        scanStarted = true
        scanRequested = false
        scanTimeout.restart()
        if (!backend.bluetoothEnabled)
            backend.setBluetoothEnabled(true)
        else if (!backend.bluetoothDiscovering) {
            scanRequested = true
            backend.startBluetoothDiscovery()
        }
    }

    function stopDiscovery() {
        const shouldStop = scanRequested || (backend && backend.bluetoothDiscovering)
        scanTimeout.stop()
        scanWanted = false
        scanRequested = false
        if (backend && shouldStop)
            backend.stopBluetoothDiscovery()
    }

    function handleBackendChanged() {
        if (infoOpen && !radioOn)
            closeInfo()
        if (visible) {
            maybeStartDiscovery()
            if (scanWanted && !scanRequested && backend && backend.bluetoothEnabled
                    && !backend.busy && !backend.bluetoothDiscovering) {
                scanRequested = true
                backend.startBluetoothDiscovery()
            }
        }
    }

    function back() {
        if (pendingAction.length > 0 && prompts && prompts.busy) {
            cancelPrompt()
            return true
        }
        if (infoOpen) {
            closeInfo()
            return true
        }
        return false
    }

    onSelectedDeviceChanged: {
        if (infoOpen && !selectedDevice.path)
            closeInfo()
    }

    onVisibleChanged: {
        if (visible) {
            scanStarted = false
            maybeStartDiscovery()
        } else {
            cancelPrompt()
            closeInfo()
            stopDiscovery()
        }
    }

    onBackendChanged: maybeStartDiscovery()

    Component.onCompleted: maybeStartDiscovery()

    Component.onDestruction: {
        cancelPrompt()
        stopDiscovery()
    }

    Timer {
        id: scanTimeout
        interval: root.scanDurationMs
        onTriggered: root.stopDiscovery()
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

    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onErrorOccurred(message) { root.showError(message) }
        function onChanged() { root.handleBackendChanged() }
    }

    Connections {
        target: root.prompts
        ignoreUnknownSignals: true
        function onFinished(requestId, result, text) {
            const action = root.pendingAction
            const path = root.pendingPath
            root.clearPending()
            if (result !== "accepted" || action.length === 0 || !root.infoOpen
                    || path !== root.selectedPath || !root.selectedDevice.path || !root.backend)
                return
            if (action === "nickname")
                root.backend.renameBluetoothDevice(path, text)
            else if (action === "forget") {
                root.backend.forgetBluetoothDevice(path)
                root.closeInfo()
            }
        }
    }

    Rectangle {
        visible: root.errorMessage.length > 0
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
            objectName: "bluetoothToastText"
            width: parent.width - 40
            anchors.centerIn: parent
            text: root.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Fonts.family
            font.pixelSize: 26
            color: "white"
        }
    }

    Flickable {
        id: scroller
        objectName: "bluetoothMainPane"
        anchors.fill: parent
        visible: !root.infoOpen
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
                    objectName: "bluetoothSwitch"
                    padding: 0
                    indicator: Item {}
                    checked: root.radioOn
                    Accessible.role: Accessible.CheckBox
                    Accessible.checked: checked
                    Accessible.name: root.radioOn ? qsTr("Turn off Bluetooth") : qsTr("Turn on Bluetooth")
                    x: 0
                    y: 0
                    width: Math.min(headingPrefix.implicitWidth + headingName.implicitWidth, parent.width - 176) + 120
                    height: 56
                    enabled: root.adapterReady
                    onToggled: {
                        const enable = checked
                        root.userDisabled = !enable
                        if (!enable)
                            root.stopDiscovery()
                        else {
                            root.scanStarted = false
                            root.scanWanted = false
                        }
                        root.backend.setBluetoothEnabled(enable)
                        checked = Qt.binding(() => root.radioOn)
                    }
                    contentItem: Item {
                        Row {
                            id: headingRow
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                id: headingPrefix
                                objectName: "bluetoothHeadingPrefix"
                                text: qsTr("Visible as ")
                                textFormat: Text.PlainText
                                font.family: Fonts.family
                                font.pixelSize: root.largeText ? 42 : 36
                                font.weight: Font.Light
                                color: "white"
                            }
                            Text {
                                id: headingName
                                objectName: "bluetoothHeadingName"
                                width: Math.min(implicitWidth, Math.max(0, scroller.width - 176 - headingPrefix.implicitWidth))
                                text: root.deviceName
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.family: Fonts.family
                                font.pixelSize: root.largeText ? 42 : 36
                                font.weight: Font.DemiBold
                                color: "white"
                            }
                        }
                        Rectangle {
                            id: radioSwitch
                            objectName: "bluetoothSwitchTrack"
                            x: headingPrefix.width + headingName.width + 20
                            anchors.verticalCenter: parent.verticalCenter
                            width: 100; height: 50; radius: 25
                            color: root.radioOn ? "#0759fa" : "#777e83"
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
                    objectName: "bluetoothRefresh"
                    anchors.right: parent.right
                    y: -6; width: 56; height: 56
                    Accessible.name: qsTr("Search for Bluetooth devices")
                    onClicked: root.refreshRadio()
                    contentItem: Item {
                        SettingsSymbol {
                            objectName: "bluetoothRefreshSymbol"
                            anchors.centerIn: parent
                            width: 44; height: 44
                            kind: root.discovering ? "scan" : "refresh"
                            color: "#cbd0d3"
                        }
                    }
                    background: Item {}
                }
            }

            Column {
                visible: root.radioOn
                width: parent.width
                Repeater {
                    model: root.rememberedDevices
                    DeviceRow {
                        required property var modelData
                        objectName: "bluetoothDevice_" + modelData.address
                        x: 47; width: parent.width - 67
                        device: modelData; largeText: root.largeText
                        actionState: root.backend.bluetoothActionPath === modelData.path
                            ? root.backend.bluetoothActionState : ""
                        enabled: !root.backend.busy
                        onClicked: root.activate(modelData)
                        onLongPressed: root.openInfo(modelData)
                    }
                }
                Item {
                    objectName: "bluetoothSeparator"
                    width: parent.width; height: 26
                    Rectangle {
                        x: 20; y: 8; width: parent.width - 40; height: 2
                        color: "#d4dadd"
                    }
                }
                Text {
                    objectName: "bluetoothDiscoveredHeading"
                    x: 20; width: parent.width - 40
                    height: root.largeText ? 76 : 66
                    text: qsTr("Discovered devices")
                    color: "white"; font.family: Fonts.family; font.weight: Font.ExtraLight
                    font.pixelSize: root.largeText ? 40 : 34
                }
                Repeater {
                    model: root.discoveredDevices
                    DeviceRow {
                        required property var modelData
                        objectName: "bluetoothDevice_" + modelData.address
                        x: 47; width: parent.width - 67
                        device: modelData; largeText: root.largeText
                        actionState: root.backend.bluetoothActionPath === modelData.path
                            ? root.backend.bluetoothActionState : ""
                        enabled: !root.backend.busy
                        onClicked: root.activate(modelData)
                    }
                }
            }
            Text {
                objectName: "bluetoothStatusText"
                visible: !root.radioOn || root.devices.length === 0
                width: parent.width - 94
                x: 47
                text: root.statusText
                textFormat: Text.PlainText
                color: "#d4dadd"
                font.family: Fonts.family
                font.pixelSize: root.largeText ? 34 : 28
                wrapMode: Text.Wrap
            }
        }
    }

    DeviceDetails {
        id: details
        objectName: "bluetoothInfoPane"
        anchors.fill: parent
        visible: root.infoOpen
        enabled: !(root.prompts && root.prompts.busy)
        device: root.selectedDevice
        deviceName: root.selectedName
        largeText: root.largeText
        busy: !!root.backend && root.backend.busy
        connecting: !!root.backend && root.backend.bluetoothActionPath === root.selectedPath
                    && root.backend.bluetoothActionState === "connecting"
        onBackRequested: root.closeInfo()
        onNicknameRequested: root.requestPrompt("nickname")
        onDisconnectRequested: root.backend.disconnectBluetoothDevice(root.selectedPath)
        onConnectRequested: root.backend.connectBluetoothDevice(root.selectedPath)
        onForgetRequested: root.requestPrompt("forget")
    }
}
