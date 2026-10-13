# Ocean Launcher on OpenWave

The Qt Quick launcher source is in `OceanLauncher/`. OpenWave builds it as
`/usr/bin/ocean-launcher` and starts it from `/init` using the EGLFS/KMS
configuration in `/etc/openwave/kms.json`.

From the OpenWave source root:

```sh
./build-openwave.sh userspace
```

This cross-compiles init and the launcher using `cross/openwave-x86_64.cmake`,
the installed target Qt in `toolchain/sysroot/x86_64-unknown-openwave/usr`, and the Qt host tools in `toolchain`.
It installs both executables, the runtime font, and the KMS configuration into
`rootfs`. The build needs the OpenWave Qt 6.11.2 target build with Quick,
Quick Controls 2, SVG, WaylandCompositor, the compositor QML module, and the
Wayland client platform plugin. The target sysroot also needs Wayland
client/server, xkbcommon, libffi, and XKB keyboard data. Mesa must be built
with its Wayland EGL platform and `bind-wayland-display` support; without that
extension, Ocean cannot import the accelerated client surface. The matching
host `qtwaylandscanner` is a build tool and must stay outside the target image.
Resources and a
licensed Inter Light/Regular fonts are embedded in the executable. DejaVu Sans
is retained as a system fallback.

To create a new boot image and run it:

```sh
./build-openwave.sh userspace --image out/OpenWave-ocean.img
./run-openwave --image out/OpenWave-ocean.img run
```

Image creation uses `mke2fs` and `e2fsck`, does not need root, and refuses to
overwrite an existing image. It packages the complete staged `rootfs`,
including its existing Qt, graphics libraries, and configuration. Build output
is in `out/userspace-x86_64-unknown-openwave`. Set `OPENWAVE_BUILD_JOBS` to change parallelism.
The exported SDK toolchain is relocatable; runtime files are assembled from the
SDK and `out/staging/x86_64-unknown-openwave/userspace`. See [SDK and rootfs](../SDK.md).

The design coordinate space remains 768x1366. The default physical QEMU
framebuffer is 544x960, which fits a 1080p desktop without reducing a larger
guest framebuffer in the host window. The launcher renders proportionally
inside that portrait display.

```sh
./run-openwave run                          # 544x960 physical pixels
./run-openwave run --resolution 768x1366    # full design resolution
./run-openwave run --resolution 480x854     # smaller native portrait view
```

`OPENWAVE_QEMU_RESOLUTION` also sets the default. Dimensions must be 320..4096;
width must be a multiple of eight because DRM mode timing aligns it to eight
pixels. Invalid modes are rejected before QEMU starts.

The helper synchronizes the GPU's xres/yres, Linux's
`video=Virtual-1:WIDTHxHEIGHT@60e`, and `OPENWAVE_DISPLAY_MODE`. Init writes the
validated Qt configuration to `/run/openwave/kms.json` for Qt's `Virtual1`
connector. `/etc/openwave/kms.json` is the installed default for 544x960.
This explicit mode is necessary: GTK can report its initial 720x400 window in
EDID, which overrides QEMU's xres/yres and causes Qt to letterbox the entire
phone into a tiny strip. The Linux connector name contains a hyphen; Qt's name
does not.

Swipe up from the bottom edge or tap the chevron above the dock to open the
drawer. Drag its handle down to close it. Tap the bottom home area or press
Escape, Back, or Home to return home. Short swipes return to their starting
state. Icons show pressed feedback and an unavailable-app notice when tapped.
Installed entries use the loading transition described below. OceanSettings is
the first separate app; live clock/network/battery services are not included.

## Installed-app loading transition

`Main.installedApps` comes from AppController, which recognizes Settings only
when its installed desktop entry and executable are present. Placeholder icons
continue to show the unavailable-app notice.

Tapping an installed entry in either the dock or drawer hides the original
icon and animates a copy, with its title, from the tapped position to the
reference loading position. Other apps fade away, the wallpaper blurs and dims,
and a loading wheel fades in and rotates clockwise. The status row stays visible.
Once the Wayland client presents a frame, its surface fades in over the loading
screen while the icon, title, and wheel fade out. The loading view stays until
the app is fully opaque, so Home does not flash between the two scenes.
The top chevron cancels loading and restores the starting view; Home, Back,
and Escape cancel loading and return home. Repeated taps are ignored while loading.

After `appLaunchRequested(appId)`, AppController starts `/usr/bin/ocean-settings`
with the Wayland platform plugin, `WAYLAND_DISPLAY=wayland-0`, and
`XDG_RUNTIME_DIR=/run/ocean`. Ocean remains the EGLFS/DRM owner. Its XDG shell
receives the Settings toplevel and displays it in the application layer below
the compositor-owned status icons. The client background extends beneath the
transparent status row, while its controls begin below that row. Home hides the
client surface without killing
the process; reopening Settings shows the existing surface. Closing Settings
destroys that surface and reveals Ocean Home. The standalone Settings UI and
backend remain in their own process.

The transition is implemented in `OceanLauncher/AppLoading.qml`, wired through
`OceanLauncher/Main.qml`, `OceanLauncher/AppIcon.qml`, and
`OceanLauncher/DrawerAppIcon.qml`, and registered in
`OceanLauncher/CMakeLists.txt`. Regression coverage is in
`tests/tst_controls.qml`; this README documents the integration contract.

Loading validation: all 14 QtTest results passed, including the installed gate,
dock and drawer transitions, cancellation before launch, completion/failure,
repeat taps, clockwise rotation, and scaled windows. A separate QEMU test image
supplied two installed IDs only in its preview harness and verified the blur,
loading layout, top-chevron cancellation, and dock launch using guest pointer
input. Captures are `out/app-loading-qemu-{settings,phone,cancelled}.png`.
The production launcher was rebuilt into `rootfs` and packed into the existing
`OpenWave.img`; the image was cleanly unmounted and passed `e2fsck -fn`.

