import QtQuick
import QtTest
import OpenWave.QuickMenu as Applets
import ".."

Item {
    id: window
    width: 768
    height: 1366

    FontLoader { id: regularFont; source: "../assets/fonts/Inter-Regular.otf" }
    FontLoader { id: lightFont; source: "../assets/fonts/Inter-Light.otf" }

    Image {
        id: wallpaper
        anchors.fill: parent
        source: "../assets/wallpaper.png"
    }

    QuickMenu {
        id: menu
        width: window.width
        height: window.height
        wallpaper: wallpaper
        canOpen: true
        reduceMotion: true
        assetBase: Qt.resolvedUrl("../assets/topbar/QuickMenu/")
        regularFont: regularFont.name
        lightFont: lightFont.name
    }
    SignalSpy { id: lockSpy; target: menu; signalName: "lockRequested" }
    SignalSpy { id: addSpy; target: menu; signalName: "addAppletsRequested" }
    SignalSpy { id: appliedSpy; target: menu; signalName: "appearanceApplied" }
    SignalSpy { id: failureSpy; target: Applets.System; signalName: "operationFailed" }

    Rectangle {
        id: statusBackground
        width: parent.width
        height: 70
        z: 1
        color: "white"
        visible: false
    }
    Item {
        id: handleLayer
        anchors.fill: parent
        z: 2
    }

    TestCase {
        name: "QuickMenuGesture"
        when: windowShown

        function init() {
            TestHooks.resetBackend()
            menu.reduceMotion = true
            menu.settle(false)
            menu.progress = 0
            menu.canOpen = true
            lockSpy.clear()
            addSpy.clear()
        }

        function test_initialises_closed() {
            compare(menu.progress, 0)
            compare(menu.open, false)
            verify(!findChild(menu, "quickMenuPanel").visible)
        }

        function test_disabled_handle_stays_visible() {
            const handle = findChild(menu, "quickMenuHandle")
            menu.canOpen = false
            menu.showDisabledHandle = true
            verify(handle.visible)
            verify(!handle.enabled)
            mouseClick(handle, handle.width / 2, handle.height / 2)
            compare(menu.progress, 0)
            menu.showDisabledHandle = false
        }

        function test_light_page_handle_paints_above_status_background() {
            const handle = findChild(menu, "quickMenuHandle")
            const chevron = findChild(menu, "topChevron")
            menu.appVisible = true
            menu.handleLayer = handleLayer
            menu.handleColor = "black"
            statusBackground.visible = true
            compare(handle.parent, handleLayer)
            compare(chevron.color, Qt.color("black"))
            menu.handleColor = "white"
            menu.handleLayer = null
            menu.appVisible = false
            statusBackground.visible = false
        }

        function test_handle_tap_and_geometry() {
            menu.reduceMotion = false
            const handle = findChild(menu, "quickMenuHandle")
            const panel = findChild(menu, "quickMenuPanel")
            const chevron = findChild(menu, "topChevron")
            compare(Math.round(chevron.rotation), 180)
            mouseClick(handle, handle.width / 2, handle.height / 2)
            tryCompare(menu, "progress", 1)
            compare(panel.height, menu.bottomY)
            compare(Math.round(chevron.rotation), 0)
            compare(Math.round(handle.y + handle.height / 2), Math.round(menu.chevronEndY + 8))
            mouseClick(handle, handle.width / 2, handle.height / 2)
            tryCompare(menu, "progress", 0)
        }

        function test_pull_down_and_back() {
            menu.reduceMotion = false
            const handle = findChild(menu, "quickMenuHandle")
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, handle.height / 2 + 25, 20)
            mouseMove(handle, handle.width / 2, handle.height / 2 + 600, 30)
            verify(menu.progress > 0.25)
            mouseRelease(handle, handle.width / 2, handle.height / 2 + 600)
            tryCompare(menu, "progress", 1)
            mousePress(handle, handle.width / 2, handle.height / 2)
            mouseMove(handle, handle.width / 2, handle.height / 2 - 25, 20)
            mouseMove(handle, handle.width / 2, handle.height / 2 - 700, 30)
            mouseRelease(handle, handle.width / 2, handle.height / 2 - 700)
            tryCompare(menu, "progress", 0)
        }

        function test_fast_release_velocity_settles_directionally() {
            menu.reduceMotion = false
            menu.progress = 0.18
            menu.settle(menu.progress >= 0.5, 1200)
            compare(menu.open, false)
            menu.progress = 0.42
            menu.settle(true, 1200)
            compare(menu.open, true)
            menu.settle(false, -1200)
            compare(menu.open, false)
        }

        function test_arrival_rotation_and_staggered_reveal() {
            menu.reduceMotion = false
            const chevron = findChild(menu, "topChevron")
            menu.progress = 0.22
            compare(Math.round(chevron.rotation), 180)
            verify(menu.contentOpacity > 0 && menu.contentOpacity < 1)
            menu.progress = 0.45
            verify(findChild(menu, "quickApplet_WiFi").opacity > 0.9)
            verify(findChild(menu, "quickApplet_Lock").opacity < 1)
            menu.progress = 0.9
            verify(chevron.rotation > 0 && chevron.rotation < 180)
            menu.progress = 1
            compare(Math.round(chevron.rotation), 0)
            tryCompare(findChild(menu, "quickApplet_Lock"), "opacity", 1)
            menu.progress = 0
            compare(menu.contentOpacity, 0)
        }
    }

    TestCase {
        name: "QuickMenuContent"
        when: windowShown

        function init() {
            TestHooks.useFixtureAppearances(true)
            TestHooks.resetBackend()
            TestHooks.writeBattery(87, "Charging")
            Applets.Appearance.select("space")
            menu.reduceMotion = true
            menu.settle(true)
            menu.width = window.width
            menu.height = window.height
            lockSpy.clear()
            addSpy.clear()
            failureSpy.clear()
        }

        function cleanup() {
            menu.settle(false)
        }

        function appletHost(id) {
            const host = findChild(menu, "quickApplet_" + id)
            verify(host !== null, id)
            return host
        }

        function test_lists_the_system_applets_in_registry_order() {
            const ids = Applets.AppletRegistry.panelApplets.map(item => item.id)
            compare(ids, ["WiFi", "Bluetooth", "MobileData", "FlightMode", "Lock"])
            for (const id of ids) {
                tryVerify(() => appletHost(id).applet !== null)
                compare(appletHost(id).applet.title.length > 0, true)
            }
            compare(appletHost("WiFi").applet.title, "Wi-Fi")
            compare(appletHost("Lock").applet.title, "Lock Phone")
        }

        function test_applet_taps_reach_the_system_service() {
            tryVerify(() => appletHost("WiFi").applet !== null)
            mouseClick(appletHost("WiFi"))
            compare(TestBackend.calls, ["wifi:1"])
            tryVerify(() => appletHost("WiFi").applet.active)
            mouseClick(appletHost("Bluetooth"))
            mouseClick(appletHost("FlightMode"))
            compare(TestBackend.calls, ["wifi:1", "bluetooth:1", "airplane:1"])
        }

        function test_unavailable_hardware_is_shown_and_inert() {
            TestBackend.wifiAvailable = false
            tryVerify(() => !appletHost("WiFi").applet.available)
            mouseClick(appletHost("WiFi"))
            compare(TestBackend.calls, [])
            verify(!appletHost("MobileData").applet.available)
        }

        function test_lock_applet_requests_a_trusted_session_lock() {
            tryVerify(() => appletHost("Lock").applet !== null)
            mouseClick(appletHost("Lock"))
            compare(lockSpy.count, 1)
        }

        function test_add_applets_tile_is_the_last_entry() {
            const tile = findChild(menu, "quickMenuAddApplets")
            verify(tile !== null)
            compare(tile.slotIndex, 5)
            tryCompare(tile, "opacity", 1)
            const pager = findChild(menu, "quickMenuPager")
            pager.contentX = pager.width
            tryVerify(() => tile.x >= pager.width)
            const applet = tile.children[0]
            mouseClick(applet)
            compare(addSpy.count, 1)
            pager.contentX = 0
        }

        function test_pages_follow_the_applet_count() {
            const pager = findChild(menu, "quickMenuPager")
            const dots = findChild(menu, "quickMenuPageDots")
            compare(menu.pageCount, 2)
            verify(dots.visible)
            compare(pager.contentWidth, menu.width * 2)
            pager.contentX = pager.width
            compare(pager.page, 1)
            pager.contentX = 0
            compare(pager.page, 0)
            TestHooks.setAppletEnabled("Lock", false)
            TestHooks.setAppletEnabled("FlightMode", false)
            tryCompare(menu, "pageCount", 1)
            verify(!dots.visible)
            TestHooks.setAppletEnabled("Lock", true)
            TestHooks.setAppletEnabled("FlightMode", true)
            tryCompare(menu, "pageCount", 2)
        }

        function test_hidden_required_applets_stay_loaded_but_not_shown() {
            tryVerify(() => appletHost("WiFi").applet !== null)
            TestHooks.setAppletEnabled("WiFi", false)
            tryVerify(() => findChild(menu, "quickApplet_WiFi") === null)
            compare(Applets.AppletRegistry.residentApplets.map(item => item.id), ["WiFi"])
            TestHooks.setAppletEnabled("WiFi", true)
            tryVerify(() => findChild(menu, "quickApplet_WiFi") !== null)
            compare(Applets.AppletRegistry.residentApplets.length, 0)
        }

        function test_appearance_strip_selects_and_marks_the_preset() {
            const strip = findChild(menu, "quickMenuAppearances")
            verify(strip !== null)
            for (const id of ["space", "purple", "ocean", "dusk"])
                verify(findChild(menu, "quickAppearance_" + id) !== null, id)
            compare(selection("space").opacity, 1)
            compare(selection("purple").opacity, 0)
            appliedSpy.clear()
            menu.settle(true)
            mouseClick(findChild(menu, "quickAppearance_purple"))
            compare(Applets.Appearance.current, "purple")
            compare(selection("purple").opacity, 1)
            compare(selection("space").opacity, 0)
            verify(Qt.colorEqual(selection("purple").border.color, Applets.Appearance.selectionColor))
        }

        function test_without_appearances_the_strip_is_hidden_and_applets_move_up() {
            const strip = findChild(menu, "quickMenuAppearances")
            const pager = findChild(menu, "quickMenuPager")
            const withStrip = pager.y
            verify(strip.visible)
            TestHooks.useFixtureAppearances(false)
            compare(Applets.Appearance.presets.length, 0)
            verify(!strip.visible)
            verify(!menu.hasAppearances)
            verify(pager.y < withStrip)
            verify(Qt.colorEqual(findChild(menu, "quickMenuScrim").color, Applets.Appearance.scrim))
            tryVerify(() => findChild(menu, "quickApplet_WiFi").applet !== null)
            verify(findChild(menu, "quickVolume").y > pager.y + pager.height)
            TestHooks.useFixtureAppearances(true)
            verify(strip.visible)
        }

        function test_tapping_an_appearance_applies_it_and_closes_the_menu() {
            appliedSpy.clear()
            compare(menu.open, true)
            mouseClick(findChild(menu, "quickAppearance_dusk"))
            compare(Applets.Appearance.current, "dusk")
            compare(menu.open, false)
            compare(appliedSpy.count, 1)
            compare(appliedSpy.signalArguments[0][0], "dusk")
            compare(appliedSpy.signalArguments[0][1], "Dusk")
            verify(appliedSpy.signalArguments[0][2].toString().endsWith("/dusk-wallpaper.jpg"))
        }

        function test_the_menu_slides_up_when_motion_is_enabled() {
            appliedSpy.clear()
            menu.reduceMotion = false
            menu.progress = 1
            menu.open = true
            mouseClick(findChild(menu, "quickAppearance_ocean"))
            compare(Applets.Appearance.current, "ocean")
            compare(menu.open, false)
            tryVerify(() => menu.progress < 1)
            tryCompare(menu, "progress", 0)
            compare(appliedSpy.count, 1)
        }

        function test_tapping_the_active_appearance_still_reports_it() {
            appliedSpy.clear()
            mouseClick(findChild(menu, "quickAppearance_space"))
            compare(Applets.Appearance.current, "space")
            compare(appliedSpy.count, 1)
            compare(menu.open, false)
        }

        function test_selection_marker_fades_in_when_motion_is_enabled() {
            menu.reduceMotion = false
            const marker = selection("purple")
            compare(marker.opacity, 0)
            Applets.Appearance.select("purple")
            verify(marker.opacity < 1)
            tryCompare(marker, "opacity", 1)
            tryCompare(selection("space"), "opacity", 0)
        }

        function test_panel_colors_fade_to_the_new_appearance() {
            menu.reduceMotion = false
            const scrim = findChild(menu, "quickMenuScrim")
            Applets.Appearance.select("dusk")
            verify(!Qt.colorEqual(scrim.color, Applets.Appearance.scrim))
            tryVerify(() => Qt.colorEqual(scrim.color, Applets.Appearance.scrim))
        }

        function test_tile_gives_press_feedback() {
            menu.reduceMotion = false
            const tile = findChild(menu, "quickAppearance_ocean")
            compare(tile.scale, 1)
            mousePress(tile, tile.width / 2, tile.height / 2)
            tryVerify(() => tile.scale < 1)
            mouseRelease(tile, 1, 1)
            tryCompare(tile, "scale", 1)
        }

        function test_strip_tiles_use_the_preview_image_or_the_wallpaper() {
            const dusk = findChild(findChild(menu, "quickAppearance_dusk"), "quickAppearancePreviewTile")
            tryVerify(() => dusk.imageReady)
            verify(dusk.preset.thumbnail.toString().endsWith("/dusk.png"))
            const space = findChild(findChild(menu, "quickAppearance_space"), "quickAppearancePreviewTile")
            tryVerify(() => space.imageReady)
            verify(space.preset.thumbnail.toString().endsWith("/space-wallpaper.png"))
            verify(!findChild(space, "previewControls").visible)
        }

        function selection(id) {
            return findChild(findChild(menu, "quickAppearance_" + id), "quickAppearanceSelection")
        }

        function test_appearance_drives_the_panel_and_applet_colors() {
            const scrim = findChild(menu, "quickMenuScrim")
            tryVerify(() => appletHost("WiFi").applet !== null)
            TestHooks.resetBackend()
            mouseClick(appletHost("WiFi"))
            tryVerify(() => appletHost("WiFi").applet.active)
            const disc = findChild(appletHost("WiFi"), "appletDisc")
            Applets.Appearance.select("space")
            const spaceScrim = scrim.color.toString()
            const spaceActive = disc.color.toString()
            Applets.Appearance.select("dusk")
            verify(Qt.colorEqual(scrim.color, Applets.Appearance.scrim))
            verify(Qt.colorEqual(disc.color, Applets.Appearance.activeColor))
            verify(spaceScrim !== scrim.color.toString())
            verify(spaceActive !== disc.color.toString())
        }

        function test_panel_scrim_keeps_text_readable_over_any_wallpaper() {
            const scrim = findChild(menu, "quickMenuScrim").color
            const flattened = Qt.rgba(scrim.r * scrim.a + (1 - scrim.a), scrim.g * scrim.a + (1 - scrim.a), scrim.b * scrim.a + (1 - scrim.a), 1)
            function luminance(c) {
                const lin = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
                return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
            }
            verify(1.05 / (luminance(flattened) + 0.05) >= 2.95)
        }

        function test_volume_slider_tracks_the_system_state() {
            const slider = findChild(menu, "quickVolume")
            const label = findChild(slider, "sliderValueLabel")
            compare(Math.round(slider.value), 40)
            TestBackend.volume = 47
            tryCompare(slider, "value", 47)
            compare(slider.percent, 47)
            compare(label.text, "Volume: %47")
            TestBackend.volume = 0
            tryCompare(slider, "percent", 0)
            compare(label.text, "Volume: %0")
        }

        function test_dragging_a_slider_writes_the_system_state() {
            const slider = findChild(menu, "quickVolume")
            const track = findChild(slider, "sliderTrack")
            mouseClick(track, track.width * 0.5, track.height / 2)
            compare(TestBackend.volume, 50)
            mouseClick(track, track.width * 0.9, track.height / 2)
            compare(TestBackend.volume, 90)
            compare(slider.percent, 90)
        }

        function test_brightness_slider_synchronises_and_never_reaches_zero() {
            const slider = findChild(menu, "quickBrightness")
            const label = findChild(slider, "sliderValueLabel")
            TestBackend.brightness = 70
            tryCompare(slider, "percent", 70)
            compare(label.text, "Device Brightness: %70")
            const track = findChild(slider, "sliderTrack")
            mouseClick(track, 0, track.height / 2)
            compare(TestBackend.brightness, 1)
        }

        function test_battery_status_reflects_the_system() {
            const battery = findChild(menu, "quickBattery")
            verify(battery.visible)
            compare(battery.text, "Battery 87% - Charging")
            TestHooks.writeBattery(15, "Discharging")
            tryCompare(battery, "text", "Battery 15%")
        }

        function test_modem_label_tracks_the_modem() {
            const label = findChild(menu, "quickModemLabel")
            compare(label.text, "NO MODEM")
            TestBackend.modemLabel = "Vodafone DE - Unknown number"
            tryCompare(label, "text", "Vodafone DE - Unknown number")
        }

        function test_failed_operations_are_reported() {
            tryVerify(() => appletHost("MobileData").applet !== null)
            TestBackend.cellularAvailable = true
            tryVerify(() => appletHost("MobileData").applet.available)
            mouseClick(appletHost("MobileData"))
            compare(failureSpy.count, 1)
            compare(TestBackend.calls, [])
        }
    }

    TestCase {
        name: "QuickMenuLayout"
        when: windowShown

        function init() {
            menu.reduceMotion = true
            menu.settle(true)
        }

        function cleanup() {
            menu.width = Qt.binding(() => window.width)
            menu.height = Qt.binding(() => window.height)
            menu.settle(false)
        }

        function bottomOf(item) {
            return item.mapToItem(menu, 0, item.height).y
        }

        function check_layout(width, height) {
            menu.width = width
            menu.height = height
            const pager = findChild(menu, "quickMenuPager")
            const strip = findChild(menu, "quickMenuAppearances")
            const volume = findChild(menu, "quickVolume")
            const brightness = findChild(menu, "quickBrightness")
            const status = findChild(menu, "quickMenuStatus")
            const label = "size " + width + "x" + height
            verify(menu.bottomY <= height, label)
            verify(menu.unit > 0, label)
            verify(bottomOf(brightness) <= menu.bottomY + 1, label + ": brightness bottom " + bottomOf(brightness) + " vs " + menu.bottomY)
            verify(bottomOf(volume) <= brightness.mapToItem(menu, 0, 0).y + 1, label + ": sliders overlap")
            verify(pager.mapToItem(menu, 0, pager.height).y <= volume.mapToItem(menu, 0, 0).y, label + ": pager overlaps volume")
            verify(bottomOf(strip) <= pager.mapToItem(menu, 0, 0).y, label + ": strip overlaps applets")
            verify(bottomOf(status) <= strip.mapToItem(menu, 0, 0).y + 1, label + ": status overlaps strip")
            verify(status.mapToItem(menu, 0, 0).y >= 70 * menu.unit - 1, label + ": status under the status bar")
            compare(menu.columnWidth * menu.columns, width)
            verify(volume.x >= 0 && volume.x + volume.width * menu.unit <= width + 0.5, label + ": slider horizontal fit")
            for (const id of ["WiFi", "Bluetooth", "MobileData", "FlightMode"]) {
                const host = findChild(menu, "quickApplet_" + id)
                verify(host.x >= 0 && host.x + host.width <= width + 0.5, label + ": " + id)
                verify(host.width > 40 && host.height > 60, label + ": " + id + " size")
            }
        }

        function test_reference_phone_768x1366() { check_layout(768, 1366) }
        function test_smaller_phone_540x960() { check_layout(540, 960) }
        function test_tall_phone_720x1600() { check_layout(720, 1600) }
        function test_wide_screen_1080x1920() { check_layout(1080, 1920) }
        function test_short_tablet_1024x768() { check_layout(1024, 768) }
        function test_small_320x568() { check_layout(320, 568) }
    }
}
