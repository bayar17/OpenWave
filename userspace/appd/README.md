# appd — application management

PID 1 supervises `/System/Services/appd`. C/GLib manages identity, lifecycle,
processes and IPC; a small C++/Qt Core reader validates JSON bundle manifests.
Ocean owns the compositor, dock, drawer, recent cards, and visual handoff.

## Registry and metadata

`/System/Applications` and `/Applications` contain the authoritative manifest
registry. Full application IDs replace the previous short IDs. Desktop entries
call `/System/Services/openwave-launch` with an ID and are not launch metadata.
See [the native bundle contract](../../docs/APPLICATION-BUNDLES.md) for validation,
installation, migration, Qt paths, and enforcement status.

## D-Bus and public SDK

System service: `org.openwave.Applications`; object:
`/org/openwave/Applications`; interface: `org.openwave.Applications1`. The
canonical contract lives in `userspace/dbus/interfaces/org.openwave.Applications1.xml`.

| Method | Arguments | Result |
| --- | --- | --- |
| ListApplications | none | array of application dictionaries |
| GetApplicationInfo | app ID | dictionary |
| GetRunningApplications | none | array of instance dictionaries |
| Launch | app ID, string-array arguments | PID, or zero for an accepted visual handoff |
| Activate | app ID | request focus of an existing instance |
| Terminate | app ID | begin shutdown of all instances |
| RegisterShell | none | authenticate supervised Ocean |
| SetSessionReady | boolean | actual Wayland readiness |
| CommitLaunch | ticket | execute after loading transition; PID |
| CancelLaunch | ticket | discard handoff |

Dictionaries contain `appId`, `name`, `icon` (file URL), `executable`, `pid`,
`state`, `error`, `singleInstance`, `bundlePath`, `isolationProfile`, and
`namespaceSandboxed` (true for a running regular app after namespace setup). States are `Stopped`, `Starting`, `Running`,
`Stopping`, `Crashed`. Running means execution started; Ocean separately waits
for the first rendered frame.

Signals are `ApplicationStarted`, `ApplicationStopped`, `ApplicationCrashed`,
`ApplicationStateChanged`, `ActivationRequested`, `LaunchRequested`,
`OperationFailed`, `RegistryChanged`. Exit status is Linux's raw wait status.
The state/crash signal distinguishes unexpected exits from requested stops.

Qt applications link `OpenWave::Applications` and include
`<OpenWave/ApplicationsClient.h>`. Async queries, launch/activate/terminate and
lifecycle signals run through a dedicated GLib context/thread, keeping RPC off
the UI thread. `call()` exposes compositor operations. The SDK verifies appd's
root bus credentials, supervised PID and the supervisor-recorded process start time.

The existing `OpenWaveApplications::launch()` now calls appd. For app-originated
launches, appd sends a targeted ticket to Ocean, which crossfades into the loading
screen and calls `CommitLaunch`. Ocean defers requests during a visual transition.
One handoff may be pending; tickets expire after 30 seconds. Canceling discards
the ticket. The old launcher socket address remains source-compatible in the SDK
but Ocean no longer serves a competing registry there.

PID-specific `ApplicationChannel` sockets keep their activation/quit JSON
protocol. appd verifies both peer UID and PID and bounds replies/timeouts.
Activation retries while an endpoint starts. Settings has no quit endpoint and
falls back to SIGTERM. Browser-to-Files delivery uses the existing Files handler.

## Execution, termination and restart

Each launch creates a session/process group. Its environment explicitly selects
Qt Wayland, `/run/ocean`, the existing buses and application data convention
`/Data/Users/<user>/.DataLocation/Applications/<appId>`. Only locale settings are
inherited; EGLFS, TZ, LD_PRELOAD and arbitrary variables are excluded. Inherited
descriptors are closed and umask is 0077. Trusted components retain the session
UID/GID; regular apps receive a private UID/GID and only the render group.
`PR_SET_NO_NEW_PRIVS` is enabled.

Regular applications use their persistent application directory as `HOME` and
its `Data`, `Config`, and `Cache` children as the XDG data, configuration, and
cache directories. appd creates standard folder links such as `Pictures` and
`Downloads` in that private home, pointing to the current session user's real
folders beneath `/Data/Users/<user>`. `Music` and `Musics` both point to OpenWave's
existing `Musics` folder. `Config/user-dirs.dirs` makes those aliases available
through Qt's standard paths. Existing app files, directories, and user overrides
are preserved. Link provisioning never follows app-controlled directory symlinks.
The aliases do not change Landlock, UID, or SELinux permissions: apps continue
to use the File Chooser broker for protected user files. Private application
storage persists; only each launch's runtime directory is removed on exit.

Regular apps use the `bubblewrap-landlock-uid-cgroup` isolation profile. appd
uses the root-owned, non-setuid `/usr/bin/bwrap` to construct separate mount,
IPC and UTS namespaces. A minimal root-only namespace keeper reports readiness;
appd pins the namespace and root descriptors, then terminates that setup process.
The actual application remains a direct appd child. Before execution it selects
its SELinux application domain, joins the prepared namespaces, changes root,
closes the setup descriptors, drops to its private UID/render group, and applies
the existing Landlock policy and no-new-privileges setting. Neither Bubblewrap
nor the namespace keeper remains as an application supervisor.

