import QtQuick

QtObject {
    property bool serviceAvailable: true
    property bool bluetoothAdapterAvailable: true
    property bool bluetoothEnabled: true
    property bool bluetoothDiscovering: false
    property bool busy: false
    property string bluetoothStatus: "No devices found. Tap refresh to search."
    property string bluetoothActionPath: ""
    property string bluetoothActionState: ""
    property var bluetoothDevices: []
    property string bluetoothAction: ""
    property var actions: []
    property int refreshCount: 0
    signal changed()
    signal errorOccurred(string message)

    function note(text) {
        bluetoothAction = text
        actions = actions.concat([text])
    }
    function refresh() { refreshCount += 1 }
    function setBluetoothEnabled(enabled) { bluetoothEnabled = enabled; note("power:" + enabled) }
    function startBluetoothDiscovery() { bluetoothDiscovering = true; note("discover") }
    function stopBluetoothDiscovery() { bluetoothDiscovering = false; note("stop") }
    function pairBluetoothDevice(path) {
        note("pair:" + path)
        bluetoothActionPath = path; bluetoothActionState = "pairing"; busy = true
    }
    function connectBluetoothDevice(path) {
        note("connect:" + path)
        bluetoothActionPath = path; bluetoothActionState = "connecting"; busy = true
    }
    function disconnectBluetoothDevice(path) { note("disconnect:" + path) }
    function renameBluetoothDevice(path, nickname) { note("rename:" + path + ":" + nickname) }
    function forgetBluetoothDevice(path) { note("forget:" + path) }
}
