# QEMU keyboard recovery

The manual VM used `ocean-wifi-connected-35.img` with
`bzImage-power-wifi-28` and `-device virtio-keyboard-pci`. That kernel did not
enable `CONFIG_VIRTIO_INPUT`, so the keyboard selected by QEMU had no guest
driver. Qt 6.10.3 was also built with `FEATURE_xkbcommon=OFF`: Ocean's Wayland
compositor advertised no keymap and Qt's Wayland client discarded key events.

`build-openwave kernel` now requires the VirtIO input and evdev drivers.
`scripts/build-qtwayland-openwave.sh` explicitly enables XKB for target QtBase
as well as building the matching QtWayland compositor. The runtime needs the
target libxkbcommon library and `/usr/share/X11/xkb` data, already supplied by
the runtime assembler. Host tools remain outside the runtime.

The runtime stager now also handles this Qt build's actual `/usr/plugins` and
`/usr/qml` install directories, so rebuilding Qt updates its platform and QML
plugins along with its libraries.
It resolves runtime directory aliases inside the guest root and honors module
RUNPATHs, including `$ORIGIN`, during dependency validation. Regression tests
cover deployment, font aliases, escaping/looping links and a missing module
dependency.

No SELinux permissions were changed for this keyboard repair. Ocean already
owns the input-device access; applications receive keys and the keymap through
their Wayland connection. Applications do not gain raw input-device access.

The opt-in `openwave.qa=keyboard` fixture focuses an Ocean TextInput followed
by Settings' actual search field. The host sends QMP hardware key events for
`a`, `b`, `Shift+C`, `Backspace`, `Shift+D`. Each field must contain `abD`.
The fixture never synthesizes Qt key events or assigns the expected text.
Normal boots do not create the fixture or inspect user-entered text.

Run the integration test with `scripts/test-openwave-boot.py --keyboard` and
explicit image, kernel, output directory and time limit. The result requires
both field checks under enforcing SELinux. QMP acceptance alone cannot pass.
Evidence includes `serial.log`, `keyboard-input.json`, `command.json` and
`result.json`. Guest Ocean and appd logs are tmpfs-backed; the serial capture
preserves their QA results after orderly shutdown.

Qt's compose input context also needs X11 Compose tables. The first keyboard
test passed ASCII typing but exposed missing tables. Candidate 37 includes
64 architecture-independent data files and their license, imported from the
installed `libx11-data` package (2:1.8.13-1ubuntu0.1) into the target SDK by
`scripts/stage-openwave-compose.py`. The source hashes are recorded in
`out/boot-recovery/keyboard-compose-37-source.json`. The QtWayland build invokes
this importer; alternate data/license inputs can be supplied with
`OPENWAVE_X11_LOCALE_SOURCE` and `OPENWAVE_X11_COMPOSE_LICENSE`. The runtime
stager and assembler include the resulting data. No host ELF objects are
imported. The keyboard acceptance check also rejects a failed Compose table
initialization.

## Accepted image and kernel

Use both the updated image and kernel:

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-keyboard-37.img \
  --kernel out/boot-recovery/bzImage-keyboard-36 \
  --usb-wifi 04e8:2018 --usb-bluetooth none --audio-backend none
```

The image preserves the saved Wi-Fi profile and the previous network policy
repairs. The keyboard tests use no NIC or USB Wi-Fi passthrough; the earlier
physical association evidence remains documented in WIFI-ASSOCIATION-RECOVERY.md.
The previous image 35 was not modified by this repair.

`out/boot-recovery/keyboard-37-acceptance/result.json` reports
`keyboard_passed=true`, enforcing SELinux, a complete boot report, no kernel
panic, guest Power down and QEMU exit code 0 after ACPI/OpenRC shutdown. Both
input hashes remained unchanged. The serial log identifies the VirtIO keyboard
driver and records both field checks; `keyboard-input.json` records the QMP
hardware chords. There are no Compose initialization errors in the final run.
This verifies letters, Shift and Backspace in the shell and a normally managed
Settings Wayland client; it does not test every locale/layout.

`out/boot-recovery/keyboard-37-app-regression/result.json` also passes the
existing application suite. Settings, Files, Gallery, Photos and Browser each
held a live confined surface for ten seconds. Termination, crash recovery and
appd/Ocean restarts passed. This run also used enforcing SELinux, preserved both
input hashes, reported guest Power down and exited QEMU normally.

The runtime update comes from `rootfs`, with matching Ocean/Settings executables
also installed in userspace staging. Candidate 36 updated and verified 1,338
runtime entries; candidate 37 adds 65 Compose/license files. The update manifests
are `keyboard-runtime-36.json` and `keyboard-runtime-37.json` under
`out/boot-recovery`. Each new candidate passed read-only filesystem checks and
SELinux labeling. No image was mounted during packing.

Host validation passed 17 image/runner tests, two staging tests, OpenRC service
validation and shell/Python syntax checks. No test VM or image mount remained
after acceptance.

Changed implementation files:

- `build-openwave`: required kernel input drivers.
- `scripts/build-qtwayland-openwave.sh`, `scripts/stage-openwave-compose.py`,
  `scripts/stage-openwave-runtime.py`: target XKB, Compose data and runtime deployment.
- `userspace/openrc/qa/KeyboardHarness.h`, `userspace/openrc/qa/keyboard.start`,
  `OceanLauncher/OceanLauncher/main.cpp`, `OceanLauncher/OceanSettings/main.cpp`:
  opt-in field focus/observation through the normal application processes.
- `scripts/prepare-openwave-boot.py`, `scripts/test-openwave-boot.py`,
  `scripts/tests/test_stage_qt_runtime.py`: fixture deployment, hardware injection
  and staging regression checks.
- This document and links from `WIFI-ASSOCIATION-RECOVERY.md` and
  `POWER-USB-WIFI-RECOVERY.md`: current artifact pair and historical evidence.

Image SHA-256:
`3511695151f2a94c75a86eb86bd7d1ba795880402219994b77e83876c9595467`

Kernel SHA-256:
`1f6f1cddede7a78507cebd11fd852273740cbbe9352c33173108d023fb9a40ed`

Reproduce the keyboard check using a new evidence directory:

```sh
GDK_BACKEND=x11 python3 scripts/test-openwave-boot.py \
  --image "$PWD/out/boot-recovery/ocean-keyboard-37.img" \
  --kernel "$PWD/out/boot-recovery/bzImage-keyboard-36" \
  --output "$PWD/out/boot-recovery/my-keyboard-check" --seconds 80 --keyboard
```
