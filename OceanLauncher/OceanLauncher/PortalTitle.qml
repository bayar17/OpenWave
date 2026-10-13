import QtQuick

Item {
    id: titleItem

    // Callers can supply translated segments. A legacy single-string title
    // falls back to emphasizing its final whitespace-separated word.
    property string title: ""
    property string mutedTitle: ""
    property string primaryTitle: ""
    property color primaryColor: "#ffffff"
    property real mutedOpacity: 0.5
    property string fontFamily: ""
    property int fontPixelSize: 44
    property int fontWeight: Font.ExtraLight

    readonly property bool hasSegments: mutedTitle.length > 0 || primaryTitle.length > 0
    readonly property string trimmedTitle: title.trim()
    readonly property int splitAt: trimmedTitle.search(/\s+\S+$/)
    readonly property string effectiveMutedTitle: hasSegments ? mutedTitle.trim()
        : splitAt >= 0 ? trimmedTitle.slice(0, splitAt).trim() : ""
    readonly property string effectivePrimaryTitle: hasSegments ? primaryTitle.trim()
        : splitAt >= 0 ? trimmedTitle.slice(splitAt).trim() : trimmedTitle
    readonly property string displayTitle: effectiveMutedTitle && effectivePrimaryTitle
        ? effectiveMutedTitle + " " + effectivePrimaryTitle
        : effectiveMutedTitle || effectivePrimaryTitle
    readonly property string renderedTitle: titleMetrics.elidedText(displayTitle,
        Text.ElideRight, width)
    readonly property real gradientEndPosition: effectiveMutedTitle
        ? Math.min(1, titleMetrics.advanceWidth(effectiveMutedTitle
            + (effectivePrimaryTitle ? " " : ""))
            / Math.max(1, titleMetrics.advanceWidth(displayTitle))) : 0

    onRenderedTitleChanged: gradientCanvas.requestPaint()
    onGradientEndPositionChanged: gradientCanvas.requestPaint()
    onPrimaryColorChanged: gradientCanvas.requestPaint()
    onMutedOpacityChanged: gradientCanvas.requestPaint()
    onFontFamilyChanged: gradientCanvas.requestPaint()
    onFontPixelSizeChanged: gradientCanvas.requestPaint()
    onFontWeightChanged: gradientCanvas.requestPaint()
    onWidthChanged: gradientCanvas.requestPaint()
    onHeightChanged: gradientCanvas.requestPaint()

    FontMetrics {
        id: titleMetrics
        font.family: titleItem.fontFamily
        font.pixelSize: titleItem.fontPixelSize
        font.weight: titleItem.fontWeight
    }

    Text {
        id: titleText
        objectName: "portalTitleText"
        x: 0
        y: 0
        width: Math.min(titleItem.width, titleMetrics.advanceWidth(text))
        height: titleItem.height
        text: titleItem.displayTitle
        visible: titleItem.displayTitle.length > 0
        opacity: 0
        color: titleItem.primaryColor
        font.family: titleItem.fontFamily
        font.pixelSize: titleItem.fontPixelSize
        font.weight: titleItem.fontWeight
        elide: Text.ElideRight
        wrapMode: Text.NoWrap
        verticalAlignment: Text.AlignVCenter
    }

    Canvas {
        id: gradientCanvas
        objectName: "portalTitleGradient"
        x: 0
        y: 0
        width: titleText.width
        height: titleText.height
        visible: titleItem.displayTitle.length > 0
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!titleItem.renderedTitle.length) return

            const family = titleItem.fontFamily.replace(/["\\]/g, "") || "sans-serif"
            ctx.font = titleItem.fontWeight + " " + titleItem.fontPixelSize
                + "px \"" + family + "\""
            ctx.textBaseline = "alphabetic"
            const red = Math.round(titleItem.primaryColor.r * 255)
            const green = Math.round(titleItem.primaryColor.g * 255)
            const blue = Math.round(titleItem.primaryColor.b * 255)
            const gradient = ctx.createLinearGradient(0, 0, Math.max(1, width), 0)
            const colorAt = opacity => "rgba(" + red + "," + green + ","
                + blue + "," + opacity + ")"
            gradient.addColorStop(0, colorAt(titleItem.effectiveMutedTitle
                ? titleItem.mutedOpacity : 1))
            if (titleItem.effectiveMutedTitle) {
                gradient.addColorStop(titleItem.gradientEndPosition, colorAt(1))
            }
            gradient.addColorStop(1, colorAt(1))
            ctx.fillStyle = gradient
            ctx.fillText(titleItem.renderedTitle, 0,
                (height - titleMetrics.height) / 2 + titleMetrics.ascent)
        }
    }
}
