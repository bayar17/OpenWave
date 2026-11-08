# Waydroid integration audit and recovery

This is an incomplete integration. Android framework boot, Ocean Android
windows, native Android shortcuts and Back-only navigation have not passed.
Do not use a successful source build or Waydroid CLI exit as Android boot proof.
This report separates the 2026-11-06 checkout audit from current guest evidence.

## Current architecture

| Component | Current source/runtime | Finding |
| --- | --- | --- |
| Upstream runtime | `third_party/waydroid`, commit `5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991` | Clean pinned Waydroid 1.6.3 checkout; original lifecycle implementation retained. |
| Packaging | `build-openwave: build_waydroid_host_deps` | Referenced two absent `userspace/waydroid` helpers. They have been reconstructed and tested. |
| Manager adapter | `userspace/waydroid/waydroidd.c` | Previously an installed binary without its source. Now a rebuildable fixed-argument OpenRC shim that executes upstream Python, with a fixed environment and no arbitrary commands. |
| OpenRC | `userspace/openrc/etc/init.d/waydroid{,-runtime}` | Optional service, deliberately disabled in the default runlevel until initialization and confinement are validated. Native services do not depend on it. |
| Android images | Host cache `/var/lib/waydroid/images` | Both images report Android 17, SDK 37, incremental `1792184876`, security patch `2026-10-01`. Copies are staged at `rootfs/usr/share/waydroid-extra/images`, an upstream-supported preinstalled-image location. No host Android user data was copied. |
| Kernel | Reference `out/linux/.config` versus separate `out/linux-waydroid-20261106` | Reference lacks Binder, loop devices, EROFS, bridge/veth and Netfilter. The separate kernel builds these in and retains SELinux, Landlock and seccomp. |
| Python | Target runtime Python 3.14.9 | Stale `python3 -> python3.12` link and missing `/usr/bin/env` prevented upstream CLI execution. Assembly now restores the correct target launchers. |
| Python GI | Target PyGObject 3.54.5 | Existing `_gi` used libffi.so.8 while target GIRepository/GObject used libffi.so.7. GLib function invocation crashed. Rebuilding against SDK headers/libraries fixes the reproduced failure. |
| LXC | Installed target LXC 6.0.6 tools and `liblxc.so.1.8.0` | User image 49 was uninitialized. Guest test 54 successfully creates upstream configuration; Android execution has not yet passed. |
| SELinux | `userspace/security/selinux/{openwave.cil,file_contexts}` and generated `services.cil` | Repaired dedicated storage transitions and BinderFS labels. Test 54 initializes upstream under enforcing mode. Manager startup permissions are being validated separately; Android container mount/execution authority remains unvalidated. |
| Networking | Upstream network helper, target iproute2/legacy iptables/dnsmasq | Dependencies exist; separate kernel supplies missing facilities. Actual Android bridge, DHCP and Internet operation remain untested. |
| Graphics | Ocean QtWayland compositor, `AppController`, `Main.qml` | Native app surfaces are authenticated through appd identities. There is no validated Android task-to-surface mapping. Android's shared compositor PID cannot safely be treated as an ordinary native app leader. |
| Native registry | `userspace/appd/{registry.c,process.c,CMakeLists.txt}` | Current source scans native manifest bundles. The historical restrictive Waydroid desktop importer described in `THIRD-PARTY-INTEGRATION.md` is absent from this checkout and Git history examined. No working importer was available to reuse. |
| Icons and launcher | `OceanLauncher/OceanLauncher/{AppController.cpp,ApplicationMetadata.h}` | Ocean requires appd's explicit launchable flag; Android records must stay unavailable until their provider and authenticated surface path work. |
| Navigation | Ocean owns system navigation | No Android navigation control has been added. The requested Back-only design is retained as the acceptance requirement; Home and Recents must not be introduced. |

## Verified failures and repairs

The baseline guest is `out/boot-recovery/ocean-waydroid-audit-50.img`, based on
the user's latest closed image 49. Evidence is in
`out/boot-recovery/waydroid-50-baseline/{serial.log,result.json,waydroid.json}`.
SELinux remains enforcing. The guest reports stopped OpenRC Waydroid, missing
Binder filesystem/devices, no initialized container or Android images, and
Waydroid CLI execution failure with exit 127. Framework probes are explicitly
BLOCKED when the container is not running. The diagnostic runner returns a
failure instead of treating a complete native boot as Android success.

