# OpenRC and SELinux boot recovery

This records the investigation of the 2026-11-04 x86_64 image. Runtime
validation is in progress; the final validation section must be consulted
before treating a candidate as ready.

## Root causes

* The policy in `out/OpenWave-openrc-20261104-172621-selinux.img` has SHA-256
  `0b69efca53e047f0e0a7a1fec0bd53e23990e53b1e1a73372ddce18f9414f85b`.
  Its system-to-runtime file execute permission is absent. The current source
  compiles that permission correctly: the previously tested image was stale.
* Removing the generated system-domain/all-types/all-permissions rule exposed
  missing explicit PID 1, OpenRC, and native service permissions. Restoring
  that blanket grant would also privilege application-facing services.
* The ordinary userspace CMake install selected the legacy initializer while
  the staged image used the small OpenRC bootstrap. They now select the same
  bootstrap.
* Normal image creation did not include offline labeling or source/policy
  freshness checks. Blanket fakeroot root ownership also lost explicit
  service ownership.
* The runtime staging tree lacked mount/umount and their target libraries,
  and the persistent layout lacked `/var/log` and canonical `/var/run`,
  `/var/lock`, and `/etc/mtab` links.
* The kernel configuration disabled Unix-domain sockets. D-Bus reported
  `Address family not supported by protocol`; udev also could not create its
  control socket. System V IPC was disabled, preventing IPC namespaces needed
  by the application sandbox. These defects are independent of SELinux.
* The target loader searches `/lib64` and `/usr/lib64`; some assembled libraries
  (notably zlib) were only in `/usr/lib`. D-Bus readiness failed because gdbus
  could not load zlib. Safe aliases now preserve the installed layout while
  exposing libraries in the default search directory. The actual target loader
  checks core executables in a guest-root user namespace without a library-path
  override or host cache.
* The service registrar wrote PID/UID/start-time records, but the reader expected
  two fields. It also treated Ocean's UID 1000 identity as a root service. The
  reader now checks the complete record and explicit expected UID; appd, portal,
  and promptd authenticate Ocean's executable and start time under its account.
* udev's dependency on full runtime provisioning formed a cycle through the
  stock filesystem-check services and virtual `dev` provider. It could hang
  shutdown even though the custom-script-only validator reported an acyclic
  graph. udev now provisions just its early runtime directories, and the
  validator includes installed stock scripts and virtual providers.

## Implemented boot sequence

The kernel starts `/init` from `userspace/openrc/bootstrap.c`. It mounts the
basic virtual filesystems and calls `openwave_selinux_boot()`. Policy is loaded
in the explicit bootstrap permissive window. For images carrying the build
manifest, critical persistent labels are checked read-only, and only dynamic
`/dev`, `/run`, `/tmp`, and `/dev/shm` are relabeled. Legacy images without the
manifest retain the full-root relabel fallback. Enforcing is selected and
read back before the bootstrap execs `/sbin/openrc-init`, preserving PID 1.
The OpenRC runlevels then start the dependency-ordered services.

Enforcing failures remain fatal. Diagnostic modes require an explicit
`openwave.selinux=permissive` or `openwave.selinux=disabled` kernel argument;
there is no automatic unconfined fallback. The minimal bootstrap remains
necessary to initialize policy before OpenRC starts sensitive services.

`openwave.audit=verbose` disables printk rate limiting before policy load.
For investigation also use `ignore_loglevel`, because the OpenRC dmesg service
otherwise lowers the console level and hides later AVCs. Do not equate a
service's initial `[ok]` with a live process or successful readiness check.

OpenRC's protected service scripts construct the service environment, including
the fixed session-bus address. The system-to-native-service transition preserves
that environment explicitly; appd-to-application transitions retain secureexec.
Native service state has separate labels from executable runtime and from other
brokers' private state. NetworkManager, iwd, BlueZ, Avahi, PipeWire, and WirePlumber
now also have dedicated domains rather than inheriting the supervisor domain.

## Repeatable image build

Start with the assembled x86_64 target runtime and SDK. Compilation uses
`toolchain/sysroot/x86_64-unknown-openwave`, never `rootfs`.

```bash
ROOT=/mnt/aosp/ThunderOS
cd "$ROOT"
OPENWAVE_BUILD_JOBS=8 ./build-openwave kernel
IMAGE="$ROOT/out/OpenWave-openrc-$(date +%Y%m%d-%H%M%S).img"
./build-openwave image "$ROOT/rootfs" "$IMAGE"
```

The image command refuses an existing output. It validates OpenRC runtime
prerequisites and guest runlevel links, rebuilds bootstrap/runtime checker and
policy, checks compiled permissions, and stages a manifest containing source
and output hashes. Native policy tools are rebuilt separately using the pinned
libsepol 3.11 sources; they never enter the target runtime.

The image is made with fakeroot and the account-aware ownership model. Builder
UID/GID become root, explicit other owners are preserved, and declared user
homes receive their account UID/GID. All staged inodes (including symlinks)
and generated `lost+found` are labeled using debugfs. Label values are read
back and compared, policy/context bytes are compared with staging, and
`e2fsck -fn` must pass. The output is published only after completion. No image
mount is required, and existing images are preserved. Do not run the former
manual second labeling step after this command.

A missing runtime prerequisite or failed build stops before packing. The
OpenRC stage is included by the rootfs assembler; the image command validates
rather than downloading/rebuilding the entire SDK dependency stack.

## Boot evidence

```bash
python3 scripts/test-openwave-boot.py --image "$IMAGE" \
  --output "$ROOT/out/boot-recovery/$(date +%Y%m%d-%H%M%S)-enforcing"
```

