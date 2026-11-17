import QtQuick

IconImage {
    id: indicator

    property string kind: "wifi"
    property int strength: 0
    property bool connected: false
    property url assetBase: "qrc:/OceanTopbard/assets/TopBar/"

    readonly property int segmentCount: kind === "cellular" ? 5 : 4
    readonly property int level: connected
        ? Math.ceil(Math.max(0, Math.min(100, strength)) * segmentCount / 100)
        : 0

    source: {
        const base = indicator.assetBase + kind + "/"
        if (!connected)
            return base + "0bar.svg"
        const name = level === segmentCount ? "fullbar" : level + "bar"
        return base + name + "-active.svg"
    }
}