## Settings

Settings lives in the separate [OceanSettings project](OceanSettings/README.md)
within this folder. Its CMake project, window, backend, resources and tests live
there. It installs
as `/usr/bin/ocean-settings` with `/usr/share/applications/ocean-settings.desktop`.

System preferences are shared at `/var/lib/openwave/OpenWave/settings.ini`.
The old `OpenWave/Ocean Launcher/settings.ini` is migrated without overwriting
an existing new configuration. The launcher reads Reduce motion from this shared
file each time it starts; it no longer links the Settings backend.

The QEMU helper uses a USB absolute tablet. Its kernel command line includes
`OPENWAVE_QEMU_POINTER=1`, which tells init to find the QEMU USB tablet by name
and configure Qt's evdev mouse backend with its actual event node and `abs`.
This avoids hard-coded event numbers and the VirtIO tablet mismatch where Qt
expects touch releases while QEMU sends mouse-button releases. Other boots keep
normal Qt device discovery; an explicit `QT_QPA_EVDEV_MOUSE_PARAMETERS` override
is preserved.

For normal updates, rebuild into `rootfs`, then update the existing image
through a verified mount as described in `../.codex/AGENTS.md`. The `--image`
option above is only for explicitly creating a separate image. Do not boot an
image while it is mounted. Boot the updated default image with:

```sh
./run-openwave run
```

Run the launcher control regression tests using the host Qt tools:

```sh
QT_QPA_PLATFORM=offscreen \
QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu \
QT_QUICK_BACKEND=software \
toolchain/bin/qmltestrunner -input OceanLauncher/tests
```

Validation: all 11 QtTest results passed (8 control tests, a real-font-weight check,
plus setup/cleanup).
The affected target files were rebuilt, installed in `rootfs`, and copied
into the existing `OpenWave.img` through a verified FUSE mount. The mount was
cleanly removed and `e2fsck -fn` passed after FUSE finished flushing.
A QEMU snapshot boot verified 768x1366 output, mouse swipe open/close and
settling, handle and home taps, icon feedback, and Escape. Screenshots are
`out/controls-verified-*.png`; the boot log is `out/ocean-controls-boot.log`.
The pre-change image is preserved as `out/OpenWave.before-controls.img`.
The base filesystem still emits its existing missing UTF-8 locale warning.

## Rendering quality

`IconImage.qml` is shared by dock, drawer, and status icons. It rasterizes at
2x physical display size (including the UI scale and device pixel ratio) and
uses smooth mipmap filtering for downscaling. The wallpaper also uses mipmaps;
its original landscape artwork remains aspect-cropped into the portrait screen.
The original artwork and angular icon outlines are preserved.

Inter Light and Regular are loaded by QML, so the installed launcher and direct
QML previews use the same fonts. Their actual resolved faces are checked in
`tst_controls.qml`. The bundled font license and Apache notice accompany both
the source resources and installed runtime fonts.

The default GTK display explicitly uses `zoom-to-fit=off`. For a pixel-for-pixel
reference, start with `./run-openwave run`; avoid stretching the QEMU output
in its View menu. A smaller QML preview renders its text and images at the
smaller size; resizing an already-rendered guest framebuffer is a separate host
scaling operation.

Quality was inspected from native 768x1366 QEMU captures and a 461x820 desktop
preview. Before/after captures are `out/quality-before-{home,drawer}.png` and
`out/quality-after-{home,drawer}.png`. The small preview is
`out/quality-small-drawer.png`. QEMU input regression checks passed after the
rendering changes. The same installed binary and fonts were packed into the
existing `OpenWave.img`; the original image is backed up at
`out/OpenWave.before-quality.img`.

Files changed for this update:

- `OceanLauncher/OceanLauncher/IconImage.qml` (new shared renderer)
- `OceanLauncher/OceanLauncher/AppIcon.qml`
- `OceanLauncher/OceanLauncher/DrawerAppIcon.qml`
- `OceanLauncher/OceanLauncher/Main.qml`
- `OceanLauncher/OceanLauncher/main.cpp`
- `OceanLauncher/OceanLauncher/CMakeLists.txt`
- `OceanLauncher/OceanLauncher/assets/fonts/Inter-Regular.otf`
- `OceanLauncher/OceanLauncher/assets/fonts/Inter-Light.otf`
- `OceanLauncher/OceanLauncher/assets/fonts/Inter-LICENSE.txt`
- `OceanLauncher/OceanLauncher/assets/fonts/Inter-Apache-2.0.txt`
- `OceanLauncher/tests/tst_controls.qml`
- `run-openwave`
- `OceanLauncher/README.md`

## Physical window verification

The GTK issue was reproduced through `run-openwave`, with the guest
selecting a 720x400 framebuffer despite GPU xres/yres of 768x1366. With the
forced connector mode, GTK runs selected 544x960 and 768x1366 as requested.
The final default-run log is `out/display-final-default.log`; the explicit
full-resolution run is `out/display-forced-probe.log`. Earlier headless-only
checks did not reproduce this GTK EDID behavior.

The window fix changes `run-openwave`, `userspace/init.c`,
`userspace/config/kms.json`, and this README. Init and the installed config were
rebuilt/staged and copied through a verified mount into the existing
`OpenWave.img`, then unmounted and checked with `e2fsck -fn`. The pre-fix image
is preserved as `out/OpenWave.before-window-fix.img`.
