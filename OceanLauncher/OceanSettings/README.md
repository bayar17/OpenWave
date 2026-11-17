# OceanSettings

OceanSettings is OpenWave's first separately installed application. Open this
folder's `CMakeLists.txt` in Qt Creator to work on it independently. `Main.qml`
owns the application window; SettingsPage, SettingsRow, SettingsSymbol and
SettingsBackend live here, with their assets and tests. Qt Creator's existing
local project settings are preserved.

Build and install all OpenWave userspace from the ThunderOS root:

```sh
./build-openwave userspace
```

This stages `/usr/bin/ocean-settings`,
`/usr/share/applications/ocean-settings.desktop`, its icon, fonts and licenses
in `rootfs`. The launcher recognizes the installed desktop entry and executable
before enabling Settings. Other app icons remain unavailable.

Ocean is the sole EGLFS/DRM owner and runs the Wayland compositor. It launches
Settings directly as a Wayland client process; Settings creates an XDG toplevel
and Ocean places that frameless surface across the full phone screen. Its
background continues behind Ocean's transparent status row, while Settings
controls begin below it. Home hides the surface while leaving the process alive.
Closing Settings removes the surface. Settings draws
only application content, so the clock, battery, network status, and system
chevron are owned by Ocean. No Settings QML or backend is embedded in Ocean.

Settings preserves the Bluetooth concept, searchable scrolling tabs, General
information, device naming, timezone selection and accessibility preferences. The
General tab queries `timed` over the system bus and offers a searchable list of
installed IANA zones. The daemon validates the selection and updates
`/etc/localtime` and `/etc/timezone`; Ocean refreshes its clock after changes.
Wi-Fi and Bluetooth use `connectivityd` over the system bus. The Bluetooth page
shows connected and previously paired devices above a separator, with discovered
devices below it, and starts a 30-second scan when opened. Tap an unpaired device
to show Pairing… followed by Connecting… after successful pairing. Paired devices
move into the upper group even if connection fails. Tap a saved device to connect.
Hold a saved or connected device to slide into its detail pane. Edit nickname
and Forget this device use the shared frosted prompt service; Disconnect acts
immediately and preserves pairing. Nicknames use BlueZ's persisted Alias property.
The slide respects Reduce motion. The current pairing
agent supports devices that can pair without entering or confirming a code;
other pairing methods need a confirmation UI. SBC A2DP audio is available when
a Bluetooth adapter is passed through to the guest. Ocean's battery and
cellular graphics remain placeholders.

Shared system preferences live at `/var/lib/openwave/OpenWave/settings.ini` on
OpenWave. The previous `OpenWave/Ocean Launcher/settings.ini` is copied on first
use without overwriting an existing new configuration; the original remains
available. Both apps use the shared path, so Reduce motion still affects Ocean
Launcher after returning from Settings. Saves flush the file and directory entry.
Desktop builds use the corresponding XDG configuration directory.

Tests from the ThunderOS root:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu \
  toolchain/bin/qmltestrunner -input OceanLauncher/OceanSettings/tests
cmake -S OceanLauncher/OceanSettings/tests/backend \
  -B out/ocean-settings-backend-tests -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/toolchain"
cmake --build out/ocean-settings-backend-tests
ctest --test-dir out/ocean-settings-backend-tests --output-on-failure
```

The separate project owns Main.qml, SettingsPage.qml, SettingsRow.qml,
SettingsSymbol.qml, SettingsBackend.cpp/.h, its resources, and its tests.
