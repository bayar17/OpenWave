# Waydroid Stage 2C — fixed overlay commands (NOT ACTIVATED)

## Status and outstanding failure

Stage 2C adds **staged** system/vendor overlay operations to the
fixed-operation mount helper. It **does not** install any new privileged
SELinux grants, change the pinned Waydroid sources, enable a PATH wrapper or
route active Waydroid mounts through this helper.

The manager still has temporary `CAP_SYS_ADMIN` in `openwave.cil`.
The three existing negative policy tests remain failing in the **full** suite.
No overlay mount has been verified inside QEMU; a successful host build
is not proof that the guest kernel accepts the mount flags/options.

## Fixed commands

The new zero-path-argument operations are:

| Operation | Purpose |
| --- | --- |
| `--overlay-system` | Overlay the mounted EROFS `/var/lib/waydroid/rootfs` |
| `--overlay-vendor` | Overlay mounted ext4 `/var/lib/waydroid/rootfs/vendor` |
| `--unoverlay-vendor` | Remove only an overlayfs vendor top layer |
| `--unoverlay-system` | Remove only an overlayfs rootfs top layer |

All require root identity, the dedicated `openwave_waydroid_mount_t`
domain and SELinux enforcing. There is **no** arbitrary source, destination,
flag or option argument. The required image mount must already be present.

For system, the lower layers are the fixed
`/var/lib/waydroid/overlay` and `/var/lib/waydroid/rootfs`; the
upper and work directories are respectively
`/var/lib/waydroid/overlay_rw/system` and
`/var/lib/waydroid/overlay_work/system`.

For vendor, the lower layers are
`/var/lib/waydroid/overlay/vendor` and
`/var/lib/waydroid/rootfs/vendor`, with upper/work suffix `vendor`.

Source/mountpoint directory traversal uses `openat2` with
`RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS` and fd-pinned
`/proc/self/fd/N` references; lower/upper/work must be root-owned and not
group/world writable, and upper/work must be distinct on the same device.
The backing rootfs/vendor must be a **distinct**, correctly typed EROFS/ext4
mount. Overlay mount options are bounded and fixed
(`lowerdir=...,upperdir=...,workdir=...,xino=off`); flags specify
`MS_RDONLY|MS_NOSUID|MS_NODEV`.

**Critical guest compatibility gate:** Upstream's
`mount_overlay(..., readonly=True)` requests `ro` even when specifying
upper/work directories. The helper follows that requested behavior; some
Linux kernels may reject it. It must not automatically retry with a
writable overlay or claim success without actual guest mount evidence.

Overlay teardown requires an actual overlayfs superblock at the exact
approved target; nested vendor/property mounts must be removed first.
It uses `umount2(..., 0)`, not lazy or recursive detach. Linux may still
reject attempts if additional nested mounts exist.

The existing fixed `--bind-prop`/ `--unbind-prop` functions now recognize
either ext4 vendor or an overlayfs vendor top layer. The property target is
**still required to exist** and be root-owned, which may need a safe
creation/provisioning step in future work. A readonly overlay may prevent
that creation entirely. This is a blocker, not an implicitly fixed feature.

## Remaining upstream operations

- Optional host EGL binds and host ODM/fallback ODM directory binds.
- Backward-compatibility session-data bind.
- Full rootfs descendants mount enumeration and teardown.
- Optional legacy cgroup schedtune unmount.
- Verified creation and labeling of Android property target and overlay dirs.
- Manager-to-helper adapter without modifying pinned Waydroid package
  unexpectedly; must be included in package integrity checks.
- QEMU evidence: precise CIL/loop/mountpoint rights in the helper domain,
  correct transition, Android container and framework startup.
- Remove manager `sys_admin` and old diagnostics **only after** the
  replacement is proven; all SELinux policy tests must pass unmodified.
- Ocean graphics, Android back-only navigation and appd shortcuts need
  separate authenticated runtime acceptance.

## Host-only checks

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

Do **not** call the privileged `--overlay-*` or `--unoverlay-*`
commands on the development host. A guest QEMU validation uses only a
disposable disk snapshot under enforcing SELinux and must verify
actual mount IDs and filesystem types.

This branch is an **inactive security staging change**, not a working
Waydroid boot fix.
