# Stage 2H — unblock appd rebuilding without incomplete WaterRouge SDK

## Failure

`./build-openwave userspace` currently reaches OceanLauncher and fails at

```
find_package(OpenWaveSDK REQUIRED COMPONENTS Controls Fonts Theme ...)
OpenWaveSDK_FOUND=FALSE
```

The current `userspace/sdk/CMakeLists.txt` / `OpenWaveSDKConfig.cmake.in`
does **not** implement or export `OpenWave::Application`,
`OpenWave::Controls` or `OpenWave::Theme`. WaterRouge Gallery
intentionally skips itself on missing targets, but Ocean Launcher,
Settings, Files and Photos require these APIs and therefore fail the
entire userspace CMake configuration.

Regenerating the SDK with `./build-openwave sdk` cannot create missing
sources or exports. Fake imported INTERFACE targets would silence the
error while leaving QML imports and controls nonfunctional. The Qt
`openwave-fontsplugin`, `openwave-webplugin`, `qtwpe` auto-link
messages are warnings and **not** the fatal configuration failure.

## Bounded workaround for appd Stage 2H only

The Stage 2H issue modifies `userspace/appd/process.c`, not a WaterRouge
component. A standalone CMake wrapper reuses the **actual production**
`userspace/appd/CMakeLists.txt` and the SELinux helper library source,
links only against real SDK components (`Xml`, `Identity`,
`Applications`), and installs appd, openwave-launch and namespace
keeper into the existing runtime staging tree. No placebo SDK targets.

`./build-openwave appd-only` **requires** a previously complete
runtime staging tree with executable Ocean Launcher, Settings, Files,
Photos, Browser, and WaterRouge Gallery. If any is missing it fails
closed and prints a diagnostic: restore the missing WaterRouge SDK
modules and rebuild the native apps rather than shipping an incomplete
root filesystem.

## Build and test the changed daemon

```bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch build/appd-only-stage2h-unblock-20261108 ||
  git switch --track origin/build/appd-only-stage2h-unblock-20261108

python3 -m unittest discover -s scripts/tests -p test_appd_only_build.py -v
python3 -m unittest discover -s scripts/tests -p test_app_control_ipc.py -v

# Does not configure Ocean/WaterRouge. Rebuilds the real appd executable.
./build-openwave appd-only

# Verify the updated executable reached the *staging* tree:
source ./env.sh --build
grep -aF 'appd: control quit-noack-exited appId=' \
  "$OPENWAVE_RUNTIME_STAGE/System/Services/appd"

# Assemble a new rootfs using existing UI staging and updated appd.
./build-openwave rootfs
grep -aF 'appd: control quit-noack-exited appId=' \
  rootfs/System/Services/appd

# Never overwrite the previous guest image.
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-app-control-2h-isolated.img

python3 scripts/test-openwave-boot.py \
  --app-launch --mode enforcing --seconds 180 \
  --image out/boot-recovery/ocean-app-control-2h-isolated.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/app-control-2h-isolated-evidence

E=out/boot-recovery/app-control-2h-isolated-evidence
cat "$E/result.json" "$E/app-control.json"
grep -aE 'APP_TEST CONTROL_|APP_CONTROL_(PASS|FAIL)|appd: control quit-' \
  "$E/serial.log" | tail -n 100
```

`result.json` must report `mode=enforcing`,
`app_launch_passed=true`, `app_control_passed=true` and unchanged
input hashes. The actual guest result is required; host CI is not
equivalent to a QEMU runtime pass.

## Full SDK repair (a separate feature)

Implement and export functional Qt/QML modules `OpenWave.Application`,
`OpenWave.Controls` and `OpenWave.Theme`, with the actual
`WRApplicationWindow`, `WRButton`, `WRSlider`, `WRTextField`,
`WRSwitch`, `WRComboBox`, `WRCheckBox`, `WRRadioButton`,
`WRListItem` and `WaterRouge` singleton contracts needed by
the installed applications. Install the generated QML module files,
backing libraries/plugins and exported CMake targets, and test both
configuration and loading against Qt 6.10. Do not `set(..._FOUND
TRUE)` for unimplemented components, disable required apps, change
SELinux policy or use guest binaries as substitutes for source code.

This isolated build path is solely a maintenance workaround and does
**not** claim the full userspace build is repaired.
