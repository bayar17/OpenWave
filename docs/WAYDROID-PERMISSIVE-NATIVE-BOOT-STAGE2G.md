# Stage 2G — native OpenWave boot during permissive Waydroid debugging

## Evidence from the uploaded Stage 2F archive

The user-provided `waydroid-mount-debug-permissive-2f.zip` contains
`result.json`, `mount-preflight.json`, an AVC summary and the raw serial log.

Observed:
- `selinux_debug_capture_complete=true` and
  `inputs_unchanged=true`; QEMU ran using `-snapshot`.
- SELinux was permissive; the QA report correctly did not pass the
  enforcing-only release gate.
- 61 AVC records, all `permissive=1`. 58 are from
  `openwave_system_t`, 3 from `openwave_udev_t`.
- The installed helper **exists**, is root-owned, is ELF64 x86-64,
  and is labeled `openwave_waydroid_mount_exec_t`.
- `/dev/loop-control` **exists** in permissive mode. An
  `openwave_system_t -> openwave_loop_control_t:chr_file getattr`
  denial explains why the prior enforcing QA observer could not see it.
- EROFS system and ext4 vendor images passed their magic checks.
- Overlay upper/work paths are absent **before** Waydroid starts; this
  alone does not establish a provisioning regression.
- `openwave-runtime` failed, blocking D-Bus/appd/Ocean and many
  dependent services. Its C executable explicitly refuses permissive mode.
- The `--help` command from the QA observer generated SELinux
  `execute`, `execute_no_trans`, `read`, `open` and `map`
  denials on the helper executable. This does **not** show that the
  manager-to-helper transition would be denied.
- No Android framework boot, image mounts, manager privilege transfer
  or successful helper transition has been verified.

## Intent

Permit the **existing native OpenWave services** to advance during an
explicit *disposable-QEMU permissive QA boot*, while retaining strict
SELinux enforcing as the default for all ordinary and production boots.

The Stage 2G exception applies **only** to
`/System/Services/openwave-runtime-check`, not the SELinux policy,
kernel, upstream Waydroid manager, or dedicated C mount helper.

The QA selection code scans exact whitespace-delimited tokens in
`/proc/cmdline`; all four are required:

```
openwave.selinux=permissive
openwave.audit=verbose
openwave.qa=mount-preflight
openwave.qa=selinux-debug
```

The runner emits the last token **only** when explicitly invoked with
`--mount-preflight --mode permissive --selinux-debug`.

The executable still requires SELinux to be active and still checks
directory ownership, modes, and exact SELinux labels. In diagnostics it
prints a marked `QA_ONLY` result, **never the production verification
success message**.

The dedicated Waydroid mount helper still **refuses privileged mounts
in permissive mode**. No new `CAP_SYS_ADMIN` or broad
`openwave_default_t` rules are introduced. This is not a release gate.

## Run

Check the stacked branch and retain any local `llvm-project` changes.
Rebuild a **fresh image** because `openwave-runtime-check` is compiled
into the image; reusing the Stage 2E image cannot activate the new
guard.

```bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/permissive-native-boot-stage2g-20261108 ||
  git switch --track origin/waydroid/permissive-native-boot-stage2g-20261108
python3 -m unittest discover -s scripts/tests -p 'test_selinux_debug.py' -v
./build-openwave selinux
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-permissive-native-2g.img

python3 scripts/test-openwave-boot.py \
  --mount-preflight --mode permissive --selinux-debug \
  --seconds 120 \
  --image out/boot-recovery/ocean-waydroid-permissive-native-2g.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-permissive-native-2g-evidence
```

The kernel path is the previously verified Waydroid diagnostic kernel;
use it only if present and unchanged. Every run requires a new
`--output` directory. The boot runner uses QEMU `-snapshot` and
compares input image/kernel SHA-256 checksums.

Review:

```bash
cat out/boot-recovery/waydroid-permissive-native-2g-evidence/result.json
cat out/boot-recovery/waydroid-permissive-native-2g-evidence/selinux-avc-debug.json
grep -E 'openwave runtime check:|openwave-runtime|OPENWAVE_BOOT_REPORT|MOUNT_QA_' \
  out/boot-recovery/waydroid-permissive-native-2g-evidence/serial.log
```

Expected: `openwave-runtime` should no longer fail **merely because
SELinux is permissive**. If it still fails, the new logs identify a
concrete ownership/mode/context or another dependency. Successful
native startup under permissive is diagnostic evidence only.

## Subsequent work

Inspect *enforcing* AVCs with read-only metadata access for the QA
observer, rather than granting it execute or execute_no_trans permission
on the Waydroid mount helper. Validate a narrow solution to the
`openwave_system_t -> openwave_loop_control_t:chr_file getattr`
observer denial. Keep bootstrap relabel AVCs separate from these
Waydroid tests. No automatic `audit2allow` suggestions.

Later, test real mount operations in a disposable enforcing QEMU guest
with carefully scoped mountpoint and image types. Do not remove the
manager's diagnostic mount privileges until the helper has been wired
and verified. Do not enable permissive mode by default.
