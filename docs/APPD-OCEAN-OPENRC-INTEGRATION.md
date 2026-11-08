# appd and Ocean under OpenRC

The newer [power and USB Wi-Fi recovery](POWER-USB-WIFI-RECOVERY.md) records
the current candidate and launch command, including the confined-app DNS repair.
The sustained-runtime run below supersedes the narrower run-13 first-frame
acceptance; later candidate results are recorded in the linked follow-ups.

The original run-13 acceptance below covered first presentation followed by
requested termination. A subsequent manual-use report exposed failures after
that point. Sustained-runtime checks now hold each of all five apps for ten
seconds, including Browser and Photos; the original first-frame results do not
establish sustained operation.

The integration keeps OpenRC in charge of system services, appd in charge of
application eligibility and isolation, and Ocean in charge of the compositor
and visual handoff. The launcher layout, gestures and animation timings are
unchanged by these integration repairs.

## Diagnosed defects

| Classification | Evidence | Correction |
| --- | --- | --- |
| Implementation | `appd_info()` lacked the boolean `launchable` consumed by Ocean. | Serialize explicit eligibility; reject missing or incorrectly typed eligibility in Ocean. |
| Implementation | `trusted_renderer()` read an unversioned PID file instead of checking start time. | Verify the protected registrar PID/UID/start-time proof and actual executable. |
| Service integration | The deployed prebuilt Applications library called `VerifyApplicationsService`, absent from the local permissiond implementation. | Rebuild the local Applications source and stage it in the SDK and rootfs. |
| Service integration | D-Bus name ownership can precede OpenRC's identity publication. | Recheck the authenticated owner after proof publication and replacement. |
| Architectural integration | permissiond exited on appd replacement expecting the obsolete PID 1 supervisor to restart it. | Invalidate old leases, watchers and pending requests, then authenticate and load the replacement registry. |
| Implementation | QML completion announced readiness without compositor-created or rendered-frame evidence. | Gate the handshake on Qt compositor creation, a rendered frame, an owned Wayland socket and successful appd reply. |
| Implementation | appd created trusted-app storage as root and did not transfer it to the execution UID. | Reuse protected data-owner provisioning for trusted applications. |
| Implementation | Ordinary children inherited protected supervisor log descriptors; SELinux correctly rejected their diagnostics. | Give each child private, unlinked output files and report escaped, bounded output on exit. |
| Implementation | Recovered Controls Gallery QML used `ButtonGroup` without importing Qt Quick Controls. | Add the missing import and rebuild the Gallery against the target SDK. |
| Implementation | The account allocator opened all ancestors for reading. | Pin ancestors with `O_PATH` while retaining protected-directory validation. |
| Configuration | Qt constructs KMS names as `Virtual1`, while the kernel reports `Virtual-1`. | Use `Virtual1` in Qt JSON and keep `Virtual-1` in kernel arguments. |
| Security labels | The supervisor fallback regex overrode app runtime regexes. | Order specific runtime rules after the fallback and test libselinux matching. |
| Security policy | cgroup v2 checks the common ancestor's membership control file during migration. | Label `/cgroup.procs` with the existing appd controller type. |
| Security policy | `qt610-core2-boot/serial.log` shows bundle traversal, udev monitor, cache link and runtime-directory denials. | Add scoped Ocean access for these observed operations and label `/dev/tty` as a console. |

## Metadata contract

`ListApplications` and `GetApplicationInfo` return `a{sv}` records with a boolean
`launchable`. A validated application is launchable unless being removed.
The `com.openwave.shell` compositor is excluded by manifest parsing even if its
role field is omitted, and its serialized eligibility is always false. Unknown
component roles and unsupported runtimes stay rejected. Ocean treats absent or
non-boolean eligibility as false. Existing digest, permissions, trust, isolation,
state and executable fields retain their meanings.

The registry scans both `/System/Applications` and `/Applications`. Metadata and
executables must be installed as protected root-owned files. Duplicate IDs fail
closed. The compositor is started by `openwave-ocean`, never through appd.

