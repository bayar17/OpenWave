import QtQuick
import QtQuick.Window

Image {
    property real renderScale: 1
    sourceSize.width: Math.max(1, Math.ceil(width * renderScale * Screen.devicePixelRatio * 2))
    sourceSize.height: Math.max(1, Math.ceil(height * renderScale * Screen.devicePixelRatio * 2))
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
}
