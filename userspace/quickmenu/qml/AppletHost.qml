import QtQuick

Item {
    id: host

    property string appletId: ""
    property bool interactive: true
    property bool reduceMotion: false
    property real renderScale: 1
    property string labelFont: ""
    property bool failed: false
    property string error: ""
    readonly property Item applet: appletItem

    property Item appletItem: null

    function discard() {
        if (!appletItem)
            return
        appletItem.visible = false
        appletItem.enabled = false
        appletItem.destroy()
        appletItem = null
    }

    function clear() {
        discard()
        failed = false
        error = ""
    }

    function fail(message) {
        discard()
        failed = true
        error = message
        AppletRegistry.reportLoadFailure(appletId, message)
    }

    function adopt(component) {
        if (component.status === Component.Error) {
            fail(component.errorString())
            return
        }
        if (component.status !== Component.Ready)
            return
        const item = component.createObject(host)
        if (!item) {
            fail(component.errorString())
            return
        }
        if (item.appletApi !== 1) {
            item.visible = false
            item.destroy()
            fail(qsTr("The applet's root item is not an Applet."))
            return
        }
        item.width = Qt.binding(() => host.width)
        item.height = Qt.binding(() => host.height)
        item.interactive = Qt.binding(() => host.interactive)
        item.reduceMotion = Qt.binding(() => host.reduceMotion)
        item.renderScale = Qt.binding(() => host.renderScale)
        item.labelFont = Qt.binding(() => host.labelFont)
        appletItem = item
    }

    function load() {
        clear()
        if (appletId === "")
            return
        const url = AppletRegistry.entryUrl(appletId)
        if (url.toString() === "") {
            failed = true
            error = qsTr("The applet is no longer available.")
            return
        }
        const component = Qt.createComponent(url)
        if (component.status === Component.Loading)
            component.statusChanged.connect(() => host.adopt(component))
        else
            adopt(component)
    }

    onAppletIdChanged: load()
    Component.onCompleted: load()
}
