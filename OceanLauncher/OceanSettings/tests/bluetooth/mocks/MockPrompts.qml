import QtQuick

QtObject {
    property bool busy: false
    property string kind: ""
    property string title: ""
    property var options: ({})
    property int cancelCount: 0
    signal finished(string requestId, string result, string text)

    function textInput(heading, body, config) {
        kind = "input"; title = heading; options = config; busy = true
    }
    function confirmation(heading, body, config) {
        kind = "confirmation"; title = heading; options = config; busy = true
    }
    function respond(result, text) {
        busy = false
        finished("request", result, text || "")
    }
    function cancel() {
        cancelCount += 1
        respond("cancelled", "")
    }
}
