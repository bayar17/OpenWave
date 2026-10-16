# permissiond

`permissiond` is the system-bus authority for brokered application permissions:
`org.openwave.Permissions`, `/org/openwave/Permissions`,
`org.openwave.Permissions1`. PID 1 supervises `/System/Services/permissiond`.
The canonical interface is in `userspace/dbus/interfaces` and ships with the SDK.

The implemented identifiers are `files.read`, `files.write`, and
`files.directory`. Unknown identifiers are restricted. Camera, microphone,
network, and other resources are not advertised as permission-controlled APIs.

## Identity and isolation

Applications cannot supply their identity or user ID to a permission request.
The daemon authenticates the supervised appd bus owner and resolves the unique
calling connection through appd. appd binds kernel UID/PID/start time to a live
launch and its root-owned cgroup. Helpers retain the identity of the application
that launched them, even if they execute another installed program.

Settings (`com.openwave.settings`,
`/System/Applications/com.openwave.settings/bin/ocean-settings`) and Files
(`com.openwave.files`, `/System/Applications/com.openwave.files/bin/ocean-files`) are trusted system components. The exact installed ID and
executable pair is required. They are part of the system trust boundary and
are labelled accordingly in Settings; their access is not represented as
revocable application policy. Administrative permission operations additionally
require the actual Settings leader process.

Other applications have persistent, distinct unprivileged UIDs, private runtime
and temporary directories, no new privileges, and an inherited Landlock domain.
Only their own application data and private runtime storage are writable,
with the device/shared-memory access needed by the graphics stack. The existing user/application data
paths remain unchanged. Existing root-owned application data is migrated without
following symlinks or accepting hard-linked files. Render-node group access is
provided for Qt/WPE; input and raw storage devices are not granted. Runtime
libraries and configuration remain readable. User documents and other
applications' private data cannot be opened through ambient filesystem paths,
even when ordinary file modes would otherwise allow a read.

Regular application launch fails closed without cgroup v2 or Landlock ABI 6.
Build the matching kernel with `./build-openwave kernel`. Its firmware source is
resolved through the exported target sysroot, preserving embedded Wi-Fi firmware.

Service clients verify UID 0, bus-owner PID, and PID start time against
root-owned supervisor records. PID files retain their original numeric format;
PID 1 writes separate `.pid.identity` records. The compositor runtime remains private (0700). appd publishes root-owned
socket aliases for Wayland and the session bus and refreshes them after service
restarts. Runtime directory traversal does not expose private service or
application state directories. A root-owned
read-only snapshot exposes only the public accessibility preferences to isolated
apps; private configuration remains inaccessible.

The PipeWire socket is also published for playback. The OpenWave PipeWire patch
uses immutable peer credentials to restrict non-root clients to their own
playback streams and read-only output devices. Capture devices, server-side
plugin factories, and changes to physical device parameters are inaccessible;
the session manager cannot override these limits. The audio alias requires the
patched library. Both audio build commands apply this patch automatically.
Audio playback is an ambient capability, not an additional permission identifier.

## Decisions and prompts

States are `granted`, `denied`, `not-determined`, and `restricted`.
An undecided request queues a native prompt through promptd's `RequestFor`.
Only the supervised permissiond process can invoke that operation. promptd
independently resolves the original system-bus connection using appd and presents
the owning application. Only the registered supervised Shell renderer can answer;
the response is private to permissiond's original prompt connection.

Concurrent requests for the same application and permission share a prompt.
Queues, leases, subscribers, input lengths, and policy counts are bounded.
Cancel, timeout, loss of the requesting process, unavailable services, and stale
identities cannot grant access. A cancelled prompt does not persist a denial.

The protected store is
`/var/lib/openwave/permissions/<session-user-uid>/policies.json` (directory 0700,
file 0600). Version 1 records include application/user identity, permission,
state, grant duration, session identity, creation/modification timestamps, and
expiry. Updates use atomic replacement and fsync. Invalid ownership, symlinks,
unsafe permissions, unsupported schemas, and damaged records fail closed.

Permanent decisions survive restarts. Session grants survive permissiond
restarts only within the same appd session. Restarting appd terminates its old
applications and starts a new session. One-time grants expire after two minutes
if unused and authorize one broker request, including one multi-file selection.
The one-time decision is consumed when its lease is issued, even if the picker
is subsequently cancelled. They are never restored from disk.

## File broker enforcement

