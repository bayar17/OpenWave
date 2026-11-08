# Stage 2E — Read-only enforcing QEMU mount-helper preflight

## What this does / does not prove

The 18 Stage 2D offline helper tests and target cross-build have passed.
The new QEMU probe is an **independent guest readiness measurement**.
It does not start Waydroid, allocate a loop device, mount an image, unmount
a filesystem, grant SELinux authority, run Android or modify a host disk.

The existing active Waydroid manager still has the temporary `CAP_SYS_ADMIN`
grant. The Stage 2A–D helper retains the explicit `neverallow` against
`CAP_SYS_ADMIN`; its actual domain transition and privileged filesystem
operations remain **UNVERIFIED**. This probe cannot certify release readiness.

The probe runs only if `openwave.qa=mount-preflight` is on the kernel
command line. It is installed as
`/etc/local.d/zzz-openwave-mount-preflight.start` by
`scripts/prepare-openwave-boot.py` when packaging a new image.

It records to the guest serial console:

- `enforcing`: SELinux `/sys/fs/selinux/enforce` must be exactly `1`.
- `helper_inode`: the staged helper must be root-owned executable,
  non-world/group writable and a regular file.
- `helper_label`: exact `openwave_waydroid_mount_exec_t` xattr.
- `helper_elf`: the correct ELF64 little-endian x86-64 file format.
- `image_system`, `image_vendor`: root-owned fixed-store image files
  with the expected EROFS/ext4 superblock signatures.
- `loop_control`: the dedicated kernel device node exists.

The observer's SELinux domain is **not** the Waydroid manager. The helper's
optional `--help` read-only invocation is logged as information only:
an SELinux refusal from an unrelated observer domain must not be mistaken
for a failed manager-to-helper transition. This probe does **not** execute
any privileged helper CLI operation.

The probe logs filesystem status, overlay directory readiness, observer
context, `helper_domain_transition=UNVERIFIED` and
`mount_syscalls=NOT_ATTEMPTED`. The parser refuses incomplete, duplicate
or permissive-mode evidence and always sets `release_ready=false`.

## Build a fresh diagnostic image

Work from the Stage 2E branch, with your existing LLVM submodule untouched.
Only create a **new** image. Never overwrite your diagnostic baseline:

```sh
cd /mnt/aosp/ThunderOS
python3 -m unittest discover -s scripts/tests \
  -p 'test_mount_preflight_report.py' -v
./build-openwave waydroid-mount
./build-openwave selinux
./build-openwave rootfs

# Requires previously built native Ocean binaries in rootfs.
# The image builder installs the new QA script and re-labels every inode.
OPENWAVE_IMAGE_SIZE=6G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-mount-preflight-2e.img
```

If the rootfs assembler refuses due to missing WaterRouge SDK exports,
do not patch the policy or skip security checks. Diagnose that build
dependency separately; existing verified UI binaries are prerequisites.

Use your verified Waydroid-capable QEMU kernel. The prior dedicated kernel
name is shown below **only if it exists and its matching config is verified**:

```sh
test -f out/boot-recovery/bzImage-waydroid-51
python3 scripts/test-openwave-boot.py \
  --mount-preflight --mode enforcing --seconds 120 \
  --image out/boot-recovery/ocean-waydroid-mount-preflight-2e.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-mount-preflight-2e-evidence
```

The QEMU runner already uses `-snapshot`, SHA-256 hashes the base kernel
and disk before and after boot, and stores logs in a new directory that must
not exist previously. It requires an OpenGL-capable QEMU display. Do not
invoke privileged helper commands on the developer host.

Inspect results:

```sh
cat out/boot-recovery/waydroid-mount-preflight-2e-evidence/mount-preflight.json
grep -E 'MOUNT_QA_(BEGIN|GATE|INFO|END)' \
  out/boot-recovery/waydroid-mount-preflight-2e-evidence/serial.log
```

Even `"preflight_passed": true` proves only safe, read-only prerequisites.
It does **not** indicate successful overlay mounts, Waydroid startup, or
Android framework boot.

## Next enforcing guest acceptance test

Only after confirming this report: design a **disposable guest-only**
mount operation harness, with a dedicated exact-scoped SELinux domain,
verified mount namespaces/loop-control and failure rollback. It must test
the actual FD mount targets, readonly overlay upper/work compatibility,
host EGL/ODM, property target provisioning, session-data bind, and child-first
teardown. It must never use the developer host as a test mount namespace.

After that: integrate Waydroid through a reproducible, integrity-verified
adapter, remove the manager diagnostic `CAP_SYS_ADMIN` and run the complete
SELinux suite. Framework/graphics/shortcut acceptance remains separate.
