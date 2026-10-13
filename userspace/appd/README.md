# appd — application management

PID 1 starts `/usr/libexec/appd` before Ocean and supervises it. The daemon uses
C, GLib/GIO and `OpenWave::Xml`; it has no Qt dependency. Applications are its
children. Ocean retains the compositor, focus, dock, drawer, recent cards,
loading animations and previews, using a client controller instead of QProcess.

## Registry and metadata

The single registry comes from `/usr/share/applications/*.desktop`, monitored for
changes. Entries require `Type=Application`, a localized `Name`,
`X-OpenWave-AppId`, and an absolute `Exec` under `/usr/bin` or `/usr/libexec`.
IDs are at most 128 ASCII letters/digits/dots/underscores/hyphens, starting with
a letter or digit. Hidden/terminal entries and unsupported Exec field codes are
excluded. Root must own resolved executables/metadata and every ancestor; none
may be writable by group or others. Metadata is bounded to 64 KiB. Duplicate IDs
are logged; installers must ensure uniqueness.

`Icon` is an absolute trusted image or a name in `/usr/share/pixmaps`. Missing
icons do not prevent launch. Ocean obtains names/icons from the registry and
places new applications in the unpinned drawer, preserving saved favorite order.
`X-OpenWave-SingleInstance` defaults to true. `X-OpenWave-AcceptsPaths=true` allows
up to 16 absolute file-path arguments. Whole `%f`, `%F`, `%u`, `%U` tokens also
enable path arguments; they are removed from fixed Exec arguments. URI dispatch
and other field codes are unsupported. Execution parses argv without a shell.
Files receives one path and still enforces user-root confinement. Running
entries retain launch metadata until exit, when definitions are rescanned.

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
`state`, `error`, `singleInstance`. States are `Stopped`, `Starting`, `Running`,
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
root bus credentials, supervised PID file and `/usr/libexec/appd` executable.

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
descriptors are closed, umask is 0077, supplementary groups are cleared, the
configured UID/GID is applied, and `PR_SET_NO_NEW_PRIVS` is enabled.

Shutdown requests graceful Qt quit, falls back to SIGTERM when unavailable, and
forces SIGKILL after `ShutdownTimeoutMs` (2500 by default, bounded 100–30000).
Normal exit and crashes clean remaining groups, sockets and PID-named previews.
appd is a child subreaper. A GLib-integrated signalfd reaper monitors leaders and
adopted helpers together, avoiding competing waitpid owners.

PID 1 mounts cgroup v2. Each launch enters
`/sys/fs/cgroup/openwave-apps/app-<uuid>` before exec. `cgroup.kill` also removes
helpers that change process groups. On restart appd cleans leftover cgroups.
Without cgroup v2, cleanup falls back to process groups and cannot guarantee
cleanup of helpers escaping their group. `/run/openwave/appd` records PID,
executable and start time for verified fallback recovery. Leaders also receive a
parent-death signal.

Ocean registration verifies bus UID/PID, `/run/openwave/ocean.pid` and its
executable. Launch remains disabled until Ocean announces readiness and its
owned Wayland socket exists. If Ocean disappears, appd disables launch, drops
handoffs and shuts down app groups: old Wayland connections cannot be adopted by
a replacement compositor. Ocean then re-registers/reconciles state and launches
fresh instances, avoiding invisible orphaned processes or duplicate ownership.

## Security limitations

The installed config explicitly selects `User=root` for compatibility. appd
requires a configured user, logs root mode, supports UID/GID transitions and
never silently changes existing data ownership. This prepares non-root execution;
it does **not** sandbox applications.

Changing the user alone is insufficient. Blockers include Ocean's root session,
private runtime/PID-file permissions, promptd's renderer/session credential
checks, root-only connectivity/time/audio mutation policies, and existing
root-owned app/preferences data and the current image creator's all-root
ownership policy. A session migration must deliberately update
these policies and ownership. Non-root app directories must already be
provisioned to the configured UID with private permissions. Apps sharing a UID
are not isolated. appd stays privileged for identity transitions, signaling and
cgroup management; capability reduction and sandbox policy are future work.

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
