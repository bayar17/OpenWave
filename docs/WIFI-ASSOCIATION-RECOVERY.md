# USB Wi-Fi association and DHCP recovery

For the current keyboard-enabled image and kernel, use the command in
[KEYBOARD-INPUT-RECOVERY.md](KEYBOARD-INPUT-RECOVERY.md). The association
evidence below records candidate 35.

The manual image-32 attempt reached NetworkManager but stayed on Connecting.
The earlier scan and Settings-dispatch checks did not establish association,
DHCP or working DNS.

## Confirmed failure

The physical Samsung `04e8:2018` adapter authenticated using the guest's saved
profile and received a DHCP offer. In the image-34 diagnostic, NetworkManager
then logged `accept failed: failed to accept lease: Permission denied` at the
same time as SELinux denied `net_bind_service`. NetworkManager removed the
assigned IPv4 address/default route and remained in IP configuration. This was
not a bad-password diagnosis; the saved credentials authenticated successfully.

`wifi-association-34-diagnostic/serial.log` also captured denied IPv6 raw-socket
creation, cleanup of NetworkManager's own resolver helper, and iwd's interface
ARP/ND carrier-eviction settings. No credential values were printed.

## Runtime correction

`userspace/security/selinux/openwave.cil` now permits only NetworkManager to
bind its reserved DHCP client port, create the IPv6 router-discovery socket,
and terminate its own helper processes. A dedicated proc type covers interface
IPv4/IPv6 configuration so iwd can preserve ARP/ND state during roaming. Apps
retain read-only access to these settings and gain no capabilities; mutation
tests reject app grants for the reserved-port capability and sysctl writes.
SELinux stays enforcing. No sys_admin or dac_override grant was added to
NetworkManager.

`userspace/openrc/etc/init.d/NetworkManager` now uses `--no-daemon --debug`.
In NetworkManager 1.50, this directs normal logs to stderr; OpenRC captures them
in `/run/openwave/networkmanager.stderr.log`. Previously they went to a missing
syslog service despite the configured OpenRC log files. These files are tmpfs
and disappear at shutdown. The opt-in test captures them in the host serial log.
The service validator was updated for the foreground arguments.

## Association acceptance test

`userspace/openrc/qa/wifi-association.start` runs only with
`openwave.qa=wifi-associate`. It scans the real adapter, activates an existing
saved Wi-Fi profile, observes native OpenWave connectivity and NetworkManager's
state/reason, and requires a DHCP4 configuration. It then launches Photos through
appd for a real `example.com` DNS lookup from an ordinary confined app. The Photos
probe is also explicitly QA-gated. It uses the deployed DNS namespace import and
normal application identity, SELinux, Landlock and seccomp constraints.

The runner option is `--usb-wifi 04e8:2018 --wifi-associate`. QEMU has no virtual
network interface in this suite. A root scan, accepted activation request, or
QMP shutdown acknowledgement alone cannot satisfy the acceptance marker.

Affected test/deployment files are `scripts/test-openwave-boot.py`,
`scripts/prepare-openwave-boot.py`, `userspace/openrc/qa/wifi-association.start`,
`OceanLauncher/OceanPhotos/{main.cpp,CMakeLists.txt}`,
`userspace/openrc/validate_openrc.py` and
`userspace/security/selinux/tests/test_policy.py`.

## Image preparation

No QEMU was running when the manual attempt was inspected. The user image's
read-only filesystem check found an unrecovered journal. A separate copy was
recovered and checked; image 32 was left untouched. Candidate 35 preserves that
guest's saved network state. Its installed runtime updates come from `rootfs`;
the rebuilt policy, contexts, manifest, Photos binary and service/QA scripts
were copied from staging and verified byte-for-byte after labeling. Their
checksums are in `out/boot-recovery/wifi-association-runtime-35.json`.

The target builds succeeded before installation. Compiled-policy mutations,
service readiness and OpenRC integration checks passed. The candidate's guest results and launch command follow below.

## Accepted connection in candidate 35

`out/boot-recovery/wifi-association-35-acceptance/serial.log` records native
`wifiState=Connected`, IPv4 DHCP address `192.168.1.146`, gateway
`192.168.1.1`, DNS servers and an IPv6 address. It also records
`WIFI_ASSOC_APP_DNS_PASS` from the confined Photos process, followed by
`WIFI_ASSOC PASS associated, DHCP and DNS`. No virtual NIC was present.
This establishes actual association and DNS operation; it does not establish
Browser page rendering or every network's roaming behavior.

Use the candidate with the preserved wireless kernel:

```sh
cd /mnt/aosp/ThunderOS
GDK_BACKEND=x11 ./run-openwave run --window \
  --image out/boot-recovery/ocean-wifi-connected-35.img \
  --kernel out/boot-recovery/bzImage-power-wifi-28 \
  --usb-wifi 04e8:2018 --usb-bluetooth none --audio-backend none
```

The image retains the saved guest Wi-Fi profile. Normal boots do not execute
the QA activation or DNS probes. The kernel SHA-256 is
`53583b5eb96621eeb7017891fe323da8dc109c12d2acdf1c960b917dead26d30`.

Final `result.json`: saved-profile association requested, `usb_wifi_passed=true`,
SELinux enforcing, no kernel panic, complete boot report, guest Power down and
QEMU exit code 0 after orderly ACPI/OpenRC shutdown. The base image and kernel
hashes remained unchanged. A read-only filesystem check passed. No test VM or
image mount remained afterward, and the USB adapter returned to the host.

Image SHA-256 before normal manual use:

`641a77016081ea99aa74c50352f35af31380c030c4ae6c40fc35b6a08c63c460`

Reproduce using a fresh evidence directory:

```sh
GDK_BACKEND=x11 python3 scripts/test-openwave-boot.py \
  --image "$PWD/out/boot-recovery/ocean-wifi-connected-35.img" \
  --kernel "$PWD/out/boot-recovery/bzImage-power-wifi-28" \
  --output "$PWD/out/boot-recovery/my-wifi-association-check" \
  --seconds 150 --usb-wifi 04e8:2018 --wifi-associate
```
