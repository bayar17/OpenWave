import QtQuick

Canvas {
    id: glyph
    property string kind: "back"
    property color strokeColor: "white"
    property color holeColor: "#bdbdbd"
    onKindChanged: requestPaint()
    onStrokeColorChanged: requestPaint()
    onHoleColorChanged: requestPaint()
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
        } else if (kind === "lock" || kind === "unlock") {
            c.lineWidth = 3.2
            c.moveTo(10, 21)
            c.lineTo(38, 21); c.lineTo(38, 42); c.lineTo(10, 42); c.closePath()
            c.moveTo(15, 21)
            c.lineTo(15, 15)
            c.bezierCurveTo(15, 3, 33, 3, 33, 15)
            c.lineTo(kind === "unlock" ? 33 : 33, kind === "unlock" ? 9 : 21)
        } else if (kind === "star" || kind === "starFilled") {
            c.lineWidth = 3
            const cx = 24, cy = 26, outer = 20, inner = 8.4
            for (let i = 0; i < 10; ++i) {
                const radius = i % 2 === 0 ? outer : inner
                const angle = -Math.PI / 2 + i * Math.PI / 5
                const x = cx + radius * Math.cos(angle), y = cy + radius * Math.sin(angle)
                if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
            }
            c.closePath()
        } else if (kind === "chevronUp") {
            c.moveTo(6, 33); c.lineTo(24, 15); c.lineTo(42, 33)
        } else if (kind === "chevronDown") {
            c.moveTo(6, 15); c.lineTo(24, 33); c.lineTo(42, 15)
        }
        if (kind === "starFilled") { c.fillStyle = strokeColor; c.fill() }
        c.stroke()
        if (kind === "lock" || kind === "unlock") {
            c.fillStyle = strokeColor
            c.fillRect(10, 21, 28, 21)
            c.fillStyle = holeColor
            c.beginPath()
            c.arc(24, 29.5, 3.6, 0, Math.PI * 2)
            c.fill()
            c.fillRect(22.4, 30, 3.2, 6.5)
        }
        c.restore()
    }
}