This uses raw QEMU, a local GTK/OpenGL window, the regular kernel under
`out/linux`, and `-snapshot`. It records exact arguments, kernel/image hashes,
serial and host logs, ext4 checks, and actual shutdown outcome. ACPI powerdown
is attempted before discarding the snapshot on timeout. A successful QMP
response alone is not a completed guest shutdown. `--mode permissive` must be
selected explicitly for a diagnostic run. No VNC is used.

The opt-in `openwave.qa=boot` collector prints PID 1's identity, enforcement,
OpenRC status, native service process identities, and service log tails to
serial. `/run/openwave` and `/run/user/1000/openwave-logs` are tmpfs: the host
serial evidence is retained even when the snapshot is discarded.

## Host regression checks

```bash
python3 scripts/tests/test_boot_image_pipeline.py
python3 userspace/security/selinux/tests/test_policy.py
python3 userspace/openrc/tests/test_bootstrap.py
OPENWAVE_OPENRC_ROOT="$PWD/rootfs" python3 userspace/openrc/validate_openrc.py
python3 scripts/tests/test_qemu_runner.py
python3 scripts/tests/test_runtime_loader.py
python3 userspace/openrc/tests/test_service_readiness.py
```

The image tests use actual small ext4 filesystems, including guest-absolute
symlinks, stale packed policy rejection, and preservation of numeric owners.
Policy tests compile deliberately broken policies to prove the inspector
rejects missing loader execution and attribute-based runtime writes.

## Runtime validation status

The rebuilt kernel enables Unix sockets, System V IPC, IPC namespaces, and RTC
support. `out/boot-recovery/native-runtime-enforcing/result.json` records an
enforcing boot through OpenRC's default runlevel with live appd, portal, time,
connectivity, D-Bus, and udev processes. It also records actual ACPI shutdown:
the guest printed `reboot: Power down`, QEMU exited with status zero, and both
input hashes remained unchanged. The tested image was
`out/boot-recovery/native-runtime.img`, SHA-256
`c8e54aa5d1a5481e1fe2d4547998ed98cafc0bb67aec7f13f9381739a1848958`.

That image is intermediate evidence. It predates the subsequent identity,
account-storage, renderer-policy, and Qt repairs. Its permission daemon and
XDG portal required Qt 6.11, its wallpaper daemon referenced a removed symbol,
and Ocean did not start. These findings are retained in its serial log.
Ocean and a normal confined application launch still require a new image and
runtime validation after the matching Qt 6.10.3 builds finish. Wi-Fi, Bluetooth,
and audio backend operation are also unvalidated; their dedicated domains
expose additional permission and kernel feature gaps.

The account allocator now mutates only
`/var/lib/openwave-accounts/passwd`; `/etc/passwd` is a protected relative link
to that file. Image preparation preserves account contents and rejects
conflicting databases. Namespace and Landlock setup expose the backing read
only to applications. Supervisor identity records are readable public proofs,
but remain root-owned and carry `openwave_service_run_t`; consumers validate
that label together with the UID, PID, and process start time. Public broker
preferences use a separate label and cannot impersonate supervisor records.

## Matching Qt and browser bindings

The mixed installation also contained Qt 6.11 applications, Web bindings, and
Wayland libraries alongside the Qt 6.10.3 base runtime. Rebuild the Wayland SDK
with `OPENWAVE_BUILD_JOBS=8 scripts/build-qtwayland-openwave.sh all`. This builds
native scanners separately, enables the QtBase Wayland client, and installs a
matching compositor into the target sysroot. The helper refreshes Qt's generated
Gui AUTOMOC cache after changing the Wayland feature configuration.

`OPENWAVE_BUILD_JOBS=2 scripts/build-wpe-qt-adapter.sh` rebuilds the bundled WPE
Qt adapter against that exported target Qt. It requires an existing target WPE
development installation. The engine itself remains the existing target WPE
2.54.0 binary with SONAME `libWPEWebKit-2.0.so.1` (SHA-256
`990f9cc80490a3f17e0d89660b9f6a90e1eb7e45790071610256bf289c6e8ebb`).
The recovery exported that binary to the SDK and generated public headers from
the matching bundled source; no compilation used rootfs as a sysroot. Its Qt
adapter links with `-z,defs` and uses public WPE APIs and inline WTF helpers.
The recovery export records are in
`out/boot-recovery/wpe-adapter-build/runtime-export-hashes.json`.

OpenWave SDK's Web component can now be installed separately with
`cmake --install out/sdk-x86_64-unknown-openwave --component Web` after building
`openwave-webplugin`. This preserves existing UI SDK exports during repair.
The current checkout lacks the sources for the already exported
OpenWave.Application, OpenWave.Controls, and OpenWave.Theme APIs; their existing
Qt 6.10 SDK binaries are reused. A full SDK reinstall would omit their current
exports, so source recovery remains necessary before claiming a completely
fresh SDK build. This limitation does not bypass policy or image validation.

The custom Controls Gallery source is under `userspace/controls-gallery`.
Its Main.qml was recovered exactly from the old executable's embedded resource,
retaining the custom screen rather than substituting the upstream Qt example.
`userspace/appd/tests/test_environment.py` exercises the production child
environment builder and verifies configured trusted-session sockets, published
confined-app sockets, private storage paths, and removal of inherited preload
variables.

The optional boot validator flag `--app-launch` adds `openwave.qa=app-launch`.
The guest asks appd to launch Controls Gallery through Ocean, and reports a
pass only after finding a live isolated UID/domain with no_new_privs, seccomp,
zero effective capabilities, separate mount/IPC/UTS namespaces, and a bounded
pids cgroup. The validator exits unsuccessfully if that requested test does not
report a pass. Merely accepting the launch request is insufficient.
