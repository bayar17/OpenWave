import QtQuick
import QtQuick.Window

Image {
    // Render SVGs at twice their physical display size, then use the same
    // mipmapped minification for vector and bitmap assets in both icon grids.
    property real renderScale: 1
    sourceSize.width: Math.max(1, Math.ceil(width * renderScale * Screen.devicePixelRatio * 2))
    sourceSize.height: Math.max(1, Math.ceil(height * renderScale * Screen.devicePixelRatio * 2))
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
}
