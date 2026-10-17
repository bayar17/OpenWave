import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string kind: "search"
    property color color: "white"
    width: 48
    height: 48
    Item {
        width: 48
        height: 48
        scale: Math.min(root.width, root.height) / 48
        transformOrigin: Item.TopLeft
        Rectangle {
            visible: root.kind === "lock" || root.kind === "unlock"
            x: 10; y: 21; width: 28; height: 25; radius: 4
            color: "transparent"
            border.color: root.color
            border.width: 3
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.kind === "chevron" ? root.color : "transparent"
                strokeWidth: 2
                fillColor: "transparent"
                startX: 16; startY: 4
                PathLine { x: 36; y: 24 }
                PathLine { x: 16; y: 44 }
            }
            ShapePath {
                strokeColor: root.kind === "search" ? root.color : "transparent"
                strokeWidth: 2.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 20; centerY: 20; radiusX: 16; radiusY: 16; startAngle: 0; sweepAngle: 360 }
                PathMove { x: 32; y: 32 }
                PathLine { x: 42; y: 42 }
            }
            ShapePath {
                strokeColor: root.kind === "bluetooth" ? root.color : "transparent"
                strokeWidth: 3.4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 11; startY: 13
                PathLine { x: 37; y: 34 }
                PathLine { x: 23; y: 44 }
                PathLine { x: 23; y: 3 }
                PathLine { x: 37; y: 13 }
                PathLine { x: 11; y: 34 }
            }
            ShapePath {
                strokeColor: root.kind === "refresh" ? root.color : "transparent"
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 24; centerY: 24; radiusX: 18; radiusY: 18; startAngle: 45; sweepAngle: 295 }
                PathMove { x: 34; y: 8 }
                PathLine { x: 41; y: 18 }
                PathLine { x: 44; y: 6 }
            }
            ShapePath {
                strokeColor: root.kind === "scan" ? root.color : "transparent"
                strokeWidth: 4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 24; centerY: 24; radiusX: 19; radiusY: 19; startAngle: 80; sweepAngle: 270 }
            }
            ShapePath {
                strokeColor: root.kind === "scan" ? "#080c0e" : "transparent"
                strokeWidth: 4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 24; centerY: 24; radiusX: 19; radiusY: 19; startAngle: 15; sweepAngle: 45 }
            }
            ShapePath {
                strokeColor: root.kind === "lock" || root.kind === "unlock"
                             ? root.color : "transparent"
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 16; startY: 21
                PathLine { x: 16; y: 17 }
                PathCubic {
                    control1X: 16; control1Y: 5
                    control2X: root.kind === "unlock" ? 36 : 32
                    control2Y: 5
                    x: root.kind === "unlock" ? 36 : 32
                    y: 17
                }
                PathLine { x: root.kind === "unlock" ? 36 : 32; y: root.kind === "unlock" ? 18 : 21 }
            }
            ShapePath {
                strokeColor: root.kind === "wifi" ? root.color : "transparent"
                strokeWidth: 3.7
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 3; startY: 18
                PathCubic { control1X: 14; control1Y: 6; control2X: 34; control2Y: 6; x: 45; y: 18 }
                PathMove { x: 10; y: 25 }
                PathCubic { control1X: 18; control1Y: 17; control2X: 30; control2Y: 17; x: 38; y: 25 }
                PathMove { x: 17; y: 32 }
                PathCubic { control1X: 21; control1Y: 28; control2X: 27; control2Y: 28; x: 31; y: 32 }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: root.kind === "wifi" ? root.color : "transparent"
                PathAngleArc { centerX: 24; centerY: 39; radiusX: 2.5; radiusY: 2.5; startAngle: 0; sweepAngle: 360 }
            }
            ShapePath {
                strokeColor: root.kind === "plus" ? root.color : "transparent"
                strokeWidth: 4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 24; startY: 4
                PathLine { x: 24; y: 44 }
                PathMove { x: 4; y: 24 }
                PathLine { x: 44; y: 24 }
            }
        }
    }
}
