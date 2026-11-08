# Stage 2L — LXC ELF loader directory traversal under SELinux enforcing

## Failure after Stage 2K

Enforcing QEMU first-boot remains blocked: the Waydroid manager starts,
D-Bus Start(session) exits 1, LXC is STOPPED, and Android framework is
not yet running. The log now contains a more precise diagnosis:

```text
lxc-start: error while loading shared libraries: libunwind.so.1:
cannot open shared object file: Permission denied
```

The AVCs have moved from the Stage 2K host-root type
`openwave_default_t:dir` to the path components
`/etc`, `/lib64` and `/usr` labeled
`openwave_runtime_t:dir`. They deny `search` (plus a `getattr`
on `/lib64`). Dynamic loading cannot resolve library paths
without walking these directories.

## Minimal policy delta

```cil
(allow openwave_lxc_t openwave_runtime_t (dir (search getattr)))
```

The policy already allows this domain `openwave_runtime_t:file`
`open read getattr map`, and a dynamic-link client attribute grants
read-only linking of trusted shared objects. Stage 2L adds **only**
directory search and metadata queries to existing runtime path labels.

It does **not** enable directory enumeration, open, modification,
file execution, DAC overrides, LXC mount privileges or
`CAP_SYS_ADMIN`. This is per SELinux type, not path-specific: other
directories labeled `openwave_runtime_t` acquire the same
search/getattr rights. Tests assert that `read`, `open`, `write`,
`mounton` and mutations of such directories remain disallowed.

The Waydroid report also exposes `stages.lxc_loader=FAIL` and
`lxc_failed_library=libunwind.so.1` when that exact failed loader
message appears, but does not claim a loader PASS merely because
a message is absent.

## Remaining independent blockers

- The upstream OverlayFS mount emitted `statfs failed` for
  `/var/lib/waydroid/overlay_rw/system` even though this directory
  existed by the post-failure inventory. This needs a separate
  error/permissions analysis after basic loader readiness.
- Waydroid manager had specific device enumeration, default-type
  `build.prop` reading and DAC capability AVCs. Do not grant
  `dac_override`, `dac_read_search`, broad `openwave_default_t`
  file access or mount authority as a workaround.
- Android vendor-file labels include a not-valid-on-host-policy
  warning. Do not silently map Android type names into the host
  OpenWave policy without a deliberate security design.
- The native appd and WaterRouge SDK work is separate.

## Build and rerun using a new disposable snapshot

```sh
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-runtime-loader-stage2l-20261108 ||
    git switch --track origin/waydroid/lxc-runtime-loader-stage2l-20261108

python3 -m unittest discover -s scripts/tests -p 'test_waydroid_report.py' -v

./build-openwave selinux

# Requires locally built compile-policy:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_runtime_loader_can_traverse_without_browsing_or_mutating -v

./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-loader-stage2l.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-loader-stage2l.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-loader-stage2l-evidence

E=out/boot-recovery/waydroid-loader-stage2l-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'WAYDROID_QA|LXC_LOG|libunwind|loading shared libraries|avc:.*denied|overlayfs:' \
  "$E/serial.log" | tail -n 180
```

Success for Stage 2L means `lxc-start` gets past this early loader
failure; a new failure may appear later. **Do not claim Android boot**
until LXC RUNNING, ServiceManager and Zygote running, System Server
present, `sys.boot_completed=1` and Package Manager responding.
Display, Android back button and app shortcuts need later Ocean
integration tests.

The original input disk and kernel hashes must remain unchanged.
Use a new image and output path; QEMU snapshot testing must not
mutate the user's last successful native image.
