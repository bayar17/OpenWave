import QtQuick
import QtTest
import ".."

Item {
    width: 160
    height: 80

    SignalIcon {
        id: wifi
        width: 45
        height: 34
        assetBase: Qt.resolvedUrl("../../OceanTopbard/assets/TopBar/")
    }

    SignalIcon {
        id: cellular
        x: 55
        width: 82
        height: 20
        kind: "cellular"
        assetBase: Qt.resolvedUrl("../../OceanTopbard/assets/TopBar/")
    }

    TestCase {
        name: "TopbarSignalIcons"
        when: windowShown

        function checkLevels(icon, count) {
            icon.connected = false
            icon.strength = 100
            compare(icon.level, 0)
            verify(icon.source.toString().endsWith("/0bar.svg"))
            tryCompare(icon, "status", Image.Ready)

            icon.connected = true
            for (let level = 0; level <= count; ++level) {
                icon.strength = level === 0 ? 0 : Math.ceil(100 * level / count)
                compare(icon.level, level)
                const suffix = level === count ? "fullbar-active.svg"
                                               : level + "bar-active.svg"
                verify(icon.source.toString().endsWith("/" + suffix))
                tryCompare(icon, "status", Image.Ready)
            }
        }

        function test_wifi_assets_and_levels() { checkLevels(wifi, 4) }
        function test_cellular_assets_and_levels() { checkLevels(cellular, 5) }

        function test_two_wifi_bars_are_visibly_distinct() {
            wifi.connected = true
            wifi.strength = 50
            compare(wifi.level, 2)
            tryCompare(wifi, "status", Image.Ready)
            waitForRendering(wifi)
            const image = grabImage(wifi)
            let lit = 0
            let unlit = 0
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    const pixel = image.pixel(x, y)
                    if (pixel.a < 0.5)
                        continue
                    if (pixel.r > 0.9 && pixel.g > 0.9 && pixel.b > 0.9)
                        ++lit
                    if (pixel.r > 0.35 && pixel.r < 0.6
                            && pixel.b > pixel.r && pixel.b < 0.75)
                        ++unlit
                }
            }
            verify(lit > 4, "The two available bars must render white")
            verify(unlit > 4, "The unfilled bars must remain visible in blue-gray")
        }
    }
}
