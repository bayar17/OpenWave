import QtQuick

Canvas {
    id: glyph
    property string kind: "back"
    property color strokeColor: "white"
    onKindChanged: requestPaint()
    onStrokeColorChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.clearRect(0, 0, width, height)
        c.save()
        c.scale(width / 48, height / 48)
        c.strokeStyle = strokeColor
        c.lineWidth = kind === "reload" ? 1.8 : 3.4
        c.lineCap = "round"
        c.lineJoin = "round"
        c.beginPath()
        if (kind === "back") {
            c.moveTo(33, 6); c.lineTo(15, 24); c.lineTo(33, 42)
        } else if (kind === "forward") {
            c.moveTo(15, 6); c.lineTo(33, 24); c.lineTo(15, 42)
        } else if (kind === "search") {
            c.arc(20, 20, 15, 0, Math.PI * 2)
            c.moveTo(31, 31); c.lineTo(44, 44)
        } else if (kind === "plus") {
            c.moveTo(24, 7); c.lineTo(24, 41)
            c.moveTo(7, 24); c.lineTo(41, 24)
        } else if (kind === "reload") {
            c.arc(24, 24, 18, Math.PI * 1.15, Math.PI * 1.95)
            c.moveTo(42, 9); c.lineTo(42, 22); c.lineTo(29, 22)
            c.moveTo(40, 32)
            c.arc(24, 24, 18, Math.PI * 0.15, Math.PI * 0.95)
            c.moveTo(6, 39); c.lineTo(6, 26); c.lineTo(19, 26)
        } else if (kind === "close") {
            c.moveTo(8, 8); c.lineTo(40, 40)
            c.moveTo(40, 8); c.lineTo(8, 40)
        } else if (kind === "menu") {
            c.moveTo(5, 12); c.lineTo(43, 12)
            c.moveTo(5, 24); c.lineTo(43, 24)
            c.moveTo(5, 36); c.lineTo(43, 36)
        }
        c.stroke()
        c.restore()
    }
}
