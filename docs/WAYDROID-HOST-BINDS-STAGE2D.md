# Waydroid Stage 2D — fixed EGL/ODM host-directory binds

## Status

**NOT ACTIVATED; NOT A WAYDROID BOOT FIX.** This stage extends the
compiled fixed-operation C helper with host EGL/ODM directory bind commands.
No SELinux mount permissions are granted. The manager retains its temporary
`CAP_SYS_ADMIN` diagnostic grant; the known three security policy tests
against that grant remain red. No guest mount or Android lifecycle is verified.

## Whitelisted operations

These commands take **no path arguments**:

| Bind command | Source on OpenWave host | Destination inside Waydroid rootfs |
| --- | --- | --- |
| `--bind-egl32` | `/vendor/lib/egl` | `/var/lib/waydroid/rootfs/vendor/lib/egl` |
| `--bind-egl64` | `/vendor/lib64/egl` | `/var/lib/waydroid/rootfs/vendor/lib64/egl` |
| `--bind-odm` | `/odm` | `/var/lib/waydroid/rootfs/odm_extra` |
| `--bind-vendor-odm` | `/vendor/odm` | `/var/lib/waydroid/rootfs/odm_extra` |

Each has an exact `--unbind-*` counterpart.

Pinned Waydroid 1.6.3 checks host EGL directories before binding them. It
chooses `/odm` if already mounted, otherwise checks `/vendor/odm`.
**The eventual adapter must preserve these conditions** and must never run
both ODM forms simultaneously: they share the same destination.

The helper:
- Rejects arbitrary paths, options, mount flags and unknown operations.
- Requires root UID/GID, the dedicated `openwave_waydroid_mount_t` domain,
  and enforcing SELinux for these privileged operations.
- Opens sources and destinations via hard-coded absolute paths using
  `openat2`, `RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS`,
  and FD-pinned mount paths.
- Requires root-owned, non-group/world-writable source and destination
  directories; destination parents must reside on approved Android image
  or overlay filesystem types.
- Rejects pre-existing bind mounts rather than stacking new mounts.
- Uses `MS_BIND` followed by `MS_REMOUNT|MS_BIND|MS_RDONLY|MS_NOSUID|MS_NODEV`.
  If remount fails, it attempts to undo the bind.
- When unbinding, checks that the destination is a distinct mountpoint
  and identifies exactly the allowlisted host source device/inode.
- Uses `umount2(..., 0)` without lazy or recursive teardown.

## Known limitations

This code is an **inactive fixed-operation primitive**. The guest must still
validate whether its actual vendor image, overlay configuration, Waydroid
target paths, mount namespaces, and SELinux labels satisfy the checks.

Unlike upstream's `helpers.mount.bind()`, it does not create missing
destination directories, does not silently treat an existing mount as
success, and insists on readonly bind mounts. An eventual OpenWave adapter
must either provision approved directories before execution or detect and
report missing paths. Never bypass fixed-path checks to make startup pass.

The helper requires the source directory to still exist when unbinding, to
verify its inode. Unmounting may therefore fail closed if the host device is
disconnected; a future teardown service must handle that case without
arbitrary-path authority.

**Not implemented:** the upstream backward-compatibility session-data bind,
enumeration of every nested rootfs mount, and the optional cgroup schedtune
compatibility unmount. Any host mount syscall behavior, procfd mounting,
EROFS/overlay upper/work flag combination and mount identity enforcement are
still UNVERIFIED in QEMU.

## Build-host validation

```bash
cd /mnt/aosp/ThunderOS
python3 -m unittest discover -s userspace/waydroid/tests \
  -p test_mount_helper.py -v
./build-openwave waydroid-mount
./build-openwave selinux
```

Expected **18 offline tests**, with 4 new tests validating exact EGL/ODM
allowlist, argument rejection, fail-closed host use, readonly remount and
unmount provenance. GitHub Actions runs host-only compile/tests, not
OpenWave guest mounts.

**Never run `--bind-*`, `--unbind-*`, `--overlay-*` or
`--mount-*` on the developer host.** Run operations only in a disposable
SELinux-enforcing OpenWave QEMU guest when a dedicated runtime validation
harness is prepared.

## Requirements before retiring manager CAP_SYS_ADMIN

1. Implement or explicitly disable upstream session bind and optional
   cgroup compatibility mounts; define safe complete child-first cleanup.
2. Verify the helper's actual SELinux transition and exact inode labels.
3. Validate EROFS, ext4, optional overlayfs, property bind, and optional
   EGL/ODM binds under enforcing SELinux using a disposable QEMU image.
4. Wire a small, reproducible adapter through pinned upstream package
   integrity verification; never substitute a generic PATH mount wrapper.
5. Remove manager diagnostic `sys_admin`, loop and host mount permissions.
6. Pass the complete SELinux tests without bypasses; confirm LXC RUNNING,
   Android framework boot, and independently authenticated Ocean shortcuts
   and graphical integration.
