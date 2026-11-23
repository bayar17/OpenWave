import QtQuick

QtObject {
    property bool serviceAvailable: true
    property bool wifiDeviceAvailable: true
    property bool wifiEnabled: true
    property bool wifiScanAvailable: true
    property bool busy: false
    property string wifiState: "Disconnected"
    property string activeSsid: ""
    property string wifiStatus: "Ready"
    property var networks: []
    property var savedWifiInfo: ({})
    property bool savedWifiInfoLoading: false
    property string requestedInfoSsid: ""
    property string forgottenSsid: ""
    property var addedNetwork: ({})
    property var savedIpConfig: ({})
    property int refreshCount: 0
    property int scanCount: 0
    property int disconnectCount: 0
    property var connectCalls: []
    property var enableCalls: []
    signal changed()
    signal scanDeferred()
    signal errorOccurred(string message)
    signal configurationSaved(bool applied)
    signal networkAdded()

    function refresh() { refreshCount += 1 }
    function requestScan() { scanCount += 1 }
    function setWifiEnabled(enabled) { enableCalls = enableCalls.concat([enabled]) }
    function connectWifi(ssid, password) {
        connectCalls = connectCalls.concat([{ssid: ssid, password: password}])
    }
    function disconnectWifi() { disconnectCount += 1 }
    function addWifiNetwork(ssid, secured, password) {
        addedNetwork = ({ssid: ssid, secured: secured, password: password})
        networkAdded()
    }
    function forgetWifi(ssid) { forgottenSsid = ssid }
    function loadSavedWifiInfo(ssid) {
        requestedInfoSsid = ssid
        savedWifiInfo = ({ssid: ssid, security: "WPA Personal", password: "saved-secret",
                          autoConnect: true, connected: false, strength: 65, frequency: 2412,
                          bssid: "00:11:22:33:44:55"})
    }
    function clearSavedWifiInfo() {
        savedWifiInfo = ({})
        requestedInfoSsid = ""
    }
    function saveSavedWifiIpConfig(ssid, manualIp, address, prefix, gateway, manualDns, dns) {
        savedIpConfig = ({ssid: ssid, manualIp: manualIp, address: address,
                          prefix: prefix, gateway: gateway, manualDns: manualDns, dns: dns})
        savedWifiInfo = Object.assign({}, savedWifiInfo,
            {ipManual: manualIp, configuredAddress: address, configuredPrefix: prefix,
             configuredGateway: gateway, dnsManual: manualDns, configuredDns: dns})
        configurationSaved(true)
    }
}
