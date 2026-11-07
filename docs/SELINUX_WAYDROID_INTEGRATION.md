# OpenWave LXC / Waydroid SELinux integration ledger

Status: **Stage 1 implemented on a review branch. Full Waydroid acceptance NOT verified.**

## Source baseline and scope

The public repository's `main` branch lacks the active local LXC 6.0.6,
Waydroid 1.6.3, SELinux helper, and image-mount implementation. This branch
therefore implements only the demonstrable ELF loader/shared-library labeling
fix. Do **not** deploy it as a finished LXC/Waydroid policy. The local build
rootfs and disposable VM must be checked against the newer working tree.

Reference: Fedora waydroid.te and waydroid.fc (rawhide, dist-git commit
96738aadc1fed2dd9dc82af234fac167625b332c), and upstream LXC
lxc.container.conf(5), SELINUX CONTEXT. Fedora's `container_runtime_t`,
systemd interfaces, refpolicy macros and `mac_admin` grant are **not**
transferred into OpenWave.

## Stage 1: dynamic-linker labeling (this branch)

- `openwave_elf_loader_t`: only dynamic interpreters such as
  `/lib64/ld-linux-x86-64.so.2`.
- `openwave_shared_lib_t`: runtime shared objects in system library
  directories and the native bundle lib/plugin/qml directories.
- Regular applications have read/open/getattr/map/execute and symlink-read
  access to these types, but **not** write, relabel, create or modify access.
- Trusted service domains keep mapped-library access. Their current broader
  allowances remain a separate hardening issue.
- Existing service execution labels, application data labels, SELinux policy
  labels, and `appd` domain transitions are preserved.
- Three negative compile-policy mutations assert that an application cannot
  acquire shared-library/loader write permissions.
- Generated file contexts are now validated using the actual libselinux
  matcher before staging completion.
- The actual-inode audit fails on a missing `security.selinux` xattr, absent
  pathname, or unexpected type.

Known limitation: `kernel-policy.c` emits
`allow openwave_system_domain openwave_all_types (<class> (all))`
for every kernel class. The existing service-domain attribute includes
`openwave_appd_t` and other trusted native services. Do not add
`openwave_lxc_t`, `openwave_android_t`, or the Waydroid manager to that
attribute; they would inherit extensive authority. A later policy review
should replace the trusted-service blanket grant with explicit minimum rules,
without breaking existing services.

## Stage 1 build and validation

Execute from the **updated local source tree**, not from the development
host's unrelated SELinux policy:

```sh
git fetch origin selinux/elf-runtime-labels-20261108
git switch selinux/elf-runtime-labels-20261108
python3 -m unittest -v userspace/security/selinux/tests/test_file_contexts.py
./build-openwave selinux
python3 userspace/security/selinux/tests/test_policy.py
./build-openwave userspace --image out/OpenWave-selinux-stage1.img
./run-openwave --image out/OpenWave-selinux-stage1.img run
```

The build produces `policy.35` and generated file_contexts; the new
build-stage check uses the **generated** specs, including appd's entrypoint.
Run the following checker **inside the OpenWave guest after boot-time
restorecon**, before running Waydroid, with the checker copied into the guest:

```sh
python3 check_installed_labels.py --root / --arch x86_64
cat /sys/fs/selinux/enforce   # must output 1
cat /proc/self/attr/current
ls -lZ /lib64/ld-linux-x86-64.so.2 /usr/lib64/libselinux.so.1 /System/Services/appd
```

A host-side unpacked staging directory can lack labels because OpenWave
applies them at boot; this checker must never report that unlabeled rootfs
as passing. Preserve the previous bootable image. Do not overwrite an
existing `out/*.img` file.

## Stages not implemented in this branch

1. **Exact local audit.** Freeze file hashes, service entrypoints, mount
   paths, `lxc-start` labels, current AVCs, image metadata, `lxc config`,
   OpenRC services and rootfs paths. Audit effective context mappings and
   installed inode xattrs (not just expected regex output).
