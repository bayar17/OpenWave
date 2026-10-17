# Native application bundles

OpenWave installs graphical applications as bundles. `appd` is the sole application
registry and launch authority; desktop files are integration entries, not a second
registry. Custom PID 1 still bootstraps the compositor and supervises services.

| Component | Application ID | Executable |
| --- | --- | --- |
| Settings | `com.openwave.settings` | `/System/Applications/com.openwave.settings/bin/ocean-settings` |
| Ocean Browser | `com.openwave.ocean` | `/System/Applications/com.openwave.ocean/bin/ocean` |
| Files | `com.openwave.files` | `/System/Applications/com.openwave.files/bin/ocean-files` |
| Shell/compositor | `com.openwave.shell` | `/System/Applications/com.openwave.shell/bin/ocean-launcher` |

OpenWave daemons and the small `openwave-launch APPLICATION_ID [PATH ...]` client
live in `/System/Services/`. Other system tools and third-party dependencies keep
their existing `/usr` locations. No old graphical executable aliases are installed.

## Bundle contract, version 1

```json
{
  "schemaVersion": 1,
  "id": "org.example.viewer",
  "name": "Viewer",
  "version": "1.0.0",
  "type": "application",
  "executable": "bin/viewer",
  "runtime": "openwave-qt",
  "icon": "resources/icon.svg",
  "sandbox": {"enabled": true, "profile": "standard"},
  "permissions": [],
  "singleInstance": true,
  "acceptsPaths": false
}
```

The bundle has `bin`, `lib`, `plugins`, `qml`, and `resources` directories.
Native C/C++ programs can use the same runtime contract without linking Qt.
Application libraries belong in `lib`; the executable RUNPATH searches
`$ORIGIN/../lib`, `/usr/lib64`, and `/usr/lib`. Libraries that need other private
libraries must also use `$ORIGIN` in their own RUNPATH. Shared Qt, WPE, system
libraries, fonts, plugins, and QML modules remain centrally installed under `/usr`.
`appd` adds the bundle's plugins and QML directories to Qt's search environment.
The SDK's `openwaveApplicationBundlePath()` resolves the bundle relative to the
running executable. Resources must not depend on the build tree or `/usr/bin`.

Optional `arguments` is an array of at most 16 fixed executable arguments, each
bounded to 4096 characters. These are passed to the unprivileged application,
never interpreted as shell commands or sandbox configuration. `acceptsPaths`
permits callers to append up to 16 absolute file paths. Unknown descriptive fields
can be added without changing version 1. Unsupported schema versions, runtimes,
profiles, and explicitly required unimplemented security mechanisms are rejected.

`appd` discovers `/System/Applications/<id>/manifest.json` and
`/Applications/<id>/manifest.json`, monitors installations, and polls for nested
manifest updates. IDs must match their directory and have at least three
reverse-DNS segments. `com.openwave.*` is reserved to `/System/Applications`.
Duplicate installed IDs are rejected in both locations. A running instance keeps
its launch metadata until it exits; executable/manifest changes are revalidated
before another launch. Single-instance activation, compositor handoff, cgroup
cleanup, and daemon recovery keep the existing lifecycle implementation.

Files and every ancestor of executable/manifest/icon paths must be root-owned and
not writable by group or others. These paths must not traverse symlinks; manifest,
icon, and executable hard links are rejected. Executables must be relative paths
beneath `bin/`, icons must stay in the bundle, and manifests are limited to 64 KiB.
The `type` and `permissions` fields never grant privileges. There is no package
signature verifier or installer yet: root-controlled installation is the current
integrity boundary, not proof of publisher identity.

## Building and installing

Source `env.sh --build`; compile against the exported target SDK, never rootfs.
The exported `lib/cmake/OpenWaveSDK/OpenWaveBundle.cmake` provides:

```cmake
include("${OpenWaveSDK_DIR}/OpenWaveBundle.cmake")
openwave_install_bundle(my_viewer org.example.viewer)
install(FILES resources/icon.svg DESTINATION /Applications/org.example.viewer/resources)
```

`manifest.json` belongs beside the application's CMakeLists.txt. Use `SYSTEM` only
for first-party repository targets. Installation still goes through `DESTDIR`
staging. `./build-openwave userspace` builds the SDK and runtime, installs staging,
then assembles rootfs and checks ELF dependencies. The assembler drops obsolete
OpenWave graphical/service paths even if an old staging tree still contains them.
It retains unrelated `/usr/bin` programs and persistent user state.

