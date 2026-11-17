import QtQuick
import QtQuick.Shapes

Shape {
    id: root

    property color color: "white"

    width: 28
    height: 16
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: root.color
        strokeWidth: 2.5
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: 2
        startY: root.height - 2
        PathLine { x: root.width / 2; y: 2 }
        PathLine { x: root.width - 2; y: root.height - 2 }
    }
}