## Supervision and readiness

`appd` and `openwave-ocean` execute their real foreground programs through
OpenRC's `start-stop-daemon` background facility. Their root-owned PID files
identify executable processes. `start_pre` invalidates old proofs; `start_post`
checks the executable, UID and process start ticks before atomically publishing
a root-owned proof. `stop_post` invalidates it. Readers reject stale start ticks,
incorrect UIDs, writable records, symlinks, and incorrect SELinux proof labels.

Ocean's source-built client requests `org.openwave.Shell` on its authenticated
system-bus connection. appd uses the connection's PID/UID and protected Ocean
proof for `RegisterShell`. Ocean tracks `initializing`, `compositor-ready`,
`session-ready` and `failed`. Qt's `createdChanged` and `frameSwapped` provide
startup evidence; appd independently checks and publishes the Wayland socket.
A retry timer handles service/proof availability and a rejected handshake, not
graphics initialization timing.

## Validation evidence

The target `userspace/appd/tests/registry-contract` executes the production
serializer, ApplicationsClient wire decoder, and the predicate used by Ocean.
It covers visible native applications, removing apps, compositor exclusion,
missing eligibility, incorrect field types and non-launchable components.
Existing registrar and identity-reader tests cover proof ownership, symlinks,
start-time changes and SELinux labels. Policy tests retain ordinary-app
neverallows and reject writable installed runtime.

Guest validation results are recorded separately for each uniquely named image
under `out/boot-recovery`. Booting and shutdown alone are not proof of a working
app drawer or application surface. The final guest acceptance run is recorded
below, separately from earlier partial runs.

## Recorded guest progress

`ocean-integration-04.img` booted with SELinux enforcing, authenticated appd and
Ocean, populated five application records, completed session readiness and
rendered Ocean's home screen at 544x960. The actual local GTK window was captured
in `out/boot-recovery/ocean-integration-04-boot/gtk-window.png` (the QMP screenshot
operation cannot capture this VirGL scanout and returns `no surface`).

Settings and Files reached their dedicated SELinux domains but encountered
missing appd descriptor-inheritance permissions. The regular Gallery launch was
blocked during Bubblewrap namespace setup. Launch acceptance and restart
acceptance were therefore NOT met by that image. The immutable snapshot base
and clean ACPI shutdown are recorded in that run's `result.json`.

The actual Mesa SDK build had `platforms=[]`, so it provided GBM/EGLFS without
Wayland EGL. The repaired build helper keeps VirGL and softpipe, enables Wayland
and Mesa 26's optional `bind-wayland-display` support required by Qt 6.10, uses
only target libraries and a separate host Wayland scanner, and records installed
library hashes in the SDK. Runtime assembly must use this complete install
before the next image is packed.

The production Ocean controller is also exercised with a test transport: missing
or malformed eligibility stays hidden, compositor creation alone cannot send a
true readiness request, an absent socket fails closed, a rendered frame plus an
owned socket completes the handshake, repeated frames do not repeat the RPC,
and compositor invalidation revokes readiness.

`ocean-integration-07-boot/serial.log` records PASS for both Settings and Files:
a real compositor surface, expected UID 1000 and dedicated domain, no_new_privs,
zero effective capabilities, and successful requested termination. The ordinary
app was still blocked on temporary-filesystem mount authorization in that run;
its mandatory sandbox was not bypassed.

SELinux filesystem control inodes use the fixed security SID in this kernel,
including the context-validation file. appd may query context validity, and
explicit neverallows prohibit its policy loading, enforcement changes and boolean
changes. Ordinary-app capability, ptrace, transition and runtime-write guards
remain active. Bubblewrap's temporary content has a separate sandbox label;
installed runtime file writes remain unauthorized.

`ocean-integration-08-boot/serial.log` also records successful actual OpenRC
restarts of `appd` and `openwave-ocean`, replacement PID/start-time proofs, and
successful Files surfaces and termination after each restart. permissiond stays
running, invalidates the old registry session and authenticates the replacement.

