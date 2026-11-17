import QtQuick
import QtQuick.Shapes

Item {
    id: glyph
    property string kind: "menu"
    property color color: "white"
    width: 42; height: 42
    Item {
        width: 48; height: 48
        scale: Math.min(glyph.width, glyph.height) / 48
        transformOrigin: Item.TopLeft
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: glyph.kind === "wifi" ? glyph.color : "transparent"
                strokeWidth: 3; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                startX: 4; startY: 16
                PathCubic { control1X: 15; control1Y: 5; control2X: 33; control2Y: 5; x: 44; y: 16 }
                PathMove { x: 11; y: 24 }
                PathCubic { control1X: 18; control1Y: 17; control2X: 30; control2Y: 17; x: 37; y: 24 }
                PathMove { x: 18; y: 32 }
                PathCubic { control1X: 22; control1Y: 28; control2X: 26; control2Y: 28; x: 30; y: 32 }
                PathMove { x: 24; y: 39 }
                PathLine { x: 24; y: 40 }
            }
            ShapePath {
                strokeColor: glyph.kind === "menu" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                startX: 5; startY: 10
                PathLine { x: 43; y: 10 }
                PathMove { x: 5; y: 24 }
                PathLine { x: 43; y: 24 }
                PathMove { x: 5; y: 38 }
                PathLine { x: 43; y: 38 }
            }
            ShapePath {
                strokeColor: glyph.kind === "search" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 20; centerY: 20; radiusX: 16; radiusY: 16; startAngle: 0; sweepAngle: 360 }
                PathMove { x: 32; y: 32 }
                PathLine { x: 44; y: 44 }
            }
            ShapePath {
                strokeColor: ["back", "next", "down"].includes(glyph.kind) ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: glyph.kind === "next" ? 15 : glyph.kind === "down" ? 6 : 31
                startY: glyph.kind === "down" ? 15 : 6
                PathLine { x: glyph.kind === "back" ? 13 : glyph.kind === "next" ? 35 : 24; y: glyph.kind === "down" ? 33 : 24 }
                PathLine { x: glyph.kind === "next" ? 15 : glyph.kind === "down" ? 42 : 31; y: glyph.kind === "down" ? 15 : 42 }
            }
            ShapePath {
                strokeColor: glyph.kind === "heart" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; joinStyle: ShapePath.RoundJoin
                startX: 24; startY: 41
                PathCubic { control1X: -12; control1Y: 19; control2X: 10; control2Y: -2; x: 24; y: 13 }
                PathCubic { control1X: 38; control1Y: -2; control2X: 60; control2Y: 19; x: 24; y: 41 }
            }
            ShapePath {
                strokeColor: glyph.kind === "info" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 24; centerY: 24; radiusX: 20; radiusY: 20; startAngle: 0; sweepAngle: 360 }
                PathMove { x: 24; y: 22 }
                PathLine { x: 24; y: 35 }
                PathMove { x: 24; y: 13 }
                PathLine { x: 24; y: 14 }
            }
            ShapePath {
                strokeColor: glyph.kind === "play" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; joinStyle: ShapePath.RoundJoin
                startX: 14; startY: 6
                PathLine { x: 40; y: 24 }
                PathLine { x: 14; y: 42 }
                PathLine { x: 14; y: 6 }
            }
            ShapePath {
                strokeColor: glyph.kind === "pause" ? glyph.color : "transparent"
                strokeWidth: 3; fillColor: "transparent"
                startX: 17; startY: 7
                PathLine { x: 17; y: 41 }
                PathMove { x: 32; y: 7 }
                PathLine { x: 32; y: 41 }
            }
            ShapePath {
                strokeColor: glyph.kind === "close" ? glyph.color : "transparent"
                strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                startX: 9; startY: 9
                PathLine { x: 39; y: 39 }
                PathMove { x: 39; y: 9 }
                PathLine { x: 9; y: 39 }
            }
        }
    }
}
