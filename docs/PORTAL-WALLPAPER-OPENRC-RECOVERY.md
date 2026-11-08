# Native portals and Settings Wallpaper under OpenRC

For the newer power controls, external Wi-Fi candidate and launch command, see
[the power and USB Wi-Fi report](POWER-USB-WIFI-RECOVERY.md).

This follow-up exercises Settings > Wallpaper using the real Settings QML,
OpenWavePortals client, supervised portal broker, permission leases, Ocean
picker, descriptor grants, cropper and wallpaper agent. SELinux stays enforcing.
The existing visual design and animation definitions are preserved.

## Diagnosed implementation and integration defects

- Settings uses `image/*`, but the local broker rejected `*` and only matched
  literal MIME strings. Valid group wildcards now work; malformed wildcards
  and invalid option types remain rejected.
- The exported SDK and Ocean UI already expose Photos picker operations, but
  local client/broker sources lacked them. Source headers and the D-Bus XML now
  match that contract. The broker enumerates only the verified session user's
  Pictures and imported Photos library, with bounded scanning, no symlink
  traversal, request-bound opaque tokens, read-only descriptors, live leases,
  and inode/device checks before previews and selection.
- The file broker tried to chown/chmod the user's profile for every request.
  Provisioning belongs to OpenRC/appd; the broker now verifies existing protected
  profile directories without mutating them.
- Wallpaper writes rejected every non-root caller, including the managed
  Settings app in the OpenRC session. Writes now require an appd-resolved,
  trusted `com.openwave.settings` identity; root retains its existing behavior.
  Persisted images stay broker-owned under the authenticated session UID's
  directory. Other applications cannot gain write permission by naming Settings.
- Guest evidence from run 17 denied Ocean metadata access to user images.
  Its embedded Files picker receives only read/list/map access to user data;
  no profile mutation was granted.
- Gallery listing used `ValidateLease`, which consumes the one-use grant.
  The first preview and final delivery therefore failed after a successful
  listing. The broker now uses authenticated `CheckLease` for enumeration and
  previews; final `TakeResult` retains consuming validation. Cancellation,
  expiry, identity changes and revocation retain their checks.

## Regression and guest validation

`python3 userspace/openwave-portal/tests/run_contract.py` builds the actual broker
against the target SDK with a temporary test-only entrypoint. A private D-Bus
session exercises open/multiple/save/directory operations, cancellation, MIME
wildcards, gallery list/preview/selection, readonly grants, invalid tokens,
request-bound tokens, symlink escape rejection, inode replacement rejection and
backend/caller disconnect handling. Production builds reject test-root arguments.

`python3 userspace/wallpaperagentd/tests/test_authorization.py` executes the
production authorization predicate with null/missing identities, untrusted
Settings, other trusted apps and verified trusted Settings. Policy and image
pipeline regressions are recorded under `out/boot-recovery/portal-*`.

The guest runner's opt-in `--portal` argument adds `openwave.qa=portal`. The
Settings driver invokes the existing tab, wallpaper-source and crop-apply
handlers. Ocean waits for the actual picker to open; it tests cancel, verifies
that the Files model lists the fixture, and decodes the Photos preview before
selecting it. The test validates grant bytes, both crop/save paths and wallpaper
refresh to 768x1366. It does not skip identity, lease, DAC or SELinux checks.

The fixture is generated with:

```sh
python3 userspace/openrc/qa/create-portal-fixture.py \
  'rootfs/Data/Users/ocean/Pictures/OpenWave portal test.png'
python3 userspace/openrc/qa/create-portal-fixture.py \
  'rootfs/usr/share/openwave/qa/portal.png'
```

Runs 16 and 17 were intermediate candidates. Run 17 exposed a test-driver Qt
method-arity error; it is a failing run, not evidence of successful portals.
Prior images and user profiles are preserved. Acceptance is recorded separately
from these intermediate runs below.

Run 18 reached the real Settings request and exposed the file broker's missing
user-data metadata permission. Scoped read and descriptor-relay rights were
added; D-Bus receives only `read` on the granted file types, never `open` or write
permissions. A compiled-policy regression enforces those limits and the picker's
read-only profile access.

Run 19 exposed an OpenRC startup race: appd could own its bus name before its
ready file existed. `start_post` now waits for its existing `ListApplications`
RPC to return before validating the marker and checks that the marker's PID
matches the supervised process. A shell fixture executes the real `start_post`
with name ownership preceding readiness and rejects wrong-PID/RPC-failure cases.

Run 20 got past profile validation but permissiond could not publish its own
QSaveFile/O_TMPFILE policy record: the AVC denied `link` on its dedicated
`openwave_permission_state_t`. Only permissiond's own-state link permission was
added. Its private records remain inaccessible to ordinary apps.

The imported-photo fixture is created by the actual confined Photos application
when the opt-in portal QA flag is present. Its appd-allocated UID, namespace,
Landlock and data ownership remain in force. No private app profile is seeded
or recursively chowned in the staged image for this test.

Run 21 completed Files crop/save but exposed the consumed gallery lease. Run 22
completed the real Settings workflow, including both Files and imported Photos,
previews, descriptor reads, cancellation, cropping, saving and 768x1366 refresh.
Its outer smoke status remained a failure because the QA script tried to stat
the wallpaper agent's protected marker. The resulting AVC correctly denied the
QA script; no private-state permission was added. The script now uses the
managed Settings client's verified GetCurrent/GetImage result and checks both
client and Ocean logs for failures.

