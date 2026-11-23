# OpenRC integration for OpenWave

OpenRC 0.64.1 is pinned in `third_party/openrc-0.64.1` at upstream commit
`83fb7385f4e9115a1c804b1c035bfbad04ebc46a`. The checkout is used unchanged.
`build-openrc-candidate.sh` cross-compiles the upstream project for
`x86_64-unknown-openwave`, builds OpenWave's small early-boot bootstrap and
runtime-label checker, and assembles a new candidate under `out/`. It refuses
to overwrite the candidate image.

To build a new disposable candidate from the existing runtime staging tree:

```bash
ROOT=/mnt/aosp/ThunderOS
cd "$ROOT"
OPENWAVE_OPENRC_OUTPUT="$ROOT/out/openrc-candidate-20261031" \
OPENWAVE_OPENRC_IMAGE_NAME="OpenWave-openrc.img" \
bash userspace/openrc/build-openrc-candidate.sh
```

Choose a fresh output directory for each candidate. The builder recreates its
intermediate staging directories and refuses an existing image. It compiles
OpenRC, BusyBox, the required mount utilities, OpenWave bootstrap/helpers, and
SELinux policy, then packs and validates the image. It does not boot QEMU.

The main builder also uses `build-acpid-candidate.sh --stage-only` to build
unchanged upstream acpid 2.0.34 from the archive pinned in `acpid-source.env`.
It installs `/usr/sbin/acpid`, the OpenRC service, and
`/etc/acpi/events/power`, and enables acpid in the boot runlevel. The event
rule requests `/sbin/openrc-shutdown -p now`. This handler is required for
QMP `system_powerdown` to request normal guest shutdown; a successful QMP reply
alone does not establish that the guest handled the event or powered off.

Wait for the builder's final image checksum and validation results. If a build
or candidate validation fails, fix that stage before packing; manually packing
an incomplete staging tree bypasses those checks. The main recipe previously
copied only the acpid service definition and omitted its binary, event rule,
and boot link. The corrected recipe stages all of them before validation.

`OPENWAVE_OPENRC_BASE_ROOT` is optional and defaults to `$ROOT/rootfs`; set it
only to an existing runtime staging directory. The base configuration may be
permissive: the builder copies the tree and installs the authoritative
`userspace/security/selinux/config` from its disposable policy build into the
candidate. That authoritative configuration must be enforcing. The copied
configuration is checked after the policy overlay and again before packaging.
The original base, production rootfs, and production image are not changed.
Symlinked SELinux staging paths are rejected rather than followed.

The bootstrap mounts only filesystems required for SELinux setup, calls the
existing policy validation/load/enforcement function, then `exec`s upstream
`/sbin/openrc-init` as the same PID 1. OpenRC runs its normal `sysinit`, `boot`,
and `default` runlevels. The old custom supervisor remains in
`userspace/init.c` for reference, as an `EXCLUDE_FROM_ALL` target, and is no
longer installed.

OpenRC is built without its optional SELinux shim because that project path
needs the target `libcrypt`/shadow toolchain components that this sysroot does
not carry. SELinux stays enforced by the existing OpenWave bootstrap. The
OpenWave runtime service uses upstream `checkpath` to create and correct
ownership/mode, then the existing OpenWave SELinux restore API validates
labels on sensitive Waydroid and persistent-state directories. This does not
rebuild policy at runtime; candidate preparation builds policy separately in
the disposable output directory.

BusyBox ash is exposed only as `/bin/sh`; no terminal or login service is
enabled. The candidate stages OpenRC's observed runtime applets, including
`chmod`, `cp`, `ifconfig`, `ip`, `kbd_mode`, `mktemp`, `readlink`, and `rmdir`,
which are enabled in the checked-in applet config. `/sbin/ip` points to
BusyBox's `ip` applet for OpenRC's loopback address and link setup, avoiding an
unstaged `libmnl` runtime dependency. OpenRC's keyboard-map, swap, and sysctl
boot services remain disabled because this graphical candidate does not
configure them; in particular, BusyBox `loadkmap` is not a compatible
replacement for the `loadkeys` command that OpenRC's keymap service expects.
The pinned BusyBox source remains unchanged; the checked-in OpenWave applet
config is `busybox.config`. Waydroid has separate
runtime-directory provisioning and manager service definitions, neither linked
into the default runlevel. Native runtime validation and native services do not
depend on Waydroid paths, labels, or startup.

