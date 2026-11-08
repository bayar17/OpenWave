# Stage 2I — read-only Waydroid mount preflight under SELinux enforcing

## Observed failure

The Stage 2H guest completed a clean enforcing QEMU boot and emitted a
complete Waydroid mount-preflight report, but preflight returned BLOCKED:

- `helper_inode` — blocked even though the helper is installed; the
  probe used `os.access(HELPER, os.X_OK)`, which tests SELinux **execute**
  authorization for the observer, not only inode mode.
- `helper_label` — `getxattr('security.selinux')` raised PermissionError.
- `helper_elf` — reading 20 bytes of the executable failed. Report detail
  `ELF64 little-endian x86_64` was the *expected* architecture string,
  not proof that the gate succeeded.
- `loop_control` — `Path.exists()` requires access to device metadata,
  and returned False although sysfs and `/dev/loop0..7` existed.
- The observer executed `waydroid-mount --help`, despite the
  privileged helper not being executable from `openwave_system_t`.
  It creates an avoidable expected AVC and must not be required to
  pass a **read-only** preflight.

The Android system/vendor image gates passed, while helper domain
transition, actual mounts, and Android startup were **not attempted**.

## Changes

- Only these SELinux allows are added:

```cil
(allow openwave_system_t openwave_waydroid_mount_exec_t
       (file (getattr read open)))
(allow openwave_system_t openwave_loop_control_t
       (chr_file (getattr)))
```

- The helper-inode test inspects root ownership, a regular non-writable
  inode, exactly one hardlink, and **executable mode bits** without
  requesting SELinux execute access.
- No `--help` execution; the report records
  `helper_help=NOT_ATTEMPTED_READ_ONLY_OBSERVER`.
- The loop-control gate checks a root-owned character device with
  Linux misc major 10, loop-control minor 237, without opening it or
  issuing ioctls.
- Gate records are emitted in a single write; the report parser
  strips NUL bytes and audit records glued to gate detail.
- Host tests enforce narrow observer access and absence of helper
  execution; a compiled-policy test checks metadata-read grants
  while rejecting new execute and device-open/ioctl authority.

**No new execution, process transitions, loop-control ioctl, CAP_SYS_ADMIN,
mount, overlay, or Waydroid manager privileges.**

## Run locally

```bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/mount-preflight-observer-stage2i-20261108 ||
  git switch --track origin/waydroid/mount-preflight-observer-stage2i-20261108

python3 -m unittest discover -s scripts/tests \
  -p test_mount_preflight_report.py -v

# Compile new SELinux policy and test narrow observer invariants.
./build-openwave selinux
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_waydroid_mount_preflight_observer_cannot_execute_or_control_device -v

# Reuse the already staged native UI binaries (from Stage 2H).
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-preflight-2i.img

python3 scripts/test-openwave-boot.py \
  --mount-preflight --mode enforcing --seconds 120 \
  --image out/boot-recovery/ocean-waydroid-preflight-2i.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-preflight-2i-evidence

E=out/boot-recovery/waydroid-preflight-2i-evidence
cat "$E/mount-preflight.json" "$E/result.json"
grep -aEi 'MOUNT_QA_(GATE|INFO|END)|avc:.*denied' \
  "$E/serial.log" | tail -n 150
```

Success requires `mount_preflight_passed=true` plus all seven
read-only gates PASS. Verify the helper label explicitly matches
`openwave_waydroid_mount_exec_t`; `helper_help` stays intentionally
unattempted, and `helper_domain_transition=UNVERIFIED` and
`mount_syscalls=NOT_ATTEMPTED` remain unchanged.

If any gate remains blocked, retain the AVCs with `scontext`,
`tcontext`, `tclass` and permissions and fix that exact cause,
not broad classes or generic `openwave_default_t` access.

The original `CAP_FSETID` and executable-memory denials,
WaterRouge SDK completeness, and actual Waydroid mounting/boot are
not addressed by this diagnostic-only patch.
