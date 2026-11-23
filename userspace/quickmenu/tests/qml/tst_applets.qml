import QtQuick
import QtTest
import OpenWave.QuickMenu as Applets

Item {
    id: root
    width: 400
    height: 400

    Applets.Applet {
        id: sample
        width: 154
        height: 210
        title: "Sample"
        reduceMotion: true
    }
    SignalSpy { id: sampleSpy; target: sample; signalName: "triggered" }

    Applets.AppletHost {
        id: host
        width: 154
        height: 210
        reduceMotion: true
    }
    Applets.AppearancePreview {
        id: preview
        y: 220
        width: 300
        height: 400
    }
    SignalSpy { id: failureSpy; target: Applets.System; signalName: "operationFailed" }
    SignalSpy { id: lockSpy; target: Applets.System; signalName: "lockRequested" }

    TestCase {
        name: "Applet"
        when: windowShown

        function init() {
            TestHooks.resetBackend()
            sample.available = true
            sample.interactive = true
            sample.active = false
            sampleSpy.clear()
        }

        function test_tap_emits_triggered() {
            mouseClick(sample)
            compare(sampleSpy.count, 1)
        }

        function test_unavailable_applet_ignores_taps() {
            sample.available = false
            mouseClick(sample)
            compare(sampleSpy.count, 0)
            compare(sample.Accessible.description, "Unavailable")
        }

        function test_non_interactive_applet_ignores_taps() {
            sample.interactive = false
            mouseClick(sample)
            compare(sampleSpy.count, 0)
        }

        function test_keyboard_activation() {
            sample.forceActiveFocus()
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_Return)
            compare(sampleSpy.count, 2)
        }

        function test_accessibility_reflects_state() {
            compare(sample.Accessible.name, "Sample")
            compare(sample.Accessible.description, "Off")
            sample.active = true
            compare(sample.Accessible.description, "On")
            sample.statusText = "Custom"
            compare(sample.Accessible.description, "Custom")
            sample.statusText = ""
        }

        function test_colors_come_from_the_active_appearance() {
            const disc = findChild(sample, "appletDisc")
            verify(disc)
            Applets.Appearance.select("space")
            sample.active = true
            verify(Qt.colorEqual(disc.color, Applets.Appearance.activeColor))
            const space = Applets.Appearance.activeColor.toString()
            Applets.Appearance.select("dusk")
            verify(Qt.colorEqual(disc.color, Applets.Appearance.activeColor))
            verify(space !== Applets.Appearance.activeColor.toString())
            sample.active = false
            verify(Qt.colorEqual(disc.color, Applets.Appearance.inactiveColor))
            const label = findChild(sample, "appletLabel")
            verify(Qt.colorEqual(label.color, Applets.Appearance.textColor))
            Applets.Appearance.select("space")
        }
    }

    TestCase {
        name: "ShippedApplets"
        when: windowShown

        function init() {
            TestHooks.resetBackend()
            host.appletId = ""
            failureSpy.clear()
            lockSpy.clear()
        }

        function load(id) {
            host.appletId = id
            verify(!host.failed, host.error)
            verify(host.applet !== null)
            return host.applet
        }

        function test_registry_lists_the_shipped_applets_in_order() {
            const ids = Applets.AppletRegistry.panelApplets.map(item => item.id)
            compare(ids, ["WiFi", "Bluetooth", "MobileData", "FlightMode", "Lock"])
        }

        function test_wifi_applet_reads_and_requests_state() {
            const applet = load("WiFi")
            compare(applet.title, "Wi-Fi")
            verify(applet.available)
            verify(!applet.active)
            mouseClick(applet)
            compare(TestBackend.calls, ["wifi:1"])
            verify(applet.active)
            mouseClick(applet)
            compare(TestBackend.calls, ["wifi:1", "wifi:0"])
            verify(!applet.active)
        }

        function test_wifi_applet_follows_external_changes() {
            const applet = load("WiFi")
            TestBackend.wifiEnabled = true
            verify(applet.active)
            TestBackend.wifiConnected = true
            compare(applet.statusText, "Connected")
            TestBackend.wifiConnected = false
            TestBackend.wifiEnabled = false
            verify(!applet.active)
        }

        function test_wifi_applet_without_hardware_does_nothing() {
            TestBackend.wifiAvailable = false
            const applet = load("WiFi")
            verify(!applet.available)
            mouseClick(applet)
            compare(TestBackend.calls, [])
            TestBackend.wifiAvailable = true
        }

        function test_bluetooth_applet() {
            const applet = load("Bluetooth")
            compare(applet.title, "Bluetooth")
            mouseClick(applet)
            compare(TestBackend.calls, ["bluetooth:1"])
            verify(applet.active)
            TestBackend.bluetoothAvailable = false
            verify(!applet.available)
            TestBackend.bluetoothAvailable = true
        }

        function test_flight_mode_applet() {
            const applet = load("FlightMode")
            compare(applet.title, "Flight Mode")
            mouseClick(applet)
            compare(TestBackend.calls, ["airplane:1"])
            verify(applet.active)
        }

        function test_mobile_data_applet_is_honest_about_missing_support() {
            let applet = load("MobileData")
            verify(!applet.available)
            mouseClick(applet)
            compare(failureSpy.count, 0)
            TestBackend.cellularAvailable = true
            verify(applet.available)
            verify(!applet.active)
            mouseClick(applet)
            compare(failureSpy.count, 1)
            compare(TestBackend.calls, [])
            verify(!applet.active)
        }

        function test_lock_applet_requests_a_lock() {
            const applet = load("Lock")
            mouseClick(applet)
            compare(lockSpy.count, 1)
        }

        function test_applet_icons_resolve_inside_their_own_directories() {
            for (const id of ["WiFi", "Bluetooth", "MobileData", "FlightMode", "Lock"]) {
                const applet = load(id)
                verify(applet.icon.toString().indexOf("file://") === 0, id)
                const image = findChild(applet, "appletIcon")
                tryCompare(image, "status", Image.Ready)
            }
        }
    }

    TestCase {
        name: "AppletLoading"
        when: windowShown

        function init() {
            host.appletId = ""
            TestHooks.reloadApplets()
        }

        function test_supporting_files_resolve_relative_to_the_applet() {
            host.appletId = "Helper"
            verify(!host.failed, host.error)
            compare(host.applet.title, "From helper")
            verify(host.applet.active)
            verify(host.applet.icon.toString().endsWith("/Helper/parts/glyph.svg"))
            tryCompare(findChild(host.applet, "appletIcon"), "status", Image.Ready)
        }

        function test_host_sizes_and_drives_the_applet() {
            host.appletId = "Counter"
            const applet = host.applet
            verify(applet)
            compare(applet.width, host.width)
            compare(applet.height, host.height)
            host.width = 200
            compare(applet.width, 200)
            host.width = 154
            host.interactive = false
            verify(!applet.interactive)
            host.interactive = true
            mouseClick(applet)
            compare(applet.taps, 1)
            verify(applet.active)
        }

        function test_syntax_errors_fail_without_crashing() {
            host.appletId = "Broken"
            verify(host.failed)
            verify(host.error.length > 0)
            compare(host.applet, null)
            verify(Applets.AppletRegistry.applet("Broken").loadError.length > 0)
        }

        function test_a_root_that_is_not_an_applet_is_rejected() {
            host.appletId = "WrongRoot"
            verify(host.failed)
            verify(host.error.indexOf("not an Applet") >= 0)
            compare(host.applet, null)
        }

        function test_missing_supporting_components_fail() {
            host.appletId = "MissingSibling"
            verify(host.failed)
            compare(host.applet, null)
        }

        function test_unknown_applets_fail() {
            host.appletId = "DoesNotExist"
            verify(host.failed)
            compare(host.applet, null)
        }

        function test_switching_applets_replaces_the_previous_one() {
            host.appletId = "Counter"
            const first = host.applet
            verify(first)
            host.appletId = "Helper"
            verify(host.applet !== first)
            compare(host.applet.title, "From helper")
            host.appletId = ""
            compare(host.applet, null)
            verify(!host.failed)
        }

        function test_user_applets_are_available_to_the_loader_but_disabled_by_default() {
            const ids = Applets.AppletRegistry.panelApplets.map(item => item.id)
            verify(ids.indexOf("Counter") < 0)
            verify(Applets.AppletRegistry.applet("Counter").kind === "user")
            verify(!Applets.AppletRegistry.applet("Counter").enabled)
        }
    }

    TestCase {
        name: "AppearanceIntegration"
        when: windowShown

        function test_presets_are_listed_and_selectable() {
            compare(Applets.Appearance.presets.map(item => item.id), ["space", "purple", "ocean", "dusk"])
            for (const preset of Applets.Appearance.presets) {
                verify(Applets.Appearance.select(preset.id))
                compare(Applets.Appearance.current, preset.id)
                compare(Applets.Appearance.name, preset.name)
                verify(Applets.Appearance.scrim.a > 0)
            }
            verify(!Applets.Appearance.select("nonexistent"))
            Applets.Appearance.select("space")
        }
    }

    TestCase {
        name: "AppearancePreview"
        when: windowShown

        function presetNamed(id) {
            return Applets.Appearance.presets.find(item => item.id === id)
        }

        function init() {
            preview.compact = false
            preview.preset = ({})
            Applets.Appearance.select("space")
        }

        function test_preview_uses_the_given_preset_not_the_active_one() {
            const dusk = presetNamed("dusk")
            preview.preset = dusk
            const disc = findChild(preview, "previewDisc_0")
            const idle = findChild(preview, "previewDisc_3")
            verify(Qt.colorEqual(disc.color, dusk.activeColor))
            verify(Qt.colorEqual(idle.color, dusk.inactiveColor))
            verify(!Qt.colorEqual(disc.color, Applets.Appearance.activeColor))
            verify(Qt.colorEqual(findChild(preview, "previewScrim").color, dusk.scrim))
            verify(Qt.colorEqual(findChild(preview, "previewSliderFill_0").color, dusk.sliderFill))
        }

        function test_preview_matches_the_colors_applied_after_selecting() {
            for (const item of Applets.Appearance.presets) {
                preview.preset = item
                Applets.Appearance.select(item.id)
                verify(Qt.colorEqual(findChild(preview, "previewDisc_0").color, Applets.Appearance.activeColor), item.id)
                verify(Qt.colorEqual(findChild(preview, "previewScrim").color, Applets.Appearance.scrim), item.id)
            }
        }

        function test_preview_image_is_shown_and_hides_the_swatch_controls_in_compact_mode() {
            const dusk = presetNamed("dusk")
            verify(dusk.preview.toString().endsWith("/dusk.png"))
            preview.preset = dusk
            preview.compact = true
            const image = findChild(preview, "previewImage")
            tryCompare(image, "status", Image.Ready)
            verify(preview.imageReady)
            verify(!findChild(preview, "previewControls").visible)
            verify(!findChild(preview, "previewScrim").visible)
            preview.compact = false
            verify(findChild(preview, "previewControls").visible)
            verify(findChild(preview, "previewScrim").visible)
        }

        function test_presets_without_an_image_show_a_generated_swatch() {
            const bare = Object.assign({}, presetNamed("space"))
            bare.thumbnail = ""
            bare.preview = ""
            bare.wallpaper = ""
            preview.preset = bare
            preview.compact = true
            wait(100)
            verify(!preview.imageReady)
            verify(findChild(preview, "previewControls").visible)
            verify(!findChild(preview, "previewImage").visible)
        }

        function test_empty_presets_still_render_neutral_colors() {
            preview.preset = ({})
            verify(findChild(preview, "previewDisc_0").color !== undefined)
            verify(findChild(preview, "previewControls").visible)
        }
    }
}