2. **Runtime/manager split.** Add `openwave_waydroid_manager_t`,
   `openwave_lxc_t`, and separately labeled manager/LXC entrypoints.
   Preserve the established manager-to-LXC transition. Do not allow every
   utility to enter the LXC runtime type. Confirm `/proc/PID/attr/current`
   for both processes after startup.
3. **Waydroid paths.** Separate types for persistent mutable data, sockets
   and fifos, network script and its executable, host Android system/vendor
   mountpoints, and BinderFS device nodes. Use the **local** actual paths,
   not assumed Fedora paths. Manager may manage only its own state; system/
   vendor image data is read-only on the host where possible.
4. **LXC/Android boundary.** Add a separate `openwave_android_t` for
   container PID 1 and its descendants, with explicit rootfs entrypoint
   rules and only required Binder/device/IPC access. The new type MUST NOT
   belong to `openwave_system_domain`.
5. **LXC native SELinux configuration.** LXC supports
   `lxc.selinux.context` and `lxc.selinux.context.keyring`, but first
   validate the Android entrypoint file type, role/type validity,
   requested transition, and Android init execution. Then set:

   ```ini
   lxc.selinux.context = system_u:system_r:openwave_android_t:s0
   lxc.selinux.context.keyring = system_u:system_r:openwave_android_t:s0
   ```

   Never enable this blindly or as a substitute for an allowed transition.
6. **Image mounts.** Inspect EROFS `system.img` and ext4 `vendor.img`
   filesystem features and existing `security.selinux` xattrs on
   **read-only copies or read-only loop mounts**. Use appropriate
   `context=` / `rootcontext=` / `defcontext=` mount options only
   after testing compatibility with the kernel, filesystem and Android
   labels. Never run blanket `restorecon -R` inside image mounts, and
   never silently replace Android's original xattrs. A single fixed mount
   context does not replace Android's per-file labels when the Android
   environment requires them.
7. **BinderFS/IPC.** Verify kernel supports binder/binderfs and applicable
   SELinux `binder` class. Assign device/node types and explicit access
   only between the validated container peers. Do not borrow Fedora's
   blanket `container_runtime_t` permissions or `capability2 mac_admin`.
8. **Remove diagnostics only after regression gates.** Enumerate temporary
   broad capabilities, `execute_no_trans`, `sys_admin`, `NoNewPrivs=0`,
   seccomp-disabled and privileged test-manager exceptions. For each rule,
   capture a before/after AVC and operation test. Record a rollback commit.
9. **Android and shell acceptance.** Confirm LXC started, Android init
   services healthy, `sys.boot_completed=1`, reliable logcat messages,
   and `cmd package list packages` returns. Check
   `waydroid.<package>.desktop` imports go through appd's validated
   compatibility importer; actually launch an imported Android app from
   Ocean Launcher without treating the desktop file as authoritative.

## Required acceptance evidence

| Gate | Evidence | Status |
| --- | --- | --- |
| File-context regex mapping | libselinux matcher unit tests | Pass on developer sandbox |
| Loader/library CIL compilation | Target `./build-openwave selinux` | NOT RUN |
| Mutation neverallow checks | Target policy compiler, negative rules | NOT RUN |
| Actual installed inode labels | Guest `getxattr` / `ls -Z` | NOT RUN |
| Standalone LXC startup | Process context + logs in guest | NOT RUN |
| Waydroid startup | LXC and Waydroid logs in guest | NOT RUN |
| Android boot | `getprop sys.boot_completed` exactly `1` | NOT RUN |
| Android app shortcuts | appd registry and launch on Ocean Launcher | NOT RUN |
| Enforcing throughout | `/sys/fs/selinux/enforce=1` before/after | NOT RUN |

**No diagnostic permissions were removed on this branch. No Waydroid
startup or QEMU acceptance has been claimed.**
