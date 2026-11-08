# OpenWave Waydroid integration runner and privilege-boundary audit

**Status:** Source automation implemented and GitHub-hosted syntax/unit checks passed.
The SELinux `CAP_SYS_ADMIN` conflict has NOT been fixed. Android framework boot and
Ocean Launcher shortcuts are NOT verified. Do not merge as a finished Waydroid fix.

## Existing upstream execution flow

Pinned Waydroid: `5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991`.

The current upstream `tools/actions/container_manager.py::do_start` calls, in
order: binder driver preparation, the Waydroid network script, per-session LXC
configuration, rootfs mounts, then `helpers.lxc.start`.

Its `helpers.images.mount_rootfs` mounts the system image at the rootfs with
`umount=True`, then mounts vendor inside that rootfs. If overlays are enabled,
it also mounts overlayfs. Additional operations include read-only bind mounts
for host EGL/ODM directories and the generated `waydroid.prop` file. The stop
path unmounts descendants under rootfs.

`tools.helpers.mount` delegates to external `mount`/`umount` commands.
The manager's fixed `PATH` starts with `/usr/libexec/openwave-waydroid`, but
no audited mount dispatcher is currently integrated there.

**Implication:** Moving mounts to an OpenRC one-time prestart step is
insufficient. The upstream code explicitly unmounts and remounts the rootfs
when the Android session starts. Simply deleting `sys_admin` from the manager
will likely break these operations.

## Proposed privileged helper boundary (NOT YET IMPLEMENTED)

A short-lived `openwave_waydroid_mount_t` process and dedicated
`openwave_waydroid_mount_exec_t` entrypoint can own the image-mount interface.
It must never inherit the `openwave_system_domain` or `openwave_native_service`
attributes. The manager must retain only the permission to execute that
entrypoint and transition to the helper, with the helper receiving only the
verified device/mount permissions.

Before activation, implement and test all of the following:

1. Explicit opcode allowlist: attach EROFS system image, attach ext4 vendor
   image, bind vetted property/data directories, overlay only if explicitly
   approved, and detach only known Waydroid descendants.
2. A secure path model: walk from preopened trusted directory FDs with
   `openat2` (e.g. beneath/no-magiclinks/no-symlink policy as appropriate);
   verify inode, mount identity and owner. Avoid time-of-check/time-of-use
   substitution. Never accept an arbitrary source or destination pathname.
3. Mount restrictions: fixed source/destination pairs, EROFS system and ext4
   vendor validation, readonly/nosuid/nodev flags where appropriate and
   explicitly allowed loop ioctls; preserve existing Android SELinux xattrs.
4. Real lifecycle integration: hook all existing upstream mount and unmount
   calls without introducing a generic command runner. Preserve the pinned
   upstream source integrity record; if an adapter patch is unavoidable, pin
   and hash its exact contents and test upstream tampering rejection.
5. SELinux policy: narrow capability, loop control/block, filesystem mount,
   dir/file mounton, mountpoint traversal and process-transition rules. Keep
   `openwave_lxc_t` separate. Exclude host `/Data/Users`, native application
   storage, securityfs policy-control access and broad runtime writes.
6. Acceptance: compile CIL and inject negative `neverallow` mutations, check
   effective file contexts and actual guest inode labels, confirm enforcing
   mounts for both images, verify that the existing manager performs no mount
   syscall itself and no longer requires `sys_admin`.
7. **Only after** #6 passes in QEMU, remove the manager's broad
   `sys_admin` and Diagnostics 72/73/74/75/76/77 rules, with precise
   replacement evidence. Rerun the full 16-test policy suite, LXC and Android
   startup, and rollback if either native apps or Waydroid regresses.

An ordinary process transitioning to the helper does not automatically make
`CAP_SYS_ADMIN` safe: check process capabilities, setuid/exec inheritance,
LSM transition, mount-namespace behavior and the actual downstream syscalls.
Do not deploy a privileged PATH wrapper merely on the strength of an argv
parser or source-level policy compilation.

## All-in-one script

Entrypoint: `scripts/openwave-waydroid-integration.sh` (executable).

It delegates to `scripts/openwave-waydroid-integration.py`. The backend
works with the repository's actual existing `build-openwave` commands,
SELinux policy suite and `scripts/test-openwave-boot.py` QEMU harness.

```bash
cd /mnt/aosp/ThunderOS
./scripts/openwave-waydroid-integration.sh audit \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --kernel-config out/linux-waydroid-20261106/.config

./scripts/openwave-waydroid-integration.sh build
./scripts/openwave-waydroid-integration.sh test

# Image creation reruns the full security suite and FAILS CLOSED
# on the known three CAP_SYS_ADMIN violations.
./scripts/openwave-waydroid-integration.sh image

# After image creation actually passes:
./scripts/openwave-waydroid-integration.sh qemu \
  --image out/waydroid-integration/<RUN>/OpenWave-waydroid-enforcing.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --kernel-config out/linux-waydroid-20261106/.config

# Entire flow, including QEMU only after preceding gates pass:
./scripts/openwave-waydroid-integration.sh all \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --kernel-config out/linux-waydroid-20261106/.config
```

Replace these illustrative kernel paths only after confirming they are
present and correspond to the same Waydroid-enabled kernel build. The runner
will block a missing kernel or config instead of silently selecting one.
QEMU requires the GUI environment used by the existing GTK/VirGL test harness.

`audit` inventories source/toolchain/kernel, hashes staged Android images and
records the known manager grant. `build` compiles SELinux, userspace and rootfs
using existing build targets. `test` runs the 16 policy tests (including the
three known failures), file-context, package-integrity, manifest and OpenRC
checks, and new offline orchestrator tests. `image` will not build unless
those tests pass. `qemu` uses the existing snapshot-mode acceptance harness
under enforcing SELinux and checks immutable kernel/image hashes, Android
framework state, and an additional separate native-app smoke test only if
Android is ready. `report` reads a previous run directory.

**Evidence:** `out/waydroid-integration/<UTC-date>-<PID>/` contains
`result.json`, `report.md`, `logs/` and QEMU JSON/serial evidence when run.
Every gate is marked PASS, FAIL, BLOCKED or UNVERIFIED. A successful full
integration report cannot be produced while Android shortcuts remain
unverified.

**Do not set `openwave.selinux=permissive` or patch tests to get a green result.**

## Static validation status

GitHub Actions job (source-only, no Android or target toolchain):
`waydroid-integration-static.yml` validates Python syntax, Bash syntax,
unit tests for fail-closed gating/image overwrite protection and CLI parsing.

No host QEMU was available to this authoring environment. Thus:
- Full target policy compilation: UNVERIFIED in this environment; Stage 1
  targeted tests were separately verified on the user's OpenWave build host.
- Full security suite: **13/16** previously observed, with the same three
  pre-existing `sys_admin` test failures.
- Actual inode labels, mount helper, LXC runtime, Android boot and
  appd/Ocean shortcut integration: UNVERIFIED.
- Removed diagnostic permissions: **none**.
