# OpenWave SELinux: Stage 1 ELF interpreter / shared-library isolation

**Status: changes staged on a review branch; compiled target policy and guest
acceptance are UNVERIFIED. Not production-ready Waydroid support.**

Baseline: GitHub main commit `99610a429b68d2540f9e68e83f53d14ac984d714`.
This stage intentionally does **not** modify LXC 6.0.6, liblxc, Waydroid
1.6.3, Android images, appd's registry or the current OpenRC runlevels.

## Problem

The existing `file_contexts` maps `/(usr|lib|lib64|bin|sbin|etc)(/.*)?`
to `openwave_runtime_t`, including `/lib64/ld-linux-x86-64.so.2`,
`/lib64/libc.so.6`, `/usr/lib64/libselinux.so.1` and
`/usr/lib64/liblxc.so.1.8.0`. LXC's restrictive
`openwave_lxc_t` needs to open/map its interpreter and shared objects, but
granting broader access to `openwave_runtime_t` would also grant unrelated
runtime executable access. The previous Diagnostic 87 rule still exists for
legacy files and is **not** counted as completion of this migration.

## Architecture

- `openwave_elf_loader_t`: ELF program interpreter path and physical inode,
  including `ld-linux-*.so.*` and `ld-2.*.so`.
- `openwave_shared_lib_t`: installed trusted shared libraries under
  `/lib*`, `/usr/lib*`, system application lib/plugins/qml, and
  `/System/Services/lib` or plugins. This does **not** match
  `/Applications` user-installed bundles or Android image contents.
- `openwave_dynamic_link_client`: grants only open/read/getattr/map/execute
  of these two types plus interpreter execute_no_trans and symlink traversal;
  includes PID 1, native service domains, native apps, Waydroid manager
  and helpers, and the dedicated LXC runtime.
- `openwave_untrusted_link_client`: all of those except PID 1; a CIL
  `neverallow` prohibits writing, creating, removing, renaming, changing
  inode metadata and relabeling trusted ELF files.
- The existing `openwave_lxc_t` transition from
  `openwave_waydroid_manager_t` via `openwave_lxc_start_exec_t` stays
  unchanged. No LXC utility inherits a privileged container-runtime type.
- `openwave_runtime_t` remains for programs and resources not yet moved;
  prior execution rules stay in place to avoid breaking unrelated services.
  Do not remove these until real QEMU label/exec coverage is recorded.

## Security justification

The new permissions implement only trusted interpreter execution and
executable memory mappings of system libraries. The policy does not add
`sys_admin`, mount, file write, Binder, device, network, setenforce, policy
load, application data access or process-transition permission. It does not
introduce Fedora `container_runtime_t`.

The same generated file_contexts used to assemble the disk image is checked
with libselinux before a fresh build manifest is published. The compiled
policy inspector checks effective access vectors for LXC, Waydroid, native
apps, and representative native service domains. OpenWave's existing
`openwave-label-ext4.py` must still apply and reread every inode label in
the **new disposable disk image**; the bootstrap checks critical inode
labels before starting OpenRC.

## Build and static acceptance (run on the OpenWave build host)

```sh
git fetch origin selinux/stage1-elf-library-isolation-20261108
git switch selinux/stage1-elf-library-isolation-20261108
python3 userspace/security/selinux/tests/test_file_contexts.py
./build-openwave selinux
OPENWAVE_POLICY_BUILD="$PWD/out/sdk-dependencies/x86_64-unknown-openwave/selinux-policy" \
  python3 userspace/security/selinux/tests/test_policy.py
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=6G ./build-openwave image rootfs \
  out/OpenWave-stage1-linker-enforcing.img
```

`./build-openwave selinux` must compile the CIL, run the compiled
`inspect-qa-policy` checks and run the effective generated-context tests.
The image builder checks the exact policy/context hash and reads back inode
xattrs. **Do not reuse an old boot image**; it would retain old inode labels.

## Guest acceptance on a disposable image

Use the correct Waydroid-capable kernel built locally and keep the previous
known-good image untouched:

```sh
./run-openwave --kernel out/boot-recovery/bzImage-waydroid-51 \
  --image out/OpenWave-stage1-linker-enforcing.img run
```

Inside the guest, record these actual labels, process contexts and denials:

```sh
cat /sys/fs/selinux/enforce
ls -lZ /lib64/ld-linux-x86-64.so.2 /lib64/libc.so.6 \
  /usr/lib64/libselinux.so.1 /usr/lib64/liblxc.so.1.8.0 \
  /usr/bin/lxc-start /System/Services/appd
cat /proc/1/attr/current
dmesg | grep -E 'avc: denied|SELinux|lxc-start|waydroid'
```

**Acceptance** requires `/sys/fs/selinux/enforce=1`, expected actual
inode labels, no new library-loader denials for appd/Ocean/Waydroid/LXC,
and unchanged native app isolation. Running `lxc-start` alone is not Android
boot success. Android framework completion and appd/Ocean Launcher shortcuts
remain subsequent stages.

## Baseline blockers and rollback

Current `openwave.cil` includes temporary Waydroid manager `sys_admin`
and generic filesystem-mount permissions around Diagnostics 72–77.
`test_policy.py` has assertions that the manager lacks `sys_admin`.
These are contradictory in the **baseline** and will need resolution when
the manager's mounting responsibilities move into a verified LXC/runtime
boundary. This stage does not silently remove those diagnostic permissions.

No old diagnostic `allow` rules were deleted. No Waydroid container PID 1
or Android SELinux policy was activated, no EROFS/ext4 Android mount options
were changed, and no SELinux enforcement bypass was introduced.

Keep `main` and the previous bootable image as rollback points. Stop
deployment on any compile, context, inode label or QEMU failure. Do not
merge this branch until the full policy compiles and native guest services
pass their acceptance gates.
