# Stage 2A — Waydroid EROFS/ext4 mount boundary

**Status:** staged implementation, deliberately NOT activated. Do not claim
Waydroid is operational, do not merge as a security fix, and do not remove the
existing manager `CAP_SYS_ADMIN` diagnostic grant yet.

## What is implemented

The fixed-operation C helper is
`userspace/waydroid/waydroid_mount.c`; the cross-build now installs
`/usr/libexec/openwave-waydroid/waydroid-mount`.

It is **not** named `mount` or `umount` and is not inserted as a
PATH shim. The pinned Waydroid 1.6.3 source and its upstream package hash
verification remain untouched. Existing OpenRC and manager entrypoints remain
unchanged.

The helper's privileged API has **no user-supplied pathname parameters**:

| Operation | Source / destination |
| --- | --- |
| `--mount-system preinstalled` | `/usr/share/waydroid-extra/images/system.img` to `/var/lib/waydroid/rootfs` |
| `--mount-system persistent` | `/var/lib/waydroid/images/system.img` to `/var/lib/waydroid/rootfs` |
| `--mount-vendor preinstalled` | `/usr/share/waydroid-extra/images/vendor.img` to `/var/lib/waydroid/rootfs/vendor` |
| `--mount-vendor persistent` | `/var/lib/waydroid/images/vendor.img` to `/var/lib/waydroid/rootfs/vendor` |
| `--unmount-vendor` | fixed vendor target only |
| `--unmount-system` | fixed rootfs target only |
| `--verify-system/vendor preinstalled/persistent` | read-only image check |

It validates EROFS/ext4 superblock magic, root ownership and permission bits
for privileged operations; resolves every component with `openat2` and
`RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS`; pins the
destination inode and compares mount IDs using `statx`; uses readonly
autoclear loop devices; and requests readonly/nodev/nosuid mounts. It never
runs a shell or accepts a generic mount command. Destructive operations
require root identity, exact `openwave_waydroid_mount_t` process context and
`/sys/fs/selinux/enforce=1`. Unmounts are limited to validated EROFS/ext4
mountpoints and never use lazy/recursive flags.

Only host QA builds compiled with `OPENWAVE_MOUNT_TESTING=1` expose a
read-only `--inspect-image TYPE FILE` fixture API. Production builds never
accept an arbitrary filename.

## SELinux architecture

New types:
- `openwave_waydroid_mount_t`: future short-lived mount domain
- `openwave_waydroid_mount_exec_t`: dedicated helper executable

The manager-to-helper entrypoint transition and required dynamic loader
read/mapping permissions are staged. Neither the runtime manager nor the
new helper receives any new mount capability in Stage 2A.

Explicit `neverallow` rules currently prohibit the new helper from obtaining
`sys_admin`, host security-policy mutation authority, or filesystem mount
permission on generic `openwave_default_t` superblocks. Native app
execution of the mount-helper entrypoint is prohibited.

**The result cannot mount images yet. This is intentional.** The fixed
C implementation is untrusted until its actual guest behavior, process
transition and mount-label boundary are validated. Relax those prohibitions
only through a narrowly justified, QEMU-tested Stage 2B policy.

## Why it is not activated

Upstream `tools/actions/container_manager.py` invokes:
- `tools.helpers.images.mount_rootfs` for system/vendor images;
- overlays if `mount_overlays=True` (the upstream default);
- multiple host EGL/ODM bind mounts;
- bind of the generated vendor `waydroid.prop`;
- conditional session-data bind mounts;
- `umount_all` during rootfs/container shutdown;
- optional cgroup/schedtune unmounts outside Android rootfs.

Merely replacing `mount` in Waydroid's PATH, or pre-mounting once in OpenRC,
would break these lifecycle operations. In addition, SELinux permissions for
loop devices and filesystem superblocks must be derived from **actual inode
labels and guest AVCs**, not assumed from source types. The ext4 image may
contain Android `security.selinux` labels and must not be relabeled by a
host-side `restorecon -R`.

Next: integrate all required mount/bind/overlay/unmount operations with
fixed-path validation and no arbitrary bind sources; establish the actual
SELinux mount/device and capability boundary in a disposable QEMU guest;
verify that the manager never runs mount syscalls directly; then remove
the old manager `sys_admin`, generic filesystem/mountpoint and loop-device
Diagnostics 72–77 once the replacement passes. Never weaken the three
pre-existing neverallow tests.

## How to test this stage

On the OpenWave build host, after switching to this branch:

```sh
python3 -m unittest discover -s userspace/waydroid/tests \
    -p test_mount_helper.py -v
python3 userspace/security/selinux/tests/test_file_contexts.py
./build-openwave selinux
OPENWAVE_POLICY_BUILD="$PWD/out/sdk-dependencies/x86_64-unknown-openwave/selinux-policy" \
    python3 userspace/security/selinux/tests/test_policy.py -v \
    -k test_mount_helper_stage2a_is_transitional_but_unprivileged
./build-openwave userspace
```

The target `./build-openwave selinux` and C cross-build have NOT been run
by this session. CI tests only host compiler compatibility and no-privilege
behavior. Run these tests locally and preserve the full-suite expected
failures as blockers. The unified audit will continue to report the
manager's old CAP_SYS_ADMIN as FAIL.

### Stage 2B acceptance

1. All current mount paths mapped and validated (including binds, overlays,
   cleanup) without changing the pinned upstream source unexpectedly.
2. Mount helper verified in its actual SELinux domain using
   `/proc/PID/attr/current` and `/sys/fs/selinux/enforce=1`.
3. Expected image/mountpoint and loop device labels confirmed from installed
   rootfs inodes, not only file-context regexes.
4. Verified correct EROFS system and ext4 vendor mounts in disposable QEMU.
5. Old manager broad grants removed and all policy tests pass without skipping.
6. LXC reaches RUNNING, Android reports `sys.boot_completed=1`, and package
   enumeration succeeds; graphics and appd shortcuts require separate tests.
