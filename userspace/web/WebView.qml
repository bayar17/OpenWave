import QtQuick
import org.wpewebkit.qtwpe 1.0 as WPE

WPE.WPEView {
    id: root
    privateBrowsing: true
    readonly property bool ready: state.ready
    readonly property bool navigationLoading: state.loading
    readonly property string errorText: state.error
    signal loadFinished(bool success, url address, string error)

    QtObject {
        id: state
        property bool ready: false
        property bool loading: false
        property string error: ""
    }
    Connections {
        target: root
        function onWebViewCreated() { state.ready = WebRuntime.attachView(root) }
        function onLoadingChanged(request) {
            if (request.status === WPE.WPEView.LoadStartedStatus) {
                state.loading = true
                state.error = ""
            } else {
                state.loading = false
                state.error = request.status === WPE.WPEView.LoadFailedStatus ? request.errorString : ""
                root.loadFinished(request.status === WPE.WPEView.LoadSucceededStatus, request.url, state.error)
            }
        }
    }
}