The launcher repair exposed a further failure on the actual target runtime:
`GLib.option_error_quark()` segfaulted through GI. The reproducer is recorded
in `out/boot-recovery/waydroid-gi-failure.log`. After the target PyGObject rebuild,
the same GLib invocation succeeds, imports of dbus/GI/gbinder succeed, and the
unmodified Waydroid CLI reports `1.6.3`. Evidence is
`waydroid-pygobject-runtime.log`; `llvm-readelf` confirms `_gi` now needs the
same libffi SONAME as GIRepository and GObject. No host libraries were copied.
The extension build installs target CPython development headers into the SDK;
the build rejects Meson configuration that falls back to host Python headers.

Additional source and staging repairs:

- Added the missing package preparation and integrity verifier. Every upstream
  implementation/config/resource file is checked against the pinned commit.
  Only removal of unsupported AppArmor assignments in the two LXC templates is
  accepted. Python/networking implementation tampering fails validation.
- Removed the legacy `id.waydro.Container.service` D-Bus activation entry from
  staging and excluded it from assembly, preserving OpenRC's sole ownership.
- Recovered an unused OpenWave catalog prototype from the old staged package
  into `userspace/waydroid/legacy/`. Current upstream does not import it and
  current appd lacks its catalog methods. It is preserved as source rather
  than installed as an extra module inside the pinned upstream package.
- Kept Waydroid icons and compatibility metadata in runtime assembly.
- Added dedicated snapshot, container-state, session-runtime and user-storage
  labels. OpenRC can create the named paths; ordinary apps cannot read or
  mutate Android private files. The user-data path no longer shares the broad
  generic private-data label.
- Added an OpenRC initialization precondition and explicit service logs. A
  missing config/rootfs fails predictably before a manager launch is attempted.
- Added bounded guest probes and a parser that requires actual running
  servicemanager/zygote, a live `system_server` PID, nonempty PackageManager output and exact
  `sys.boot_completed=1`. Empty successful commands cannot pass acceptance.
- Added an OpenWave-owned D-Bus policy loaded after upstream's compatibility
  policy. Ordinary native clients cannot directly start/stop the container or
  initialize Android. The dedicated Waydroid account has only the upstream
  session methods; root retains administrator operations. A real private-bus
  test checks allowed routing, denied initializer calls and attempts to bypass
  the restriction through the manager's unique bus name.

### Runtime evidence through diagnostic image 57

Every run below uses `-snapshot`, records image/kernel SHA-256 before and after,
and powers the guest down through ACPI. The user image 49 remains unchanged.
These are diagnostic candidates, not a completed Waydroid release.

| Check | Result | Evidence |
| --- | --- | --- |
| Native OpenWave boot and host SELinux enforcing | PASS | `waydroid-57-acceptance/{serial.log,result.json}` |
| Runtime directory ownership, modes and labels | PASS | Image 57 `provisioning` and `runtime` probes |
| Built-in BinderFS and upstream Binder allocation | PASS | Image 57 `binder_labels` confirms all three character devices and the separate control label |
| Preinstalled Android 17 image detection | PASS | Image 57 `image=system/vendor present=yes` |
| Upstream Waydroid initialization | PASS | Image 57 `initialize exit=0`, `initialized=yes` |
| OpenRC manager readiness | PASS | Image 57 startup succeeds, bus owner PID matches the supervised process, and GLib callback allocation errors are absent |
| LXC container | FAIL | Image 57 `container` reports `STOPPED` |
| Android init, servicemanager, zygote, system_server, PackageManager, boot_completed | BLOCKED | No running Android container |
| Android networking | FAIL | Image 57 reaches the upstream network script; bridge creation is denied in the manager domain |
| Ocean Android surfaces, keyboard/touch, appd catalog and Back | NOT TESTED | Later phases require framework boot |
| Orderly guest poweroff and immutable input images | PASS | Image 57 `result.json` records guest powerdown, QEMU exit and unchanged hashes |

Image 57 is `out/boot-recovery/ocean-waydroid-diagnostic-57.img`, SHA-256
`7bc229e80e6befa47f73bfe7f04f60e0a3866b626023546949be8622b824a872`.
Its kernel is `out/boot-recovery/bzImage-waydroid-51`, SHA-256
`ec9c42f1b16b6935b291926a7899e277d697db9fde5927472d40b76600451e0a`.
Image 57 uses read-only public account lookup and a bounded root QA
request to upstream's session API, using UID/GID 990 and private Android data.
That diagnostic request does not certify a running unprivileged session or
authenticated Ocean application surfaces.

Image 58 is being validated with a separate network-script domain. Its network
capabilities do not belong to the Python manager. Sysctl writes are limited to
IPv4 forwarding and the Waydroid bridge's IPv6 DAD control; writable files are
dedicated network runtime state, the xtables lock and the Waydroid DHCP lease.
The policy tests reject host mount/reboot authority, generic sysctl/proc writes,
runtime modification and access to Android user files from that domain.

