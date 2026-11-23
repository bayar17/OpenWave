import QtQuick

QtObject {
    property bool busy: false
    property string kind: ""
    property string title: ""
    property var options: ({})
    property int cancelCount: 0
    property var passwordRequests: []
    signal finished(string requestId, string result, string text)
    signal fieldsFinished(string requestId, string result, var values)

    function passwordInput(heading, body, config) {
        passwordRequests = passwordRequests.concat([{title: heading, body: body, options: config}])
        kind = "password"; title = heading; options = config; busy = true
    }
    function confirmation(heading, body, config) {
        kind = "confirmation"; title = heading; options = config; busy = true
    }
    function form(heading, body, config) {
        kind = "form"; title = heading; options = config; busy = true
    }
    function respondFields(result, values) {
        busy = false
        fieldsFinished("request", result, values)
    }
    function respond(result, text) {
        busy = false
        finished("request", result, text || "")
    }
    function complete(result, text) { respond(result, text) }
    function cancel() {
        cancelCount += 1
        if (kind === "form")
            respondFields("cancelled", {})
        else
            respond("cancelled", "")
    }
}