The Ocean window runs through EGLFS/KMS. `OceanLauncher/Main.qml` also declares
an embedded `WaylandCompositor` using socket name `wayland-0`, with
`XDG_RUNTIME_DIR=/run/user/1000`; this source configuration alone does not prove
that the compositor creates an accessible socket or serves a client. Do not
advertise a working Wayland session until the runtime socket and a client
registry exchange have been verified.

## DRM connector name and 544x960 runtime check (2026-10-29)

QEMU's virtio GPU and the guest kernel expose the connector as `Virtual-1`.
Keep that exact name in the shared `userspace/config/kms.json`, the OpenRC
candidate config, the staged `rootfs/etc/openwave/kms.json`, and the legacy
init runtime-config template. The rootfs and shared template previously said
`Virtual1`; EGLFS could not select the named output reliably. The mismatch is
now covered by `test_drm_device_policy.py`.

A fresh image assembled from the corrected `rootfs` was booted from a writable
QA image. The first packed image exposed that the staged `/init` binary was
older than `userspace/init.c`; its generated runtime config still contained
`Virtual1`. I rebuilt only `openwave-legacy-init`, staged that binary, and
assembled candidate v2 at
`out/ocean-resolution-20261029/OpenWave-resolution-candidate-v2.img`
(preboot SHA-256
`a5712ec30eafb37ea307c59052f4da98de03f03d8e86f6a7ed7280d64f89923c`). The
packed init contains `Virtual-1`, and the packed JSON requests `544x960`.

In v2, QEMU's VNC framebuffer reported and captured **544x960**. Ocean rendered
its wallpaper and dock. A pointer click on Settings initiated the app launch;
the preceding disposable run showed the Settings surface fully rendered. This
does not reproduce the reported miniature or unresponsive display. Screenshots
and both run reports are recorded in
`out/ocean-resolution-20261029/verification.json`.

QMP delivered powerdown, but the legacy-init guest did not react to the ACPI
request; QEMU was stopped by the bounded fallback. I recovered only the
disposable image with offline `e2fsck -f -y`; the final `e2fsck -fn` passed.
That shutdown result is not a guest-clean-shutdown pass. The test boot reported
SELinux permissive, and Android startup remained disabled.

Run `python3 userspace/openrc/validate_openrc.py` for host-only script and
dependency validation. Building a candidate does not imply a guest boot; a
QEMU run still needs separate authorization.

## Ocean graphics diagnostic run (2026-10-29)

The authorized disposable Ocean diagnostic image booted with SELinux enforcing,
OpenRC at the default runlevel, and Ocean running unprivileged at 544×960. Its
actual Qt platform log was `eglfs`. The screenshot showed the status area,
drawer handles, and QA badge, but a black home area without favorite icons.
appd's D-Bus catalog returned five launchable apps; Ocean's own model and QML
delegate diagnostics were absent, so the blank-home cause remains unresolved.
The run did not verify the `wayland-0` socket or a Wayland client. The complete
test record, candidate hashes, shutdown result, and screenshot path are in
[`OceanLauncher/BUILD.md`](../../OceanLauncher/BUILD.md).

The follow-up added an opt-in `OPENWAVE_QA_SHUTDOWN=1` observer for the next
authorized guest run. It samples the inode-matched `/proc/locks` entry for
`/run/openrc/exclusive/localmount` and records the holder PID, command, wait
channel, OpenRC localmount state, and mountinfo to a root-only QA log for at
most 15 minutes. The prior log does not identify the holder, so this is
diagnostic instrumentation, not a shutdown fix. No QEMU boot was run for this
follow-up.

The QA observer's `org.openwave.Wallpaper=not-registered` line is not a valid
failure result: it queries the session bus, but `wallpaperagentd` owns that name
on the system bus. Wallpaper-bus readiness remains unverified until the probe
uses the daemon's actual bus.

## Wallpaper and shutdown diagnostic candidate (2026-10-29)

