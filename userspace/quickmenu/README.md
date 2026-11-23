# Ocean Quick Menu

Ocean Quick Menu is built from loadable QML applets. The shared module `OpenWave.QuickMenu` provides the applet base type, the appearance configuration and the system-operation interface.

```qml
import OpenWave.QuickMenu as Applets

Applets.Applet {
    title: qsTr("Wi-Fi")
    icon: Qt.resolvedUrl("icon.svg")
    available: Applets.System.wifiAvailable
    active: Applets.System.wifiEnabled
    onTriggered: Applets.System.setWifiEnabled(!Applets.System.wifiEnabled)
}
```

## Locations

| Kind | Directory | Default |
| --- | --- | --- |
| Required system applets | `/System/QuickMenu/Applets/Needed/` | shown, kept loaded even when hidden |
| Optional system applets | `/System/QuickMenu/Applets/` | shown |
| User applets | `/Data/Users/<user>/.DataLocation/QuickMenu/Applets/` | hidden until enabled in Settings |

Every applet lives in its own directory. `MainApplet.qml` is the only mandatory file. Supporting QML, JavaScript and assets can have any names and are resolved relative to the applet directory. The directory name is the applet id (`[A-Za-z][A-Za-z0-9_]*`, at most 64 characters).

## Optional `applet.desktop`

Applet metadata uses the same Desktop Entry format as appearances.

```ini
[Desktop Entry]
Type=X-OpenWave-Applet
Name=Torch
Name[tr]=El Feneri
Comment=Turn the torch on or off
Icon=icon.svg
X-OpenWave-Applet-Order=100
X-OpenWave-Applet-ApiVersion=1
X-OpenWave-Applet-Settings=Config.qml
```

The file and every key except `Type` are optional. `Icon` is a path inside the applet directory (svg or png). `ApiVersion` greater than the supported version rejects the applet. `Settings` names a QML file that Settings shows under the applet; it is honoured for required and optional system applets only. Paths must be plain relative paths inside the applet directory without symbolic links.

## Appearance

OpenWave ships no appearances. Each appearance is a Desktop Entry file named after its id, for example `dusk.desktop`. System appearances go in `/System/QuickMenu/Appearances/`. Appearances created in Settings are stored by the wallpaper service in `/var/openwave/appearances/users/<uid>/`; only trusted Settings can create, change or delete them, through `SaveAppearance` and `DeleteAppearance` on `org.openwave.Wallpaper1`. The Quick Menu strip and the Settings Appearance tab list the same appearances. The format follows the freedesktop Desktop Entry specification, in the same spirit as KDE theme metadata: `Type`, a localisable `Name` (`Name[tr]=...`), `Comment`, and `X-OpenWave-Appearance-*` extension keys.

```ini
[Desktop Entry]
Type=X-OpenWave-Appearance
Name=Dusk
Name[tr]=Alacakaranlik
Comment=Warm evening colors
X-OpenWave-Appearance-Order=40
X-OpenWave-Appearance-Hue=0.05
X-OpenWave-Appearance-Saturation=0.65
X-OpenWave-Appearance-Lightness=0.4
X-OpenWave-Appearance-ScrimColor=#2a1208
X-OpenWave-Appearance-ScrimAlpha=0.55
X-OpenWave-Appearance-Preview=dusk-preview.jpg
X-OpenWave-Appearance-Wallpaper=dusk-wallpaper.jpg
```

Only `Type` and `Name` are required. `Preview` names the thumbnail image shown in the strip; without one a swatch is generated from the colors. `Wallpaper` names the wallpaper the appearance sets; the wallpaper service applies it only when it is a plain file name next to the `.desktop` file. The image can be a PNG or JPEG of any resolution (up to 64 megapixels and 64 MiB); the service center-crops it to cover the 768×1366 wallpaper and stores that. A tile without a `Preview` shows its wallpaper as the thumbnail. Panel, applet, slider and selection colors all come from the active appearance through `Applets.Appearance`. The scrim is raised automatically until white text keeps 4.5:1 contrast over a white wallpaper. With no appearances installed the Quick Menu uses neutral colors and hides the appearance strip. Tapping an appearance in the Quick Menu applies it at once: the menu slides back up, the wallpaper crossfades to the appearance's wallpaper (saved through the wallpaper service, so it persists) while the appearance name fades in at the center of the screen, then fades out. Every animation respects the reduce-motion setting. Examples live in `tests/fixtures/appearances/` and are used by the tests only.

## Settings

Settings has a Quick Menu page that shows, hides and reorders applets. State is stored in the shared preferences file under `quickmenu/applets/` and `appearance/current`; the launcher and Settings watch that file.

## Security

Applets are QML loaded into the launcher process, so directory placement is not isolation. Applets only receive `Applets.System`, which exposes narrow operations and reports failure instead of pretending to succeed. Mutating the applet list is not reachable from QML in the launcher. User applets stay disabled until the user enables them.

## Tests

```sh
cmake -S userspace/quickmenu/tests -B out/os-build/quickmenu-tests -DCMAKE_PREFIX_PATH=$OPENWAVE_SDK
cmake -S OceanLauncher/OceanSettings/tests/quickmenu -B out/os-build/quickmenu-module-tests -DCMAKE_PREFIX_PATH=$OPENWAVE_SDK
cmake -S OceanLauncher/OceanLauncher/tests/quickmenu-panel -B out/os-build/quickmenu-panel-tests -DCMAKE_PREFIX_PATH=$OPENWAVE_SDK
```

The boot check is `scripts/test-openwave-boot.py --quickmenu` against an image that contains the new launcher.
