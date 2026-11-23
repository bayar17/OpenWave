import QtQuick

QtObject {
    property string deviceName: "KB Phone"
    property var systemInfo: ({os: "OpenWave", architecture: "x86_64", kernel: "test-kernel",
                               processors: 4, storageTotal: 2147483648, storageAvailable: 1073741824})
    property bool powerActionsAvailable: true
    property bool powerBusy: false
    property bool timeServiceAvailable: true
    property bool timeBusy: false
    property string timezone: "Europe/Istanbul"
    property var zones: ["Europe/Istanbul", "Europe/London", "America/New_York", "UTC"]
    property var savedNames: []
    property var powerActions: []
    property var timezoneCalls: []
    property int refreshCount: 0
    signal changed()
    signal errorOccurred(string message)

    function saveDeviceName(name) {
        const trimmed = name.trim()
        if (trimmed.length === 0)
            return false
        savedNames = savedNames.concat([trimmed])
        deviceName = trimmed
        return true
    }
    function setTimezone(zone) {
        timezoneCalls = timezoneCalls.concat([zone])
        if (!timeServiceAvailable || zones.indexOf(zone) < 0)
            return false
        timezone = zone
        return true
    }
    function requestPowerAction(action) {
        powerActions = powerActions.concat([action])
        return true
    }
    function refreshTimeZones() { refreshCount += 1 }
}