The filesystem contains read-only runtime libraries, configuration and the
app's own bundle. Its private persistent data and launch runtime are writable;
`/tmp` and `/dev/shm` are private to that launch. Only the required device nodes,
service sockets and public supervision records are exposed. Other app profiles,
private service state and user folders remain outside the mounted view. File
Chooser access continues through brokered descriptors. PID and network namespaces
are shared to preserve trusted process credentials, Wayland identity, lifecycle
endpoints and browser networking. Trusted Settings and Files retain their existing
launch path. Missing Bubblewrap or failed namespace construction rejects launch;
there is no fallback to running an ordinary app without its namespace sandbox.

Shutdown requests graceful Qt quit, falls back to SIGTERM when unavailable, and
forces SIGKILL after `ShutdownTimeoutMs` (2500 by default, bounded 100–30000).
Normal exit and crashes clean remaining groups, sockets and PID-named previews.
appd is a child subreaper. A GLib-integrated signalfd reaper monitors leaders and
adopted helpers together, avoiding competing waitpid owners.

PID 1 mounts cgroup v2. Each launch enters
`/sys/fs/cgroup/openwave-apps/app-<uuid>` before exec. `cgroup.kill` also removes
helpers that change process groups. On restart appd cleans leftover cgroups.
Without cgroup v2, only trusted components may launch; regular apps fail closed. `/run/openwave/appd` records PID,
executable and start time for verified fallback recovery. Leaders also receive a
parent-death signal.

Ocean registration verifies bus UID/PID, `/run/openwave/ocean.pid` and its
executable. Launch remains disabled until Ocean announces readiness and its
owned Wayland socket exists. If Ocean disappears, appd disables launch, drops
handoffs and shuts down app groups: old Wayland connections cannot be adopted by
a replacement compositor. Ocean then re-registers/reconciles state and launches
fresh instances, avoiding invisible orphaned processes or duplicate ownership.

## Trust boundary

The configured session user remains root for the compositor and trusted Settings
and Files components. This does not grant that UID to regular applications.
Regular apps receive the isolation described below. Trusted system components,
the kernel, compositor, and privileged service implementations remain part of
the system trust boundary. File permissions are enforced through the file broker;
other permission categories are not yet advertised as mediated services.

The daemon can track multiple instances if metadata disables single-instance
mode, but Ocean currently represents one primary surface/recent card per app ID.
All shipped apps use single-instance mode; a complete multi-window shell model
is outside this migration.

## Changed files and validation

- `userspace/appd/{main,registry,process,dbus}.c`, `appd.h`, CMake and configuration.
- Canonical Applications1 XML and system-bus policy.
- `userspace/init.c`, `userspace/CMakeLists.txt`: boot and build integration.
- `userspace/application-client/ApplicationsClient.*`, `ApplicationChannel.*`,
  `userspace/sdk/CMakeLists.txt`: SDK transport and compatible launch API.
- `OceanLauncher/OceanLauncher/AppController.*`, `Main.qml`: registry, lifecycle,
  surface PID association and visual handoff, without application process ownership.
- Files/Browser desktop metadata; `scripts/assemble-openwave-rootfs.py` requires
  appd and normalizes administrative trust-root permissions, preserving user data.

`tests/` contains the target QEMU harness. It uses the real init, daemon, SDK,
compositor and system apps to exercise discovery, authentication, ownership,
reuse, activation, Browser-to-Files handoff, graceful/forced shutdown, escaped
helper cleanup, crash notifications, Ocean restart and appd restart. It also
runs the existing instance-channel and favorite-layout tests. QA binaries and
QtTest are confined to the test image. See the final report in
`out/appd-validation` for test results; build success alone is not runtime proof.

Run the reproducible suite with a new image path:

```bash
./build-openwave test-appd out/OpenWave-appd-test.img
```

The helper refuses existing images and keeps QA binaries out of rootfs.

## Application isolation and permission identity

Settings and Files remain trusted system components, pinned by installed ID and
executable. Other applications now use distinct persistent unprivileged UIDs,
private runtime/TMPDIR storage, no-new-privileges, and inherited Landlock rules.
Application data keeps its established on-device path. Kernel Landlock ABI 6 and
cgroup v2 are mandatory; launch fails closed when either is unavailable.

`GetOwnIdentity` reports the calling managed application's identity. The
privileged `ResolveCaller`/`ResolveProcess` operations are limited to supervised
system consumers and resolve kernel credentials against live launch cgroups and
start times. Executing Settings from another app does not change its identity.
`RevokeAccess` is permissiond-only and kills all instances/helpers in the affected
regular application's cgroups. See [permissiond](../permissiond/README.md).

Application Services coordination uses the private, supervised-permissiond-only
`org.openwave.ApplicationServices1` interface. Its snapshot and sequenced unicast
events include verified declarations, manifest digests and live launch identities.
Regular apps also receive a seccomp syscall filter and a cgroup task limit
(default 512, including threads). See
[Application Services](../application-services/README.md) for the security and
compatibility details.
