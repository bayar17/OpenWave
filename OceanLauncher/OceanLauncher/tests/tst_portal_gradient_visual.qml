import QtQuick
import OpenWave.Fonts 1.0
import QtTest
import ".." as Ocean

Item {
    id: screen
    width: 440
    height: 120

    Rectangle { anchors.fill: parent; color: "#454e53" }
    Ocean.PortalTitle {
        x: 20; y: 20; width: 400; height: 80
        mutedTitle: "WWWW"
        primaryTitle: "WWWW"
        fontFamily: Fonts.family
        fontPixelSize: 48
    }

    TestCase {
        name: "PortalGradientVisual"
        when: windowShown
        function test_gradientIsVisible() {
            verify(Fonts.sourceSansProAvailable)
            wait(100)
            const image = grabImage(screen)
            function brightest(from, to) {
                let peak = 0
                for (let x = from; x < to; ++x) {
                    for (let y = 30; y < 90; ++y) {
                        const color = image.pixel(x, y).toString()
                        peak = Math.max(peak, parseInt(color.slice(1, 3), 16))
                    }
                }
                return peak
            }
            const mutedPeak = brightest(21, 100)
            const primaryPeak = brightest(240, 400)
            verify(mutedPeak > 90, "The title must be visible")
            verify(primaryPeak > mutedPeak + 20,
                   "The title should brighten toward its primary word")
        }
    }
}