Bubblewrap 0.11.1 did not apply `--file-label` to its initial temporary-root
mount. `cross/bubblewrap/root-label.patch`, applied idempotently by the normal
build helper, fixes that mount to use the requested label. appd is not granted
write access to installed runtime directories to work around this defect.

`ocean-integration-10-boot/drawer-window.png` captures the actual GTK window with
the drawer open, showing three favorites and two additional entries at 544x960.
`ocean-integration-11-boot/serial.log` confirms the new private output capture:
Gallery reached QML loading, then failed on `ButtonGroup is not a type`.
Its missing `QtQuick.Controls` import was repaired; a target-SDK offscreen run
then stayed running without a QML component-loading error. Native application
launches and both OpenRC restarts continued to pass in run 11; that run does
not meet ordinary-app launch acceptance.

`ocean-integration-12-boot/serial.log` records successful ordinary Gallery
surfaces and termination, including after both OpenRC restarts. The process ran
as UID 200000 in `openwave_app_t`, with seccomp mode 2, no_new_privs, zero effective
capabilities, distinct mount/IPC/UTS namespaces and `pids.max=512`. Landlock
installation remains mandatory before exec. That run's aggregate result is
intentionally a failure: a newly added crash probe tried to send SIGKILL from
the QA supervisor domain to an ordinary app, which the policy correctly denied.
The revised probe exercises crash reporting and relaunch with Settings, using
existing supervisor-to-native-app signal permissions. It adds no policy grant.

## Final acceptance: ocean-integration-13

The completed confirmation run is
`out/boot-recovery/ocean-integration-13-confirm-boot/`. Its `result.json` records
an enforcing boot, the complete application integration PASS marker, QEMU exit 0
after ACPI powerdown, no kernel panic, and unchanged image/kernel hashes.
The earlier run of the same image also passed the guest suite, but its host
process ended before shutdown results were written; use the confirmation run
for the complete result.

| Check | Result |
| --- | --- |
| OpenRC appd and Ocean startup | PASS; actual process identity proofs and system D-Bus authentication |
| Readiness | PASS; compositor creation, first rendered frame, owned socket and successful handshake |
| App drawer | PASS; five entries, three favorites; actual GTK capture `drawer-window.png` |
| Settings and Files | PASS; presented surfaces, UID 1000, dedicated SELinux domains, no_new_privs, zero capabilities, termination |
| Ordinary Controls Gallery | PASS; presented surface, UID 200000, `openwave_app_t`, seccomp mode 2, distinct mount/IPC/UTS namespaces, `pids.max=512`, termination |
| Mandatory Landlock | PASS through the fail-closed production child setup before ordinary-app exec |
| appd restart | PASS; new PID/start-time proof, Files and ordinary Gallery launch again |
| Ocean restart | PASS; new PID/start-time proof, Files and ordinary Gallery launch again |
| Crash handling | PASS; forced Settings crash reported as Crashed with PID 0, then successful relaunch and termination |
| SELinux and shutdown | PASS; enforcing throughout application checks, clean ACPI powerdown, base image unchanged |

The launch, readiness and presentation code preserves the existing launcher
layout, gestures and animation definitions. QA-only drawer startup uses Ocean's
existing `--drawer` option; normal boot does not request automated launches.

Generated raw ext4 image (2 GiB):
`out/boot-recovery/ocean-integration-13.img`.

Image SHA-256: `4b0962f1a8ca9e9a999959d305db3c585bf15d59b78a036b57f5137239db73e4`.

Kernel: `out/linux/arch/x86/boot/bzImage`.
Kernel SHA-256: `b1fdf19b9ebd72cf5cd622f45c07826a37776e09f01e5807f02559f1f970156f`.

The normal pipeline verified source-policy consistency, 33 core runtime ELF
programs, an acyclic OpenRC dependency graph, 38 enabled services and 21 command
ELFs, read-only ext4 checks, and 9922 SELinux xattrs over 37 labels. The image
contains the staged appd, Applications client, Ocean, permissiond, Mesa,
Bubblewrap and Gallery repairs. Existing `OpenWave.img` and previous candidate
images were not replaced, and no image remains mounted.