The portal verifies appd identity before accepting requests. It obtains a
short-lived lease from permissiond before showing the picker. Only the supervised
portal can request, validate, or release leases. Each lease is tied to the
requester's unique connection, application, user, process start time, appd
session, and launch cgroup. The portal validates and consumes it again immediately
before opening selected resources and delivering descriptors. Restarting the
permission service invalidates undelivered results. Root-confined `openat2`
resolution rejects symlink traversal. Application-supplied paths never replace
the system renderer's selection.

`SelectDirectory` grants a directory descriptor for listing, not ambient access
to every descendant. The SDK also returns a remembered directory token, bound to
the application, user, directory device/inode, and verified selection.
`createFile(token, filename)` creates a new file through the broker and returns
a writable descriptor; an empty token targets the user's existing Downloads
folder. All writes require a fresh `files.write` authorization and lease.
Names cannot escape their directory and existing files are never overwritten.
The remembered token conveys a destination selection, not a permission decision.
Selections of the same directory reuse their token; retained destinations are
bounded per application and globally.

Ocean downloads stage bytes in their private cache and copy through writable
broker descriptors. The user-visible Downloads and chosen-folder paths remain
unchanged. Wallpaper images are supplied by the system wallpaper service, so
isolated apps can retain the shared background without reading private user
storage directly.

Settings -> Apps lists the complete appd registry, including trusted system apps.
Each app has a Permissions page showing verified declarations and permissions it actually requested.
Declarations are displayed separately from actual request timestamps. The first request timestamp is stored independently of the decision: denial,
cancellation, revocation, and one-time grant consumption retain that history.
Administrative grants and status checks do not count as application requests.
Unsupported identifiers remain restricted and appear without an Allow control;
their records are bounded per app. The optional `requestedAt` field extends the
version 1 store; older records without it acquire a timestamp on their next
request. A one-time decision is never restored, but its request metadata is.

General -> Permissions retains the existing complete policy view. Policy changes
require the authenticated Settings process. Denying,
restricting, or revoking access invalidates outstanding leases and immediately
stops regular application cgroups to close descriptors already delivered to them.
A durable pending-revocation marker is reconciled before a restarted daemon
accepts requests. Revocation cannot erase bytes that an application already
copied; applications must be reopened after an access-revoking policy change.

## SDK

Link `OpenWave::Permissions` and include `<OpenWave/PermissionClient.h>`.
`OpenWave::PermissionClient` exposes asynchronous request/check/status/list,
change subscription, and administrative methods, plus a `QAbstractListModel` for
Qt Quick. Results and errors arrive on the object's Qt thread; IPC runs on a
worker context. Applications do not need to implement D-Bus calls.

```cpp
find_package(OpenWaveSDK REQUIRED COMPONENTS Permissions)
target_link_libraries(myapp PRIVATE OpenWave::Permissions)
```

```cpp
OpenWave::PermissionClient permissions;
QObject::connect(&permissions, &OpenWave::PermissionClient::permissionResult,
    [](const QString &permission, const QString &state) {
        // A status is advisory; use the File Chooser API for actual file access.
    });
permissions.requestPermission(QStringLiteral("files.read"));
```

## Validation

`./build-openwave test-permissions out/permission-qa.img` builds the kernel,
userspace, policy tests, and a separate QEMU test image. It never overwrites an
existing image. Test-only Shell automation is compiled explicitly, injected only
into that test image, and disabled again in the normal build configuration.
The runtime fixture checks unprivileged identities, identity spoofing, private
records, forged prompt responses, concurrent requests, denial, one-time delivery,
selected-file reads, selected-folder writes, Save As, persistence, daemon restart,
and revocation of descriptors held by escaped helpers. It also launches Browser
under isolation, exercises a real WebKit/JavaScript download into Downloads,
and verifies that Files remains trusted. Logs are written to
`out/permission-validation`.
It exercises playback through a virtual sink and rejects capture, unsafe plugin
creation, and forged audio credentials from a real isolated application.

Policy tests cover versioned persistence, user/application separation, denied
and restricted states, revocation, session/one-time expiry, corrupt databases,
symlinks, storage modes, and timestamp preservation. Prompt and portal contract
tests compile separate test-only binaries; production portal binaries reject
`--test-root`.

The Application Services extension adds authenticated lifecycle snapshots/events,
verified declared permissions, manifest-bound policy reconciliation and
application-lifetime decisions. New prompts offer a launch-bound "Allow while
using the application" choice; existing session records remain compatible.
Settings now includes declared capabilities before their first request. Unavailable
hardware capabilities remain restricted. The combined SDK target is
`OpenWave::ApplicationServices`; see
[the framework documentation](../application-services/README.md).
