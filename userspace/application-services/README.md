# Application Services Framework

Link `OpenWave::ApplicationServices` and include
`<OpenWave/ApplicationServices.h>` to use the existing application, permission,
file chooser and prompt clients together. Individual SDK components remain
available. No daemon is merged or replaced.

```cmake
find_package(OpenWaveSDK REQUIRED COMPONENTS ApplicationServices)
target_link_libraries(myapp PRIVATE OpenWave::ApplicationServices)
```

```cpp
OpenWave::PermissionClient permissions;
permissions.requestPermissionAsync(QStringLiteral("files.read"),
    [](OpenWave::PermissionClient::Status state, const QString &error) {
        // The decision is advisory. Use OpenWavePortals for actual file access.
    });
```

Callbacks run on the client's Qt thread. Transport work runs on its worker
context; application requests never supply an identity. QML uses the existing
PermissionClient model and signals. Administrative methods still require the
supervised Settings leader and cannot be authorized by an application-supplied
identifier.

## Lifecycle coordination

appd exposes the private `org.openwave.ApplicationServices1` interface on
`/org/openwave/Applications`. Only the supervised permissiond PID, UID and process
start time may subscribe. Bus policy additionally limits access to root.
Subscribe takes a UUID and atomically returns the current event sequence, user,
session, verified installation metadata and live instances. Subsequent unicast
LifecycleEvent messages carry that UUID, increasing sequence numbers, and one of:
ApplicationRegistered, ApplicationLaunched, ApplicationTerminated or
ApplicationRemoved. They are never broadcast to applications.

permissiond authenticates appd's unique bus owner using the supervisor identity
record. Its in-memory snapshot is derived from appd, not a second persistent
identity database. Duplicate snapshot-era events are ignored; gaps or appd loss
cause a fail-closed restart and a new snapshot. appd never synchronously calls
permissiond, avoiding an identity-resolution dependency cycle.

Termination cancels pending requests and undelivered file leases for the exact
launch and expires launch-bound decisions. Removal or replacement of verified
installation metadata removes its policies. Startup reconciles missing apps,
manifest digests and expired instances, including changes while permissiond was
stopped. Legacy records acquire a manifest digest without losing existing grants.

A supervised file broker can use QueryFor with the application's unique bus
connection and a UUID correlation; the same UUID accompanies the effective state.
AuthorizeFileChooser continues to issue opaque, single-use correlated leases.
Neither operation treats a caller-supplied APPID as identity.

## Capabilities, prompts and storage

PermissionRegistry registers labels, descriptions and eligible durations
independently of resource availability. Camera, microphone, location, photos and
notifications have definitions but remain restricted: their brokers are not
implemented. Trusted daemon code can add definitions without pretending a
provider exists. Only the existing file broker identifiers can currently grant
access; enabling a future provider also requires its enforcement integration.

promptd owns permission dialogs. Choices are Allow once, Allow while using the
application, Always allow where eligible, and Deny. While using the application
is scoped to the current appd launch, including background operation; it is not
a foreground-visibility guarantee. Existing session policies remain readable.
Concurrent requests within one launch share a prompt. Request UUIDs, lifecycle
events and decisions are logged without file paths, input contents or secrets.

Policies remain in `/var/lib/openwave/permissions/<uid>/policies.json`, with
protected ownership, version validation, atomic replacement and fsync. Optional
instance and applicationDigest fields extend the existing schema. Decisions are
serialized on permissiond's GLib main context. Corrupt or unsafe stores prevent
service startup; protected broker requests then fail closed.

Settings -> Apps -> Permissions shows verified declarations and observed
requests. Unsupported capabilities are visible but cannot be granted. Settings
uses authenticated administrative IPC and never modifies the store directly.

## Enforcement

appd retains private application UIDs, SELinux execution labels, Landlock,
Bubblewrap mount/IPC/UTS namespaces and cgroup lifecycle management. Regular
applications additionally inherit a seccomp BPF filter before exec. It rejects
foreign syscall ABIs, namespace creation/joining, mounting, process-memory access,
ptrace, module loading and privileged kernel interfaces. clone3 returns ENOSYS so
libc can fall back to filtered clone; ordinary processes and threads remain
supported. This is a baseline deny filter, not a complete syscall allowlist.

Each regular application's cgroup limits tasks, including threads, to
`[Isolation] MaxProcesses` in appd.conf (default 512). Trusted system components
retain their existing launch path. No AppArmor dependency is introduced.

Permission grants never relax SELinux or Landlock or mount user folders into an
app. File access uses brokered descriptors. Revocation invalidates leases and
terminates regular app cgroups to close already-delivered descriptors; bytes
already copied by an app cannot be recalled. PID/network namespaces remain shared
for existing identity, compositor, IPC and browser behavior.

## Validation

`./build-openwave test-permissions out/application-services-qa.img` checks the
policy store, registry, private IPC authorization, declarations, prompt choices,
concurrent requests, launch lifetime, promptd crashes, permissiond unavailability,
uninstall cleanup, daemon recovery, revocation and real
Browser downloads through the broker. The isolated fixture verifies seccomp is
active and checks the filtered clone3 result. Existing actual file-access denial
and revocation probes remain in place.

`./build-openwave test-appd out/application-lifecycle-qa.img` covers discovery,
launch, namespace separation, stable PIDs, activation, process cleanup and appd
recovery. Settings QML tests cover permission navigation and declared unavailable
capabilities.
