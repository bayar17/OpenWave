# Waydroid mount boundary — Stage 2B, property bind (staged)

## Current blocker

The long-lived `openwave_waydroid_manager_t` still has `CAP_SYS_ADMIN`
and the 3 SELinux negative assertions against that capability remain red.
The new helper is deliberately **not invoked** by Waydroid and its
`openwave_waydroid_mount_t` domain still has an explicit `neverallow`
against `sys_admin` and broad filesystem mount operations.

No mount or unmount operation has been executed in a guest by this change.
Do **not** remove manager permissions or merge this change as a finished
Waydroid SELinux integration.

## New fixed operation

The C helper has two additional root-only commands:

```
/usr/libexec/openwave-waydroid/waydroid-mount --bind-prop
/usr/libexec/openwave-waydroid/waydroid-mount --unbind-prop
```

These take **zero path arguments**. The source and destination are
compile-time constants:

- Source: `/var/lib/waydroid/waydroid.prop`
- Target: `/var/lib/waydroid/rootfs/vendor/waydroid.prop`

The helper requires the exact staged SELinux domain, `enforce=1`,
root UID/GID, and preexisting root-owned regular files. The source must
have a single hardlink, a size in `1..65536` bytes, and no group/world
write permission. It uses `openat2` fixed-path resolution with
`RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS`. The vendor
directory must already be an ext4 mount, and the target must not already
be a separate mountpoint before binding. After a file bind it attempts a
`MS_REMOUNT|MS_BIND|MS_RDONLY|MS_NOSUID|MS_NODEV` remount and rolls back
if remount fails. `--unbind-prop` requires the mounted target to identify
the *same device/inode* as the approved property source and is not lazy
or recursive.

**Limitations:** This assumes the source exists throughout cleanup and
the vendor target already exists (the upstream function may create it).
Both are acceptance blockers until the actual boot/provisioning path is
verified. Mount operations against `/proc/self/fd/N` must be verified in
the guest; a successful C compile is not proof of Linux mount semantics.
No host-side mount test should ever be run.

## Upstream Waydroid 1.6.3 lifecycle map

| Upstream action | Staged helper support | Remaining acceptance work |
| --- | --- | --- |
| System `system.img` EROFS readonly mount | Stage 2A command, inactive | Loop, image, mountpoint, enforcing guest evidence |
| Vendor `vendor.img` ext4 readonly mount | Stage 2A command, inactive | Loop, mountpoint and guest labels |
| Generated `waydroid.prop` bind | Stage 2B fixed command, inactive | Existing destination, permitted selinux, mount syscalls |
| System/vendor overlayfs mounts (upstream default on) | **NOT IMPLEMENTED** | Fixed-path safe overlay policy, upper/work dirs, mount identity |
| Host EGL, /odm and fallback /vendor/odm bind mounts | **NOT IMPLEMENTED** | Verify optional host sources before considering allowlist |
| Backward-compatibility session data bind | **NOT IMPLEMENTED** | Authorize user/session identity and destination |
| Recursive rootfs descendants unmount during stop | **NOT IMPLEMENTED** | Enumerate validated mounts and unmount order |
| Optional cgroup schedtune unmounts | **NOT IMPLEMENTED** | Decide whether OpenWave disables or isolates upstream compatibility operation |

A generic `PATH` replacement for `mount` or `umount` is expressly
not acceptable: this would intercept operations whose path models have
not been validated. The pinned upstream implementation must remain
integrity-checked; any necessary adapter patch should be reproducible,
minimal and covered by the upstream package hash verification.

## Validation on the real OpenWave build host

```bash
cd /mnt/aosp/ThunderOS
python3 -m unittest discover -s userspace/waydroid/tests \
  -p test_mount_helper.py -v
./build-openwave waydroid-mount
./build-openwave selinux
OPENWAVE_POLICY_BUILD="$PWD/out/sdk-dependencies/x86_64-unknown-openwave/selinux-policy" \
  python3 userspace/security/selinux/tests/test_policy.py -v \
    -k test_mount_helper_stage2a_is_transitional_but_unprivileged
```

**Do not issue `--bind-prop`, `--mount-system` or `--unmount-*`
against your development host.** Run any privileged operation only in
a disposable OpenWave QEMU guest with verified immutable image evidence
and enforcing SELinux.

Next Stage 2B deliverables are complete overlay/bind/unmount coverage,
verified guest SELinux transitions, narrowed device/filesystem grants,
then retirement of manager `CAP_SYS_ADMIN`. Only after that can the full
negative policy suite and Android boot become release gates.