The system-bus wallpaper probe has been corrected to distinguish daemon
process, registered name, owner credentials, interface response, and valid
method data. It records `GetCurrent` and `GetImage` outcomes from the declared
`org.openwave.Wallpaper1` interface. Ocean's existing WallpaperClient and QML
`Image` now expose opt-in request and rendering state when
`OPENWAVE_QA_GRAPHICS=1`; the optional `OPENWAVE_QA_WALLPAPER_IMAGE=bundled-svg`
uses a bundled asset with the same `Image` element. See
[`OceanLauncher/BUILD.md`](../../OceanLauncher/BUILD.md) for protocol details,
fallback behavior, tests, and the exact disposable candidate hash. No guest
boot had been run at candidate preparation; the later authorized run and its
actual results are recorded below.

The earlier shutdown warning `udevd: waiting for localmount` is generated by
OpenRC while a service stop waits for an exclusive service lock. Existing guest
evidence does not name the lock holder or the action holding it, so the root
cause and dependency teardown order remain unresolved. The candidate preserves
the OpenRC service graph. A future authorized run needs the matching
`/proc/locks` entry and holder PID/cmdline/wchan, localmount state, and mountinfo
captured during the wait; no lock deletion, sleep, or forced shutdown is used
as a fix.

The one authorized wallpaper diagnostic boot later reproduced the stall: QMP
delivered powerdown, OpenRC reached `udevd: waiting for localmount`, no QMP
shutdown event arrived, and the bounded runner terminated QEMU. The image was
a writable copy; journal recovery and read-only ext4 validation passed
afterward. The `localmount` service lock holder and operation remain unknown.
The same run captured a separate wallpaper failure: Ocean's system-bus
`GetCurrent` was rejected by appd's trusted-shell-or-managed-app identity
resolver. Its exact failed identity predicate was not logged. The observer's
root `GetCurrent` succeeded, while `GetImage` payload classification hit
missing `wc`/`tr` applets. Details and artifact hashes are in
[`OceanLauncher/BUILD.md`](../../OceanLauncher/BUILD.md).

### Shutdown lock observer runtime result

During the authorized 2026-10-29 wallpaper candidate boot, QMP delivered the
powerdown request, but OpenRC again waited for `localmount` while stopping
`udevd`; the bounded runner forced QEMU after 60 seconds without a shutdown
event. The opt-in `localmount-lock-owner.log` was not created even though
`OPENWAVE_QA_SHUTDOWN=1` was present on the kernel command line. Therefore the
lock holder and blocked operation remain unknown, and the observer activation
path itself needs host/source correction before another authorized boot. The
writable guest copy was journal-recovered and passed a final read-only ext4
check; no second boot was run.

### Native launch interactive-run display and shutdown evidence — 2026-10-30

The interactive `--window` run selected QEMU's GTK display backend. The
captured connector `card0-Virtual-1` was connected and advertised both 720x400
and 544x960, with 720x400 first in its mode list. OpenWave's KMS config asked
for `Virtual-1` at 544x960, while the saved Ocean QA result reported a 720x400
window. The status snapshot does not contain the active DRM mode or a direct
QScreen/QQuickWindow measurement, so GTK's initial 720x400 EDID is the leading
host-side explanation, not a proven active-mode trace. The same run-openwave
source comment calls out GTK's initial EDID; earlier VNC runs recorded a
544x960 framebuffer. Do not change Ocean's compositor based on this evidence.
For the next display comparison, record DRM `mode`, Qt screen/window geometry,
and QEMU's selected backend in the same run.

The recovered writable guest contains a 1,303-byte
`localmount-lock-owner.log`. Its stop-hook snapshot says
`localmount_lock=absent` while `localmount_state=started`; there is no recorded
`/proc/locks` entry or holder PID. This explains why the observer did not
identify the holder: its sampling loop first stats the pathname
`/run/openrc/exclusive/localmount` and only then uses that inode to search
`/proc/locks`. OpenRC 0.64.1 uses `flock()` on the exclusive file descriptor,
and its service-state path unlinks the name before closing the descriptor.
When that name is absent, the observer has no inode key for a still-open lock;
its five-second polling can also miss shorter intervals. This is a confirmed
limitation of the current tracing method, not a confirmed explanation of the
50-second `udevd` wait. No OpenRC source or service graph was changed. A future
diagnostic should enumerate `/proc/locks` directly and retain device/inode
records even after unlink, while capturing the holder's command and wait
channel during the actual wait.