The current-source regression checks passed: serializer/client/Ocean contract
(seven checks), production manifest scanning in both roots, production Ocean
readiness/controller behavior, account allocation (eight checks plus readonly
NSS namespace probe), environment and private output capture, protected service
identity registrar (six tests) and consumer (five), SELinux policy (three) and
actual context matching, graphics runtime verification, image pipeline (seven),
QEMU runner (ten), service readiness (six) and DRM device policy (seven).
Logs are under `out/boot-recovery/integration-final-*` and
`out/boot-recovery/integration-output-*`. All affected target builds and the
Gallery offscreen QML check succeeded before installation and packing.

Remaining observations: this establishes appd/Ocean launch integration, not
complete system-service health. Wi-Fi/Bluetooth/audio have separate startup or
kernel-feature issues and nonblocking AVCs remain in the captured log. The new
Ocean profile has no selected wallpaper. Qt reports a locale/font fallback and
Gallery reports an existing Flow anchor warning. These did not prevent the
verified application surfaces or lifecycle checks. Missing SDK implementation
sources are described below; existing exported Qt 6.10 libraries remain in use.

To run this exact image in the local GTK window:

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-integration-13.img \
  --usb-wifi none --usb-bluetooth none --audio-backend none
```

This ordinary runner invocation writes guest changes to the image. The recorded
QA run instead used QEMU's disposable `-snapshot` mode.

## Modified source paths for this integration task

- `userspace/appd/registry.c`, `manifest.cpp`, `dbus.c`, `isolation.c`,
  `namespace.c`, `process.c`: eligibility, renderer proofs, diagnostics, storage
  ownership and labeled namespace setup.
- `userspace/application-client/ApplicationsClient.cpp`: source-built authenticated
  transport, shell name ownership, proof-publication retries and diagnostics.
- `OceanLauncher/OceanLauncher/AppController.{h,cpp}`, `ApplicationMetadata.h`,
  `main.cpp`, `Main.qml`: strict metadata consumption and lifecycle-based readiness.
- `userspace/permissiond/PermissionService.{h,cpp}`: safe registry reconnection.
- `userspace/wallpaperagentd/main.c`: read-only supervised-Ocean authentication.
- `userspace/security/selinux/openwave.cil`, `file_contexts`: scoped IPC, render,
  application lifecycle, cgroup and sandbox permissions and correct rule order.
- `userspace/openrc/etc/init.d/openwave-ocean`, `kms.json`, `validate_openrc.py`,
  `userspace/config/kms.json`: diagnostic environment and Qt KMS configuration.
- `userspace/openrc/qa/app-launch.start`, `scripts/test-openwave-boot.py`: enforcing
  trusted/untrusted launch, surface, isolation, termination and restart checks.
- `scripts/build-mesa.sh`, `assemble-openwave-rootfs.py`, `prepare-openwave-boot.py`,
  `openwave_graphics_runtime.py`: complete Wayland Mesa export, runtime verification,
  fresh source service staging and image preparation.
- `build-openwave`, `cross/bubblewrap/root-label.patch`: reproducible Bubblewrap fix.
- `userspace/controls-gallery/Main.qml`: import the Controls types it uses.
- Regression tests under `userspace/appd/tests/registry-contract`,
  `userspace/appd/tests/accounts.c`, `userspace/security/selinux/tests`,
  `userspace/openrc/tests/test_drm_device_policy.py`, and
  `scripts/tests/test_graphics_runtime.py`.
- `userspace/appd/README.md` and this report: contracts and evidence.

Other pre-existing dirty UI changes are preserved. The SDK's existing Qt 6.10
Application/Controls/Theme implementations remain in use because their source
implementations are absent from the checkout and available Git history; this
work does not claim that those missing implementations were rebuilt from source.

## Manual-use follow-up: apps exit after presentation

The user reproduced closures with the ordinary `run-openwave --window` command
against `ocean-integration-13.img`, without QA launch tokens. A read-only check
found its ext4 journal needed recovery after that run. Recovery was performed
only on `out/boot-recovery/app-exit-replay.img`, preserving the user's image.
The recovered profile confirms Browser, Controls Gallery, Settings and Files
were launched; tmpfs runtime logs cannot be recovered after shutdown.

The fresh enforcing `ocean-app-runtime-14` run reproduced real unexpected exits
without issuing termination first. Settings lasted seven seconds; subsequent
clients exited after their initial frame. Their captured stderr reports
`invalid arguments for wl_shm.create_pool` and a fatal Wayland connection error.
The matching AVCs deny Ocean `write` on `memfd:wayland-cursor`, owned by the
Settings, Files or ordinary-app domain. Linux `selinux_file_receive()` calls
`file_has_perm(..., file_to_av(file))`: an O_RDWR descriptor requires both read
and write authorization even when Wayland only maps its pixels. The correction
adds only Ocean-to-client `memfd_file write`, with installed-file and ordinary
app isolation guards retained. A compiled-policy regression asserts the
effective rights for all three client domains.

Browser independently failed QML component creation: `HistoryPage.qml` assigned
`fontFamily` to `WRComboBox`, which has no such property in the exported SDK.
The invalid assignment was removed; the SDK control already uses the shared
Fonts font. The target Browser was rebuilt and staged. A disposable namespace
check with the target offscreen plugin passed QML loading; its lack of EGL
cannot validate WPE rendering, which requires the actual guest run.

appd now records application ID, PID, raw wait status and requested-stop state
on exit, alongside the existing bounded child stderr. The guest test checks
Running state and the same PID throughout each ten-second hold.

The final enforcing GTK/VirGL run is
`out/boot-recovery/ocean-app-runtime-15-boot/`. `result.json` records successful
application integration, exit 0 after ACPI powerdown, no panic and unchanged
image/kernel hashes. `acceptance.txt` lists ten-second Running/same-PID holds
for Settings, Files, Controls Gallery, Photos and Browser, plus the successful
restart and crash-recovery checks. Pointer input was also injected over QMP;
no cursor-memory write denial or fatal wl_shm error occurred in this run.
Browser's WPE web and network helper executables started, and its surface and
process remained live through the ten-second observation. Internet navigation
was not tested: this invocation disables host network adapters.

Fixed image: `out/boot-recovery/ocean-app-runtime-15.img`.
SHA-256: `1a09e2b0f554ea335613ce29c21b47c09318664debf77eb493d36f7fa4850339`.

Additional changed files in this follow-up are
`OceanLauncher/OceanBrowser/HistoryPage.qml`, the two compositor-to-client memfd
rules in `userspace/security/selinux/openwave.cil`, its compiled-policy test,
the appd exit diagnostic in `userspace/appd/process.c`, and the sustained and
all-five-app coverage in `userspace/openrc/qa/app-launch.start`.
The target Browser and appd builds, four policy regressions, private-output
regression and seven image-pipeline tests passed. No UI geometry or animation
definition was changed.

Some unrelated warnings and AVCs remain, including the documented optional
service issues, wallpaper selection, fonts/locale, and appd's graceful socket
connection to ordinary clients (termination currently uses its existing signal
fallback). These were not relaxed to resolve the observed crashes. The
original user image 13, its profile data, and earlier candidates remain intact.

Run the fixed candidate with the same local-window setup:

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-app-runtime-15.img \
  --usb-wifi none --usb-bluetooth none --audio-backend none
```

The subsequent [native portal and Wallpaper recovery](PORTAL-WALLPAPER-OPENRC-RECOVERY.md)
is packaged as `out/boot-recovery/ocean-portals-23.img`. It includes these app
runtime fixes and passes Settings Files/Photos selection, crop/save and refresh
with SELinux enforcing, plus the separate five-app launch/restart regression.
Use that newer image when testing the repaired Wallpaper workflow.
