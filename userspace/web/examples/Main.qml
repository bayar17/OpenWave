import QtQuick
import QtQuick.Window
import OpenWave.Web 1.0

Window {
    width: 544
    height: 960
    visible: true
    WebView {
        anchors.fill: parent
        onReadyChanged: if (ready) loadHtml("<!doctype html><html><head>"
            + "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            + "<title>OpenWave WebView</title></head><body>"
            + "<h1>OpenWave WebView</h1><p>Rendered with WPE WebKit.</p></body></html>")
    }
}
