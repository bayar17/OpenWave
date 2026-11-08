# Ocean power controls and Samsung USB Wi-Fi

For the current keyboard-enabled image and kernel, see
[keyboard recovery](KEYBOARD-INPUT-RECOVERY.md). The results below describe
the earlier power and USB Wi-Fi candidates.

## Use the candidate

For the later Connecting/DHCP repair and actual saved-profile association
acceptance, see [Wi-Fi association recovery](WIFI-ASSOCIATION-RECOVERY.md).
The command below selects that image, which retains the saved guest profile.

The runtime staging tree is `rootfs`; the candidate is a separately packed
x86_64 ext4 image. `OpenWave.img` and the older portal candidate are preserved.

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-wifi-connected-35.img \
  --kernel out/boot-recovery/bzImage-power-wifi-28 \
  --usb-wifi 04e8:2018 --usb-bluetooth none --audio-backend none
```

Open Quick Menu and choose **Restart** or **Shut down**, then confirm. Cancel
leaves the system running. Open **Settings > Wi-Fi** to choose a network.
The Samsung WIS09ABGN/LinkStick adapter is USB `04e8:2018`; its guest driver is
`rt2800usb` (RT2872/RF0002). The later saved-profile test verified association, DHCP and a real DNS lookup
from a confined app; see the linked association report. It reused the saved
guest profile without requesting or printing a password.

The user granted temporary access to `/dev/bus/usb/001/003`. USB bus/device
numbers and that ACL can change after reconnection; check `lsusb` if QEMU
reports access denied. No persistent host udev rule was installed. During the
hardware test the host default route remained on its separate `wlp2s0` adapter.

## Power implementation

`OceanLauncher/OceanLauncher/PowerController.*` connects Quick Menu controls to
the existing confirmation prompt. `userspace/powerd` owns `org.openwave.Power`
and authenticates Ocean's supervised UID, PID and process start time against
OpenRC's protected identity record. Ocean also authenticates the broker.
Arbitrary root callers and unsupervised session processes are rejected.

The broker writes only fixed `reboot`/`poweroff` words to the verified,
root-owned FIFO `/run/openrc/init.ctl`. OpenRC PID 1 runs the normal shutdown or
reboot sequence. The broker executes no shell command and has no direct reboot
capability. SELinux assigns the FIFO a dedicated type and prevents direct
app/compositor access. The old signal handler in `userspace/init.c` is not used
by the current bootstrap/OpenRC PID 1 path.

The footer leaves room for Quick Menu's draggable chevron. The opt-in power
QA driver sends Qt mouse press/release events through the window's actual hit
testing, including the lower inner edge of both power buttons. It cancels once,
then confirms. The reboot suite requires different boot IDs and two complete
boot reports before confirming poweroff on the second boot. A timeout's ACPI
fallback cannot satisfy the UI power test. QA drivers run only with explicit
`openwave.qa` kernel flags.

## Wireless and DNS implementation

`build-openwave kernel` enables NETDEVICES, CFG80211, MAC80211, RFKILL, built-in
rt2800usb, its USB dependencies, and the cryptographic algorithms required by
the staged iwd. It verifies the resulting configuration before publishing the
kernel. Existing target `rt2870.bin` firmware and regulatory data stay in
`rootfs/lib/firmware`; no host libraries were imported. The immutable tested
kernel is `out/boot-recovery/bzImage-power-wifi-28`, SHA-256:

`53583b5eb96621eeb7017891fe323da8dc109c12d2acdf1c960b917dead26d30`

NetworkManager and iwd have separate protected state directories and explicit
SELinux access for their sockets, rfkill and firmware loading. The firmware
load permission is limited to the firmware type. SELinux remains enforcing;
the broker and ordinary applications gain no network administration capability.

`userspace/config/NetworkManager.conf` uses iwd, internal DHCP, profiles under
`/var/lib/NetworkManager/system-connections`, and a runtime DNS cache.
`/etc/resolv.conf` links to `/run/NetworkManager/resolv.conf`. Apps import the
runtime directory read-only, so atomic resolver replacements remain visible.
Saved profile directories are absent from app namespaces; SELinux also denies
apps access to NetworkManager's private state files. Only the daemon writes the
public resolver file.

The opt-in DNS suite has no NIC. It asks the real NetworkManager D-Bus API to
publish the documentation-only resolver `192.0.2.53` in its disposable snapshot,
then checks from confined Photos that DNS is readable, not writable, and that
saved profiles are inaccessible. This fixture performs no network connection
and does not alter the base image or normal-boot DNS configuration.

Cold boots also exposed service startup races. D-Bus/iwd now wait for their bus
names, the shared bounded readiness helper tolerates a delayed owner, and
permissiond waits for its own OpenRC identity proof before binding appd.
Neither authentication nor enforcement is relaxed.

## Verification

Guest test artifacts are under `out/boot-recovery/`; each directory contains
`command.json`, `serial.log`, `result.json` and a read-only filesystem check.
Tests use disposable overlays and verify that base image/kernel hashes remain
unchanged. Normal service logs, including powerd/iwd/NetworkManager output,
are `/run/openwave/*.stdout.log` and `*.stderr.log` in tmpfs and disappear at
shutdown; the test captures their relevant output in the host serial log.

Host checks passed for the real FIFO validation, supervised identities,
service readiness including a late owner, image pipeline, Wi-Fi QA with the
actual limited guest utilities, file contexts and compiled-policy rejection
of forbidden power/FIFO/profile grants. OpenRC validates 39 enabled services
and an acyclic dependency graph.

Before the manual connection report, image 31 passed the four suites below.
These results cover the scope of that earlier runner; its Wi-Fi suite did not
exercise Settings controls.

The preceding image was `out/boot-recovery/ocean-power-wifi-31.img`. Its SHA-256
before normal manual use was:

`919472753825aaadb0d02e794c4edbcfe33a1f250402830c784802c1e12845ed`

| Guest evidence directory | Result |
| --- | --- |
| `ocean-power-wifi-31-reboot` | Actual mouse hit testing, Cancel, confirmed restart, different second boot ID, confirmed shutdown; `power_passed: true`. |
| `ocean-power-wifi-31-apps` | All five apps hold for ten seconds with live surfaces and confinement; appd/Ocean restarts, deliberate crash recovery and DNS/profile isolation pass. |
| `ocean-power-wifi-31-wifi` | Physical Samsung adapter, firmware, enabled radio, NetworkManager scan with one AP, and native OpenWave adapter state pass. |
| `ocean-power-wifi-31-portal` | Settings Wallpaper cancellation, Files and Photos selection, descriptor access, crop, save and refresh pass. |

All four image-31 runs exited cleanly, reported guest Power down, passed
`e2fsck -fn`, and preserved their input hashes. The power suite exited before
its deadline through the confirmed UI action; the other suites used orderly
ACPI/OpenRC shutdown. The reboot boot IDs were
`938aa212-fb5c-4c82-bd6e-d2066316dada` and
`c273750b-0875-4dc6-a8d5-e22b16c8b887`. No test VM or image mount remained
afterward. Rootfs contains the installed runtime changes; `OpenWave.img` was
not repacked or overwritten.
All suites run with SELinux enforcing. This is not a claim of zero SELinux
denials: unrelated existing Bluetooth/audio/optional-operation denials remain.
Bluetooth hardware, network association, DHCP leases, real DNS queries and
Internet/browser page loading were not tested by these no-connection runs.
The earlier app lifetime repair remains included; Browser's process/surface
hold passed here.

Reproduce each suite with the shared image/kernel arguments above and a fresh
output directory:

```sh
GDK_BACKEND=x11 python3 scripts/test-openwave-boot.py \
  --image "$PWD/out/boot-recovery/ocean-wifi-controls-32.img" \
  --kernel "$PWD/out/boot-recovery/bzImage-power-wifi-28" \
  --output "$PWD/out/boot-recovery/my-power-check" --seconds 120 --power reboot
```

For the other suites, replace the output directory and final options with
`--seconds 150 --app-launch --drawer --dns`, `--seconds 90 --portal`, or
`--seconds 90 --usb-wifi 04e8:2018`. Only the last one detaches the physical
adapter from the host, and the adapter must not be carrying the host connection.

## Manual connection correction

The subsequent Settings screenshot exposed a gap in image 31's acceptance:
`ConnectWifi` was rejected by D-Bus for Settings UID 1000. The packed transport
policy allowed non-root network listing but no controls. The earlier root scan
and native read-only state checks did not exercise a Settings connection call.
The image-31 scan result remains valid; it never established connection support.
No QEMU instance remained running when this report was investigated.

The correction permits the Ocean session UID to send the interface to the
broker, then authenticates every non-public method inside connectivityd. Root
maintenance remains supported. Managed, trusted `com.openwave.settings` is
resolved through authenticated appd; only the supervised Ocean process may use
its Quick Menu Wi-Fi/scan/Bluetooth toggle controls. Unregistered processes with
the same UID and other applications cannot use these controls. Connectivityd
now has an OpenRC protected identity proof so appd can authenticate its identity
queries. SELinux adds only the existing read-only broker identity-consumer role.

Affected sources are `userspace/connectivityd/{dbus.c,CMakeLists.txt,
org.openwave.Connectivity.conf}`, `userspace/appd/dbus.c`,
`userspace/openrc/etc/init.d/connectivityd`, the service identity registrar and
validator, and `userspace/security/selinux/openwave.cil`.

The extended Wi-Fi suite launches the real managed Settings process and uses
its ConnectivityBackend to enable Wi-Fi, scan, and call ConnectWifi for a
verified-absent SSID. The expected broker NotFound response proves the request
reaches the authenticated handler without activating a network. A separate
unregistered process with the same login UID must receive broker Unauthorized.
The suite can no longer pass on root scanning alone. Drivers are in
`userspace/openrc/qa/{WifiSettingsHarness.h,wifi-settings.start}`, with the
Settings main entrypoint and image/test integration updated accordingly.

Rebuilding connectivityd found missing libnm development exports in the SDK.
`out/boot-recovery/restore-libnm-sdk.py` restores the upstream 1.50.0 public
headers and generated enum/version headers, plus the matching existing target
library from the hash-verified image 30. The compiler uses only the target SDK;
no host headers/libraries or rootfs include/link paths are used. The existing
libnm runtime bytes are preserved. Provenance is recorded in
`out/boot-recovery/libnm-sdk-recovery-32.json`. Image 31 had changed during normal
use; it was left untouched, and its changed hash was not accepted as an immutable
SDK recovery source.

The corrected candidate is `out/boot-recovery/ocean-wifi-controls-32.img`. Its
SHA-256 before normal manual use is:

`bd8e9ae90903f1f17963edd74f5ff0c1a664f1da4daaaa62ad87c508181f87cd`

The extended physical Wi-Fi suite passed in enforcing mode. Evidence:
`out/boot-recovery/ocean-wifi-controls-32-wifi/{command.json,serial.log,result.json,fsck.log}`.
It includes the managed Settings ConnectWifi probe and rejection of an
unregistered same-UID caller, followed by actual guest Power down and QEMU exit.
The image and preserved kernel hashes remained unchanged. Network association,
credentials, DHCP and Internet access remain outside this no-connection test.

Other files changed for this correction are
`userspace/openrc/register-service-identity.c`,
`userspace/openrc/validate_openrc.py`,
`userspace/openrc/tests/test_supervised_identity.py`,
`OceanLauncher/OceanSettings/main.cpp`, `scripts/prepare-openwave-boot.py` and
`scripts/test-openwave-boot.py`. Target builds succeeded before installation
and packing. The six supervised-identity and six compiled-policy checks passed,
as did the limited-guest-utilities scan checker and OpenRC validation. The
existing scan-only checker remains a separate host fixture; the real guest
acceptance now additionally requires the managed Settings and same-UID rejection
markers.

The corrected image also passed `ocean-wifi-controls-32-apps`: all five
ten-second holds, appd/Ocean restarts, Settings crash recovery and confined
DNS/profile isolation. Its `result.json` records clean guest shutdown and
unchanged input hashes. No test QEMU or image mount remained after both runs;
the physical USB adapter returned to the host and its main route stayed on
`wlp2s0`. `rootfs` has the installed repair; image 31 and `OpenWave.img` were
not overwritten.