## Accepted Wallpaper candidate

`out/boot-recovery/ocean-portals-23.img` is a fresh x86_64 ext4 image assembled
from runtime `rootfs` through the normal `build-openwave image` pipeline. No
existing image was overwritten. Target compilation used the SDK sysroot, and
the rebuilt SDK FileChooser, portal broker, permissiond, wallpaper agent,
Settings, Ocean and Photos artifacts were staged before packing.

Image SHA-256:
`65b4ac3b37f4d8bf762ffca0899c19ce5f663b24cd1010d16517aa94cfcfbe80`

Kernel SHA-256:
`b1fdf19b9ebd72cf5cd622f45c07826a37776e09f01e5807f02559f1f970156f`

```sh
GDK_BACKEND=x11 python3 scripts/test-openwave-boot.py \
  --image "$PWD/out/boot-recovery/ocean-portals-23.img" \
  --output "$PWD/out/boot-recovery/ocean-portals-23-boot" \
  --seconds 80 --portal
```

The GTK/VirGL 544x960 guest reported both the client completion marker and
`PORTAL_SMOKE PASS Settings wallpaper end-to-end`. Its result records
`portal_passed: true`, SELinux enforcing, unchanged image/kernel hashes, no
kernel panic, actual guest powerdown and QEMU exit after ACPI. Read-only ext4
checking passed. The screenshot `ocean-portals-23-boot/wallpaper-verified.png`
shows Ocean displaying the selected wallpaper after Settings exits. These are
automated calls to the real QML handlers, not a manual pointer-interaction test.

Evidence is in `out/boot-recovery/ocean-portals-23-boot/{serial.log,result.json,fsck.log}`.
Runtime `/run` logs are tmpfs-backed; the QA script copied their relevant lines
to the captured serial log before shutdown. No image mount was left behind.

The staged XDG Settings portal also passes its existing isolated contract suite
(`portal-xdg-settings-contract-23.log`): public/backend variants, filtering,
privacy, read-only behavior, live updates, malformed settings, restart and bus
name ownership. That service implements the read-only Settings interface;
file/photo selection uses OpenWave's native portal. This does not establish
support for other XDG portal interfaces.

Host regressions passed: native portal IPC, wallpaper writer authorization,
SELinux policy (including restrictions on D-Bus path opens and Ocean profile
mutation), image packing, service readiness, supervised process identity and
the appd name-before-ready race. The full permission runtime/revocation suite
and the legacy wallpaper multi-user test were not run in this follow-up.

The same image also completed the separate `--seconds 110 --app-launch --drawer`
guest suite. Settings, Files, Water Rouge Gallery, Photos and Browser each
presented a live compositor surface and remained running for ten seconds.
Termination, relaunch after appd and Ocean OpenRC restarts, and deliberate
Settings crash/recovery passed. Ordinary apps retained private UIDs, namespaces,
seccomp, cgroup limits, no-new-privileges and zero effective capabilities.
Evidence: `out/boot-recovery/ocean-portals-23-apps/serial.log`.
This checks launch stability; it does not test Browser networking or page loads.
Its `result.json` records `app_launch_passed: true`, enforcing mode, unchanged
image/kernel hashes and completed ACPI powerdown/QEMU exit. Its read-only
filesystem check also passed. No QEMU process or image mount remained afterward.

## Source locations

| Area | Files changed for this follow-up |
| --- | --- |
| File/photo client | `userspace/portal-client/OpenWavePortals.h`, `OpenWavePortals.cpp` |
| Broker | `userspace/openwave-portal/main.c`, `README.md`, `tests/portal_contract.py`, `tests/run_contract.py` |
| D-Bus contracts | `userspace/dbus/interfaces/org.openwave.portal.FileChooser.xml`, `org.openwave.Permissions1.xml` |
| Leases | `userspace/permissiond/PermissionService.cpp`, `README.md`, `tests/runtime.cpp` |
| Wallpaper writer | `userspace/wallpaperagentd/main.c`, `README.md`, `tests/test_authorization.py` |
| Confinement | `userspace/security/selinux/openwave.cil`, `tests/test_policy.py` |
| OpenRC readiness | `userspace/openrc/etc/init.d/appd`, `tests/test_appd_readiness.py` |
| Guest drivers and fixture | `userspace/openrc/qa/PortalSettingsHarness.h`, `PortalShellHarness.h`, `portal.start`, `create-portal-fixture.py` |
| Driver entrypoints | `OceanLauncher/OceanSettings/main.cpp`, `OceanLauncher/OceanLauncher/main.cpp`, `OceanLauncher/OceanPhotos/main.cpp` |
| Image/test integration | `scripts/prepare-openwave-boot.py`, `scripts/test-openwave-boot.py` |

Several files already contained prior appd/OpenRC or user UI changes. This report
describes the portal follow-up; it does not claim the entire working-tree diff.

To use this candidate normally (without the opt-in QA drivers):

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-portals-23.img \
  --usb-wifi none --usb-bluetooth none --audio-backend none
```