The OpenRC runtime adapter also recreates ephemeral Binder devices through the
pinned upstream helper on each boot, without running Android initialization or
resetting configuration/overlays. It checks the supervisor identity and device
labels. The new QA probe remounts BinderFS and checks configuration preservation;
this is separate from an eventual full Android reboot/persistence test.

The two Android images use the channels supplied by the user:

```text
https://waydroid-atv.github.io/ota/a17-qpr0/system/lineage/waydroid_x86_64/GAPPS.json
https://waydroid-atv.github.io/ota/a17-qpr0/vendor/waydroid_x86_64/MAINLINE.json
```

Raw image hashes and sizes are in
`out/boot-recovery/waydroid-android17-images.json`; build properties are in
`waydroid-android17-metadata.json`. The host installation was not reinitialized.
Its images were read and copied; its `/data`, container configuration and
services were not changed.

## Reproduce the builds and checks

```sh
cd /mnt/aosp/ThunderOS
./build-openwave waydroid-bindings
./build-openwave selinux
bash userspace/waydroid/build-kernel.sh out/linux-waydroid
python3 userspace/waydroid/prepare_apparmor_free.py --package rootfs
python3 userspace/waydroid/verify_upstream_package.py \
  --source third_party/waydroid --package rootfs --no-dbus-activation
python3 -m unittest scripts.tests.test_waydroid_report \
  scripts.tests.test_assemble_python_runtime
python3 -m unittest discover -s userspace/waydroid/tests -p 'test*.py'
python3 -m unittest discover -s userspace/security/selinux/tests -p 'test*.py'
python3 userspace/waydroid/audit.py \
  --kernel-config out/linux-waydroid/.config
```

`waydroid-bindings` installs the rebuilt module into the SDK and userspace
install staging; the rootfs assembler installs runtime files. Development
headers stay outside `rootfs`. The manager shim is wired into the normal
userspace CMake build. A direct targeted build used for this recovery is:

```sh
ROOT=/mnt/aosp/ThunderOS
"$ROOT/toolchain/bin/clang" --target=x86_64-unknown-openwave \
  --sysroot="$ROOT/toolchain/sysroot/x86_64-unknown-openwave" \
  -O2 -Wall -Wextra -Werror userspace/waydroid/waydroidd.c \
  -o out/boot-recovery/waydroidd-current
```

Normal package rebuilding remains `./build-openwave waydroid-host-deps` and
now includes the matching bindings build. `build-openwave image ROOTFS NEW_IMAGE`
is the normal fresh-image packaging interface; use sufficient
`OPENWAVE_IMAGE_SIZE` for the 2.6 GiB Android image pair plus runtime and data.
Do not replace the user's image with a freshly assembled empty user profile.

## Integration still required

Upstream initialization passes in guest test 54, with dedicated BinderFS
labels and host enforcement retained. The remaining prerequisite is an
operational confined manager and Android container. The manager starts a
D-Bus loop; it starts Android only after an upstream session `Start` request.
A manager process or its bus name alone is therefore insufficient evidence.

Android image init scripts contain labels such as `u:r:ueventd:s0` that are
absent from OpenWave's host policy. These strings alone do not prove that
Waydroid attempts the corresponding transitions. The inspected ATV
`lineage-24.0` source skips `selinux_setup` in first-stage init and compiles
out service `setexeccon` and property MAC checks for Waydroid. This source
inspection is not proof of the exact image build or of successful execution.
References: ATV [first-stage init](https://github.com/WayDroid-ATV/android_system_core/blob/lineage-24.0/init/first_stage_init.cpp),
[service setup](https://github.com/WayDroid-ATV/android_system_core/blob/lineage-24.0/init/service.cpp),
and [property service](https://github.com/WayDroid-ATV/android_system_core/blob/lineage-24.0/init/property_service.cpp).
Resolve actual guest denials; do not import Android's entire policy or disable
host enforcement based on those strings. Android image file labels also need
runtime verification when their filesystems are mounted.

After the framework boots, integrate upstream multi-window mode with an
authenticated appd provider, package-event synchronization and icons. Upstream
documents `persist.waydroid.multi_windows` for desktop window integration:
[Waydroid property options](https://docs.waydro.id/usage/waydroid-prop-options).
Do not trust arbitrary desktop Exec strings, client-supplied surface app IDs,
or the short-lived launch CLI PID as the Android application identity.

Implement Back through a narrowly authenticated foreground-Android operation;
never expose generic `input`, arbitrary keycodes or shell commands. Back must
be tested against activities, dialogs, keyboards, WebViews and the final
activity. Shortcut synchronization, reboot persistence, service restart/recovery
and unavailable-provider handling remain unverified until the framework and
surface mapping work. No native app sandbox was relaxed to bypass these gaps.