Create a new image with `./build-openwave image rootfs out/OpenWave-bundles.img`.
Existing images are never overwritten. The runtime tree contains no SDK headers,
compiler, static development libraries, or host tools.

## Data migration

Private profiles use
`/Data/Users/<session-user>/.DataLocation/Applications/<id>/{Config,Cache,Data}`.
The SDK exposes config/cache/storage resolvers while retaining the existing
application-root resolver. `appd` sets the matching XDG environment for isolated
applications and creates private directories before dropping privileges.

For the three built-in apps, old IDs `browser`, `files`, and `settings` migrate to
their full IDs before launch. Existing isolated UIDs are reused. Dock preferences,
protected permission decisions/request timestamps, and remembered portal-directory
tokens migrate as well. These aliases are migration inputs, not launch aliases.

Browser settings move to `Config/Settings.json`; history moves intact, including
SQLite sidecars, to `Data/Browser/History.sqlite`; downloads to `Data/Downloads`;
sessions/bookmarks/password storage and the engine website/cookie profile to
`Data`; disposable caches and favicon references stay under `Cache`. The existing
WPE plugin appends `cache/HTTP` to its cache base, so its cache now lives at
`Cache/cache/HTTP`; media and shader caches use `Cache/HTTP` and `Cache/GPUCache`.
No engine rebuild is needed for this compatibility detail. Cache deletion does
not delete durable records. System-wide Settings preferences retain their
existing protected location, because they configure the OS rather than a private
browser profile.

Migration uses same-filesystem non-overwriting renames. Normal launches reuse
existing ownership without traversing the entire private profile. Nonconflicting directory
contents can merge, and a restart resumes a partially completed migration.
Conflicting files or unsafe links stop launch and preserve both copies for manual
resolution. Conflicting old/new permission rows stop policy loading without
rewriting the database. Do not delete an old profile merely because migration
reported a conflict. Back up data before downgrading: older releases do not know
the new IDs or paths. Copying to a fresh image does not import data from an older
image automatically; migrate the user's persistent `/Data` and protected `/var`
state offline through the existing deployment workflow.

## Implemented and future enforcement

| Mechanism | Current state |
| --- | --- |
| Authenticated appd identity, supervised service credentials | Enforced |
| Root-controlled immutable installation metadata | Enforced |
| Unique regular-app UIDs and private writable data/runtime | Enforced |
| Landlock ABI 6 filesystem allowlist, no-new-privileges | Enforced for regular apps |
| cgroups v2 lifecycle cleanup, kill on broker revocation | Enforced |
| Permission-aware file descriptors and directory tokens | Enforced by existing portal/permissiond |
| Restricted public Wayland/session/audio socket aliases | Existing integration retained |
| Bubblewrap, mount/user/PID/IPC namespaces and `/app` mount | Planned, not implemented |
| SELinux app confinement and service/file labels | Enforced on configured builds; services retain broad trusted access |
| App-specific seccomp syscall filtering | Planned, not implemented |
| Package signatures and trusted installation/update service | Planned, not implemented |

`standard` names the existing UID/Landlock/cgroup profile, not a namespace sandbox.
`GetApplicationInfo` reports `namespaceSandboxed=false` and the actual isolation
profile. `sandbox.requires` may list `uid`, `landlock`, `cgroups-v2`, and `selinux`;
SELinux requirements need enforcing mode. Requesting Bubblewrap or seccomp fails registration. Explicit isolation
requirements are also rejected for trusted components that would bypass that
profile. There is no silent
fallback from a failed enforced profile.

Settings and Files retain their existing explicitly pinned trusted status for
this functional migration. Shell and runtime daemons are trusted system
components. Removing Files' direct filesystem authority requires a separate
broker migration; `type=system` does not make another application trusted.

The manifest's validated host bundle root and executable plus the per-instance
storage/runtime/cgroup form the inputs for a future sandbox launch adapter. It
must bind the bundle read-only at `/app`, writable private data separately, mount
only the central runtime and selected devices/sockets, and execute
`/app/<validated executable>`. Use a fixed mount/resource allowlist rather than
manifest-provided commands. Keep root private service sockets inaccessible and
continue obtaining files via the portal. Device, network, per-service SELinux, and seccomp
policies need separate enforcement tests before changing production profiles.

The target libselinux/libsepol libraries are built with `./build-openwave selinux`
and by `./build-openwave all`. Init loads the OpenWave policy before starting
services; appd assigns regular apps to an enforced domain. Per-service domains
retain broad trusted access. Per-app SELinux categories remain future work;
UID/Landlock separation still applies. See [SELinux configuration](SELINUX.md).
