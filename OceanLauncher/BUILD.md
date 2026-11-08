# OceanShell Blank Home Screen Investigation

## Runtime evidence

The one prior disposable boot ran at 544×960 and captured a partially rendered Ocean scene: the status area and drawer handle are visible over a black home area, with the drawer edge shaded. The scene is not wholly absent. The screenshot is in `out/ocean-identity-fix-runtime-20261029/runtime-test/ocean-before-pointer.png` and `ocean-after-pointer.png`.

The QA observer queried appd's system-bus `ListApplications` API successfully and received five launchable native applications. That confirms appd's catalog endpoint, but not that Ocean's `ApplicationsClient` request succeeded or that `AppController` populated the QML model. The Ocean stdout log was empty; stderr contained only Qt's non-UTF-8 locale warning. No QML creation, EGL, scene-graph, model, or input diagnostics were recorded in that run.

The injected click was at screen coordinate (270, 890). In the screenshot, the drawer handle is higher on screen, around the small chevron above the shaded drawer area. The click therefore did not target a known interactive control and cannot establish an input failure.

## Graphics and input configuration

The OpenRC Ocean service requests `QT_QPA_PLATFORM=eglfs`, `QT_QPA_EGLFS_INTEGRATION=eglfs_kms`, and the KMS configuration at `/etc/openwave/kms.json`. The runtime observer recorded those environment values and a 544×960 connected DRM mode. The code did not record `QGuiApplication::platformName()`, `QQuickWindow` exposure, scene-graph initialization, or frame submission, so the active QPA backend and renderer state remain unconfirmed.

Ocean's `Main.qml` contains a Qt `WaylandCompositor`, a `WaylandOutput` backed by the Ocean window, and an `XdgShell` handler. This is an implemented Wayland server path in source. The prior boot did not record whether its `wayland-0` socket was created or usable by a client.

## QA diagnostics candidate

Opt-in diagnostics are controlled by `OPENWAVE_QA_GRAPHICS=1`. They report the actual QPA platform, QML root creation, window visibility/exposure/size, scene-graph initialization, first swapped frame, appd catalog refresh errors and counts, and mouse press/release delivery. The QML adds a clearly visible QA badge with a tap handler only when that variable is set. Production service configuration does not enable the diagnostics.

The diagnostic executable was cross-compiled with the existing target toolchain and QML compilation succeeded. The new disposable image is based on the previously verified OpenRC candidate (base SHA-256 `f78096e17d2786bfd30260bc6f09e3b7b855b6a943bb641274d2dc4573e30a3d`) and retains the prior disposable identity/portal staging. Its SHA-256 is `24237774dab4675cf78270b9ba0e3cf89e1d17fa074f3d83f52b8eb0be2abfb0`. Its installed SELinux policy hash is `3be6878c04efb7acbe9dc0a0d30817e53a3ac1991372f7b8507592a9412f87bf`, matching the base image. It was later booted once under the authorization recorded below. SELinux policy and kernel inputs were not rebuilt or changed.

## Current conclusion

The earlier run did not establish the blank-home cause. The single authorized diagnostic boot and its evidence are recorded below.

The eudev shutdown `localmount` countdown is a separate lifecycle delay. The authorized graphics diagnostic run below also recorded this countdown before clean guest poweroff.

## Authorized graphics diagnostic boot (2026-10-29)

**Candidate and integrity — PASS.** The approved candidate's preboot SHA-256 was `24237774dab4675cf78270b9ba0e3cf89e1d17fa074f3d83f52b8eb0be2abfb0`. QEMU used a separate writable copy whose preboot hash matched. The approved original was re-hashed after the run and still has the approved hash. Kernel SHA-256 was `7c07db46a5ee9b90e6604aa8aaba1a27f26aee0fcec75e7732d5c20824d09bef`; the packed enforcing SELinux policy SHA-256 was `3be6878c04efb7acbe9dc0a0d30817e53a3ac1991372f7b8507592a9412f87bf`. OpenRC remained pinned and clean at `83fb7385f4e9115a1c804b1c035bfbad04ebc46a`. Waydroid was disabled. The QMP protocol harness passed before boot.

**Boot and native services — PASS.** The one guest boot loaded SELinux policy and entered enforcing mode (`audit ... enforcing=1`; bootstrap reported policy loaded and enforcing). OpenRC reached `default`. System and session D-Bus sockets were present; `org.openwave.Applications`, `org.openwave.Shell`, and other recorded native service names were registered. appd and permissiond were ready; permissiond logged that it verified appd's trusted process identity. Ocean ran as UID/GID 1000 with zero effective capabilities and `NoNewPrivs=1`. The display connector was connected at 544×960. The QA observer recorded five launchable native application entries from appd: Gallery, Browser, Settings, Water Rouge Gallery, and Files. Its `iwd` service state was `crashed`, unrelated to this offline graphics test.

**Graphics — partial render confirmed; cause NOT DETERMINED.** Ocean logged its actual Qt platform as `eglfs`. The before-input screenshot reports 544×960 and shows the status area, drawer chevrons/handle, shaded lower drawer panel, and the green QA badge. Most of the home area is black and no favorite application icons are visible. This confirms that Ocean produced visible frames and that the blank home remains reproducible; it does not establish a successful complete interface.

The expected detailed diagnostics were not present in the captured Ocean logs: there is no QML object-created/root geometry line, scene-graph initialization or first-frame signal, AppController `ListApplications` result/model count, nor graphics-API report. The independent appd D-Bus query returned five entries, but Ocean's receipt and QML delegate creation/visibility therefore remain **NOT TESTED**. `QSG_RHI_BACKEND=opengl` was configured in the service, but the selected Qt graphics API was not directly observed. The screenshot supports a failure after some QML content rendered; evidence does not distinguish model population from home-layout/delegate visibility or wallpaper presentation. The first confirmed blocker is insufficient runtime instrumentation at the Ocean-to-QML/model boundary, not a proven appd catalog failure.

The QA snapshot's `org.openwave.Wallpaper=not-registered` entry is not a valid wallpaper-service failure result: the observer queried this name on the session bus, while `wallpaperagentd` connects to the system bus. The service process was running, but the observer did not query its name on the correct bus, so wallpaper D-Bus availability is **NOT TESTED**.

**Input — NOT TESTED.** The visible QA badge bounds in the screenshot were approximately x=8–104, y=65–115. The host harness shared stdin with QEMU's serial console, so the supplied coordinate line went to the guest serial stream; the harness did not send a VNC pointer event and produced no after-input screenshot. The before screenshot is `out/ocean-blank-home-diagnostics-20261029/runtime-test/ocean-before-pointer.png`. No second boot or input retry was performed.

**Wayland — NOT TESTED at runtime.** EGLFS is the only observed QPA backend. `Main.qml` configures a `WaylandCompositor` with socket name `wayland-0`, which under the recorded `XDG_RUNTIME_DIR=/run/user/1000` would use `/run/user/1000/wayland-0`; the QA snapshot did not record this socket, its ownership, server readiness, or any client connection. No Wayland client/registry exchange or Wayland surface rendering was exercised. This run therefore does not establish whether the embedded Wayland server initialized alongside EGLFS.

**Shutdown and integrity — PASS.** QMP accepted `system_powerdown`; the guest emitted `SHUTDOWN` with reason `guest-shutdown`, serial recorded `reboot: Power down`, QEMU exited 0, and no forced termination occurred. OpenRC stopped native services and completed its `localmount` wait. Read-only `e2fsck -fn` passed on the writable test copy. Its post-run SHA-256 is `c0b4167270ca6ed765f63fd78e9b81c8126d18979d4aa22174d6456fadc90ab4`; it is distinct from the preserved approved candidate hash.

Persistent evidence is in `out/ocean-blank-home-diagnostics-20261029/runtime-test/` (`serial.log`, `boot-status.txt`, `runtime-report.json`, and the before screenshot). No runtime fix or second boot was made. The blank-home cause remains open; resolving it requires a serial-independent input channel and live capture of model/root/scene-graph and Wayland socket state.

## Wallpaper authorization and fallback repair (2026-10-29)

**Code-confirmed root cause:** `wallpaperagentd` previously sent non-root `GetCurrent`/`GetImage` requests through appd's `ResolveCaller`, which only resolves managed application instances. OceanShell is authenticated by appd's separate trusted-shell identity and is not a managed app, so this path rejected the shell. The seeded image is stored in `/var/openwave/wallpaper`, outside the per-user `users/<uid>` directory, and the old resolver had no default-selection fallback. The earlier guest log did not capture the D-Bus response, so this explains the failure from source but is not a confirmed guest trace.

The fix adds only appd's typed `ResolveWallpaperUser(connection) → userId` operation. appd permits calls only from the supervised wallpaper agent, resolves the supplied unique bus name to kernel-provided UID/PID credentials, then accepts its existing authenticated OceanShell identity or a live managed application with a matching process start time. It returns the appd session UID; caller-supplied identity claims are not used. Existing `ResolveCaller` is unchanged. `SetWallpaper` remains denied to non-root callers.

`GetCurrent` and `GetImage` now share selection: valid user selection first, then the one seeded `/var/openwave/wallpaper/<sha256>.png` default. Invalid or missing user data falls back; invalid or missing default returns `org.openwave.Wallpaper.Unavailable`. Authorization failure remains an authorization error. The agent rejects symlinks, non-regular files, multiply linked files, wrong ownership, writable group/world bits, invalid PNG dimensions/format, oversized content, and SHA-256 mismatch. Ocean receives bytes via `GetImage`, verifies the hash, and caches its own copy; no other user's private path is exposed.

The default directory and image have dedicated SELinux file types. OceanShell has read access to the default image; wallpaperagentd manages wallpaper storage. User selections remain under UID-private directories. The seeded file is `efe88320873b90dbe0f0cd304443da2ac73a2bdf10971345c6702c354e979be1.png`; the rootfs staging modes are directory `0755`, marker `0644`, and image `0644`. The previous QA candidate had directory mode `0775` and marker/image mode `0664`; the new disposable copy corrects these to root-owned `0755`/`0644` metadata.

WallpaperClient now reports the system-bus destination, method, remote error name, and message; it diagnoses malformed replies, image hash/cache failures, and whether the selected source was the user choice or system default. Ocean's wallpaper `Image` logs Qt's `errorString` on decode/load failure.

**Focused host checks — PASS:** trusted-shell identity and wallpaper-user resolution tests (2 TAP cases); appd policy contract tests (8 Python cases); wallpaper client/QML/API diagnostics contract tests (3 Python cases); Python syntax check; D-Bus integration exercising seeded default, user selection, `GetCurrent`/`GetImage` consistency, tampered user-image fallback, symlinked marker fallback, cross-user non-disclosure, and explicit missing-default error; `git diff --check`. Target appd, wallpaperagentd, OceanLauncher, and the SELinux policy compile completed. No QEMU boot was run.

**Guest-only checks — NOT TESTED:** actual OceanShell D-Bus authentication and returned bytes, Qt image loading, rendered default wallpaper, runtime SELinux labels, and user-selection behavior under the guest service identities. Packaging includes the rebuilt shared `libopenwave-wallpaper.so.0.1.0` as well as the launcher; the launcher dynamically loads this library, so both were replaced with the current build. The disposable candidate is `out/ocean-wallpaper-repair-20261029/OpenWave-ocean-wallpaper-repair.img`, SHA-256 `68f02bdc16d1b25e19516b048fe0f49d238559e174dcc9e837e41f6d2f018161`. Its packed executable, library, interface, policy, file-context, and seeded-wallpaper hashes are recorded in `out/ocean-wallpaper-repair-20261029/verification.json`; all matched their staged sources. Read-only `e2fsck -fn` passed. The packed wallpaper tree is root-owned with the required modes. The host `rootfs` mirror remains owned by the development UID because this environment would not authenticate the requested `sudo chown`; preserve the prepared candidate's metadata or normalize target ownership when repacking from the host tree. The preserved base candidate remains at SHA-256 `24237774dab4675cf78270b9ba0e3cf89e1d17fa074f3d83f52b8eb0be2abfb0`. Production `OpenWave.img` was not modified and hashes to `d29da5f99fb3a8fcf6fb6717e9f66293fe47b83cb7ef846088a59c8246dbb1a3` after staging. No QEMU boot was run.

## Wallpaper delivery and shutdown diagnosis (2026-10-29)

The previous wallpaper result is now classified correctly: `wallpaperagentd`
was running, but its API was not queried on the bus it owns. The wallpaper
service uses the system bus name `org.openwave.Wallpaper`, object
`/org/openwave/Wallpaper`, interface `org.openwave.Wallpaper1`; `GetCurrent`
returns `(ss)` and `GetImage` returns `(ay)`. The earlier QA report checked the
session bus and therefore did not establish name ownership or a failed request.
The prior black screenshot and running-service states remain valid evidence;
they do not identify whether Ocean received an image or whether QML displayed
it.

The corrected observer now records system-bus accessibility, daemon process
identity and SELinux context, the D-Bus name owner's UID/PID, interface
introspection, and actual `GetCurrent` and `GetImage` results separately. In
Ocean, `OPENWAVE_QA_GRAPHICS=1` enables WallpaperClient logs for connection,
method calls, D-Bus error name/message, response type, image bytes/hash, and
the resulting local-file URL. The home `Image` logs `Null`, `Loading`, `Ready`,
or `Error` with its source, geometry, visibility, opacity, fill mode, and
relevant parent/overlay state. `OPENWAVE_QA_WALLPAPER_IMAGE=bundled-svg` selects
a bundled SVG in that same QML `Image` for a controlled rendering comparison;
without the QA gate and override, the production binding remains
`Wallpaper.source`. These diagnostics have only been host/source tested; the
next guest run must capture the actual result before assigning a wallpaper
failure to authorization, file access, image decoding, or scene presentation.

The seeded default is the SHA-256-named 768×1366 PNG
`efe88320873b90dbe0f0cd304443da2ac73a2bdf10971345c6702c354e979be1.png` under
`/var/openwave/wallpaper`. The disposable candidate has root-owned directory
mode `0755` and root-owned marker/image modes `0644`. The service's fallback
contract is valid per-user selection first and then the verified system
default; missing or invalid user selection falls back, while an unavailable
default returns `org.openwave.Wallpaper.Unavailable`. Authorization errors
remain errors. The target QtGui configuration enables PNG/imageformat support
and links libpng; a host Qt `QImageReader` successfully decoded the seeded PNG
at 768×1366. The absence of a standalone `libqpng.so` is expected for this
embedded QtGui configuration and is not evidence of a missing decoder. Runtime
SELinux labels, guest D-Bus replies, guest Qt loading, and visual presentation
remain **NOT TESTED**.

The packed file-context rules map the wallpaper tree to
`system_u:object_r:openwave_wallpaper_default_dir_t:s0`, the hash-named default
PNG to `system_u:object_r:openwave_wallpaper_default_t:s0`, and user storage to
`system_u:object_r:openwave_wallpaper_user_t:s0`. These are the expected
restorecon labels; this offline check does not establish the labels applied in
a running guest.

### Disposable diagnostic candidate

Candidate: `out/ocean-wallpaper-shutdown-diagnostics-20261029/OpenWave-wallpaper-shutdown-diagnostics.img`

SHA-256: `f5d0547acc987589fcb9a7fda46b407add9fd5553222d1886970197160e0710e`.
It was copied from the prior wallpaper repair candidate (base SHA-256
`68f02bdc16d1b25e19516b048fe0f49d238559e174dcc9e837e41f6d2f018161`) and
contains the newly built OceanLauncher, wallpaper-client library, and corrected
observer. Each packed file was extracted and byte-compared by SHA-256 with its
build/source input. The `wallpaperagentd`, `appd`, SELinux policy, SELinux file
contexts, and seeded wallpaper hashes match the base candidate. Read-only
`e2fsck -fn` passed. Full evidence and hashes are in the candidate directory's
`verification.json`. No QEMU boot was run; this is the preboot hash. Production
`OpenWave.img` was not modified.

### OpenRC shutdown stall

The previous guest report proves only that QMP delivered powerdown, OpenRC
entered shutdown, and `udevd` emitted repeated
`waiting for localmount (50…10 seconds)` warnings before the bounded runner
forced QEMU to exit. No clean guest poweroff event was recorded. Inspection of
the pinned, unchanged OpenRC source shows this warning is emitted while a
service stop waits for an exclusive service lock; it proves lock contention
associated with `localmount`, but the prior guest did not capture the lock
holder PID/process/action, whether `localmount` itself was stopping, or the
mount being awaited. The cause and teardown order are therefore **INCONCLUSIVE**.
Do not remove the lock or add a sleep. A later authorized guest run should
capture the `localmount` lock's device/inode and matching `/proc/locks` record,
the holder's PID/cmdline/wchan, `rc-service localmount status`, service state,
and mountinfo during the wait. No service graph or shutdown behavior was
changed in this candidate.

### Offline checks

The focused observer and WallpaperClient/QML contract tests pass; shell syntax,
wallpaper-agent fallback/integrity tests, OpenRC graph validation, native
cross-builds, target ELF inspection, PNG decoding, and `git diff --check` passed
during candidate preparation. The later authorized guest run and its results
are recorded below. Exact commands and per-file hashes are recorded in
`out/ocean-wallpaper-shutdown-diagnostics-20261029/verification.json`.

## Authorized wallpaper diagnostic boot (2026-10-29)

The approved image was rechecked before boot at SHA-256
`f5d0547acc987589fcb9a7fda46b407add9fd5553222d1886970197160e0710e`. Kernel
SHA-256 was `7c07db46a5ee9b90e6604aa8aaba1a27f26aee0fcec75e7732d5c20824d09bef`;
the packed SELinux policy, file contexts, appd, wallpaperagentd, and seeded PNG
matched the recorded hashes. OpenRC remained clean at pinned commit
`83fb7385f4e9115a1c804b1c035bfbad04ebc46a`. QEMU used a separate writable
copy whose preboot SHA-256 matched the approved candidate. The approved image,
production image, kernel, and policy were not modified. The diagnostic gate
was passed as a kernel-to-init environment parameter; no wallpaper override
was set.

**Boot and service readiness — PASS.** The kernel loaded the policy and emitted
the SELinux audit record `enforcing=1`; OpenRC reached native startup and the
QA observer captured state. Runtime directory owner/mode/label validation
reported success. appd, permissiond, wallpaperagentd, and Ocean were running;
permissiond verified appd's service identity. The observer found the system
bus accessible and `org.openwave.Wallpaper` registered. Its unique owner was
`:1.21`, authenticated UID 0, PID 1429, matching the running
`/System/Services/wallpaperagentd` process. Interface introspection succeeded.
The observer's root `GetCurrent` call returned the seeded PNG path and
64-character hash matching the approved wallpaper. This validates the default
selection for that root request, not Ocean authorization.

**Ocean wallpaper request — FAIL, cause localized.** Ocean itself connected to
the system bus and invoked `org.openwave.Wallpaper1.GetCurrent`. The service
returned `org.openwave.Wallpaper.Unauthorized`; its message says appd rejected
the caller as neither the trusted shell nor a live managed application. Thus
the black home screenshot is explained at the wallpaper-source delivery
boundary: Ocean did not receive a selected source. The error is generated when
`appd_resolve_wallpaper_user()` rejects both identity paths. The generic error
does not identify which trusted-shell predicate failed. The observer did see
Ocean PID 1514 running as `ocean` from the expected launcher executable, and
the shell D-Bus name was registered, but it did not record that name's owner
PID, protected pidfile metadata, or SELinux label. This matters because appd
requires the D-Bus credential label and `/proc/<pid>/attr/current` to match
`openwave_shell_t` while enforcing. The observer could not read process
contexts either, which makes this the next identity check to investigate, but
does not prove it is the failing predicate. Do not weaken the check. The
wallpaper image itself was successfully selected by the root observer's
`GetCurrent`; PNG decoding was already confirmed on the host.

The QA observer's `GetImage` call returned success, but its payload summary is
**INCONCLUSIVE**: the image-count pipeline invoked missing `wc` and `tr`
commands and printed no byte count or PNG-signature result. A later observer
fix should avoid those absent utilities and preserve the method exit status.
Ocean never reached `GetImage` because its `GetCurrent` call was rejected.

**QML — NOT CAPTURED.** The 544×960 screenshot still shows the status bar and
QA badge over a black home area at
`out/ocean-wallpaper-shutdown-diagnostics-20261029/runtime-test/ocean-wallpaper-diagnostic.png`.
Ocean's log confirms the real D-Bus error and zero favorites/apps in its QML
model. No `wallpaper-qml` Image status line was captured, despite the QA gate;
therefore `Image.Null` and presentation/overlay state were not directly
verified. The service rejection explains the missing wallpaper source, but a
later test must still confirm image status and visibility after trusted-shell
authentication is corrected.

The observer reported `/proc/<pid>/attr/current` and `selinux_enforce` as
unavailable; the kernel audit record independently confirms enforcing mode.
No AVC record was found in the captured serial log. Runtime process contexts
therefore remain **NOT CAPTURED**, rather than evidence of a particular domain.

**Shutdown — FAIL.** QMP delivered the powerdown request. OpenRC stopped native
services, then `udevd` waited for `localmount` with the 50-to-10-second
countdown. No QMP `SHUTDOWN` event occurred; the bounded runner terminated
QEMU. This is not a clean shutdown. The same service-lock wait recurred; the
lock-holder PID/action and contemporaneous mount state remain unknown.

The writable copy initially needed ext4 journal recovery after forced QEMU
termination. Recovery and a subsequent read-only `e2fsck -fn` both completed;
the final writable-copy SHA-256 is
`cb09d9182c1a646fe7e5c5c85631b3af8a4af9b7516a9049bf50667ece56c5f4`. The
approved candidate remains the immutable preboot artifact and retains its
approved hash. Full serial log, screenshot, observer output, QMP report, and
filesystem-check logs are under
`out/ocean-wallpaper-shutdown-diagnostics-20261029/runtime-test/`; the
summary is `out/ocean-wallpaper-shutdown-diagnostics-20261029/verification.json`.
No second boot was performed.

## Wallpaper caller authentication follow-up (2026-10-29)

The previous guest log establishes the delegation direction: wallpaperagentd
passed Ocean's original unique D-Bus sender to appd's narrow
`ResolveWallpaperUser` method. appd authenticated the requesting process as the
supervised wallpaper service before resolving that forwarded name; the
reported caller rejection therefore came from the forwarded caller resolution,
not from a failed wallpaperagentd identity check. The log did not preserve the
unique sender, its credentials, or the individual rejected predicate, so the
exact runtime cause remains **NOT CAPTURED**.

Source review found that appd's Ocean trusted-shell check verified bus UID/PID,
`org.openwave.Shell` ownership, the protected PID file, executable path, and
SELinux context, but did not consume OpenRC's existing root-protected
`.identity` record. That record binds PID and UID to process start time. appd
now validates this record and current start time as an additional required
condition. It does not change `ResolveCaller`, managed-app policy, service
authentication, or the fail-closed result. This is a confirmed identity-check
gap and a hardening correction; available evidence does **not** prove it was
the particular check that caused the previous wallpaper denial.

`OPENWAVE_QA_IDENTITY=1` enables private service-log diagnostics for the
original sender, delegated service identity, credential lookup, UID/PID,
trusted-shell PID file, identity/start-time record, executable, SELinux
labels, managed-app result, and stable rejection code. Logs stay in service
output and are not exposed through appd's D-Bus API. `OPENWAVE_QA_SHUTDOWN=1`
enables a bounded 15-minute observer that records `/proc/locks`, the
`localmount` holder PID/command/wait channel, service state, and mountinfo
during a transition. Both switches are opt-in.

The shutdown trace and pinned OpenRC 0.64.1 source show that `openrc-run`'s
`svc_wait()` waits for a shared `flock()` on the per-service exclusive lock;
the `udevd` warning identifies `localmount` as the transition it is waiting
for. The old run did not capture the lock holder or localmount's in-progress
operation, so the cause and teardown ordering remain **UNRESOLVED**. No lock,
dependency, or unmount behavior was changed.

Focused checks passed: native cross-builds of appd and wallpaperagentd;
authenticated trusted-shell and managed-app host tests including stale
start-time rejection; wallpaper selection/fallback and `GetImage` integration;
OpenRC dependency validation; observer shell syntax; and read-only ext4
validation. The disposable candidate is
`out/ocean-wallpaper-caller-auth-20261029/OpenWave-wallpaper-caller-auth-candidate.img`
with SHA-256 `1496506f181743e5804f720256e63e34947af6df4dad54f6cac738e8c5690bd4`.
Only appd, wallpaperagentd, and the QA observer differ from the previous
diagnostic image; the SELinux policy, file contexts, system-bus policy, Ocean
binary, wallpaper client, and seeded image are preserved. No QEMU boot was
performed. Guest confirmation of the caller predicate, image rendering, and
shutdown-lock owner remains **NOT TESTED**.

## Authorized wallpaper caller-authentication boot — 2026-10-29

One bounded QEMU boot used a writable copy of
`out/ocean-wallpaper-caller-auth-20261029/OpenWave-wallpaper-caller-auth-candidate.img`.
The approved candidate hash matched before boot (`1496506f181743e5804f720256e63e34947af6df4dad54f6cac738e8c5690bd4`) and remained unchanged afterward. The production `OpenWave.img` remained at its recorded hash. Kernel hash matched `7c07db46a5ee9b90e6604aa8aaba1a27f26aee0fcec75e7732d5c20824d09bef`.

SELinux loaded and entered enforcing mode (`enforcing=1` audit record). OpenRC reached the native session; appd, permissiond, wallpaperagentd, and Ocean were running. appd readiness and permissiond's trusted-appd identity check passed. The system wallpaper D-Bus name and interface were present, and the root QA `GetCurrent` request returned the seeded default PNG path and digest.

Ocean's own `GetCurrent` request reached wallpaperagentd but was rejected by appd with `Wallpaper.Unauthorized: caller is not the trusted shell or a live managed application`. The exact failed identity subcheck remains **NOT CAPTURED**. The requested `OPENWAVE_QA_IDENTITY=1` and `OPENWAVE_QA_SHUTDOWN=1` appeared in the kernel command line, but no identity-stage records or `localmount-lock-owner.log` were produced. The precise environment propagation/filtering point is not yet established; the bootstrap's `execv` hands off to `openrc-init`, so future QA activation must be verified in the supervised service environment before relying on it.

The framebuffer screenshot is 544x960 and shows the QA badge and status bar over a black home surface; wallpaper rendering therefore remains **FAIL**. The observer called `GetImage`, but its byte-array classification failed because `wc` and `tr` are absent in the guest; payload validity is **NOT VERIFIED**.

QMP delivered `system_powerdown`, but no QEMU `SHUTDOWN` event arrived. OpenRC again logged `udevd: waiting for localmount` at 50, 40, 30, 20, and 10 seconds. The bounded runner forced QEMU termination after its 60-second shutdown window. Since the QA lock watcher did not activate, the localmount lock holder and blocked operation remain **NOT CAPTURED**. The writable image copy needed journal recovery after forced termination; `e2fsck -p` recovered it, and the subsequent `e2fsck -fn` passed. No second boot was performed.

Runtime evidence is preserved under
`out/ocean-wallpaper-caller-auth-20261029/runtime-test-20261029/`, including
`serial.log`, `boot-status.txt`, `ocean-wallpaper-diagnostic.png`,
`runtime-report.json`, the pre/post-run hashes, and filesystem-check logs. The
exact QMP outcome and component statuses are in that report. The approved
candidate and production image were not modified.

## Wallpaper trusted-shell QA diagnostics — 2026-10-29

The previous wallpaper-authentication boot did not activate its identity or
shutdown diagnostics: kernel command-line tokens do not become OpenRC service
environment variables. For the disposable diagnostics candidate, Ocean's
OpenRC service now explicitly exports `OPENWAVE_QA_GRAPHICS=1` and
`OPENWAVE_QA_WALLPAPER_IMAGE=bundled-svg`. These enable WallpaperClient's
system-bus call/error/response logging and a temporary bundled-image rendering
comparison. The existing Ocean QA observer records the relevant environment
and collects Ocean stdout/stderr. Production Ocean service configuration stays
diagnostics-off.

This candidate preserves Ocean's production wallpaper request path and the
original system-bus unique sender. wallpaperagentd logs the UID and PID it
resolves for that sender under `OPENWAVE_QA_IDENTITY`; appd logs the delegated
sender, independently resolved credentials, D-Bus name owner, protected
identity-record PID/UID/start-time checks, exact executable, SELinux context,
and authorization result. The candidate keeps Ocean at UID/GID 1000 with
`NoNewPrivs` and the registrar-generated root-protected identity record. The
diagnostic reason is confined to QA service logs; the public D-Bus rejection
remains generic.

The installed candidate passes offline checks for QA environment propagation,
the packed appd and wallpaperagentd hashes, the unchanged packed Ocean launcher,
and the enforcing SELinux policy. Candidate image:
`out/ocean-wallpaper-qa-diagnostics-20261029/OpenWave-wallpaper-qa-diagnostics.img`
(SHA-256 `f301e4aa7ea7b084260c766b887f4cdb9fdd69afe17504981591adc34debef95`).
No guest was started for this preparation, so the exact rejected predicate,
wallpaper rendering comparison, and shutdown-lock holder remain **NOT TESTED**.

The authorized diagnostic boot was held at preflight and **NOT RUN**. The
candidate's Ocean/appd/wallpaperagentd QA flags are present, but offline policy
inspection found no declared system-domain write access for the observer's
persistent `/var/log/openwave-qa` output or for the daemon logs under
`/run/openwave`. Thus the diagnostic path cannot yet guarantee capture of the
actual Ocean caller and its authentication stages while SELinux is enforcing.
No Ocean, policy, or image changes were made; the candidate must be corrected
and separately authorized before guest validation.


## SELinux-enforcing wallpaper identity diagnostics — 2026-10-29

The disposable QA candidate routes Ocean's opt-in wallpaper diagnostics to
`/var/log/openwave-qa/ocean.stdout.log` and `ocean.stderr.log`. OpenRC opens
these as root-owned mode-0600 append-only outputs, then launches the Ocean
process as UID/GID 1000 in `openwave_shell_t`; Ocean writes through the
inherited descriptors. The directory is root-owned mode 0700 and receives the
specific `openwave_qa_evidence_t` file label. The compiled QA policy grants the
shell domain append/write on that evidence type and `fd use` against the OpenRC
system supervisor, as required for the inherited descriptors. It grants no
Ocean write access to general private or runtime file types, and the production
Ocean service keeps its normal logging configuration and QA flags disabled.

The QA observer captures the wallpaper D-Bus caller identity stages and
bounded shutdown-lock state in the same protected evidence directory. It does
not return internal authentication diagnostics to D-Bus callers. Policy
inspection and host tests pass, but no guest run was performed for this
preparation. Runtime label application, Ocean log delivery, the exact failed
trusted-identity check, and wallpaper display remain **NOT TESTED**. See
`userspace/openrc/BUILD.md` and the candidate `verification.json` for the
complete candidate and policy hash.

## Latest authorized enforcing diagnostic boot — 2026-10-29

One boot used the approved candidate
`out/ocean-wallpaper-qa-evidence-20261029/OpenWave-wallpaper-qa-evidence-final.img`
(preboot SHA-256
`2640a00c1670d071e5c060f92f21c0f33d8df51ce0cf875c209359f504b07291`) with the
compiled SELinux policy
`ca8d1bc136b7586b5b164d6a0d8a9842e7ac56cd346ece9991ab3bffdeec5de0`.
SELinux loaded and enforced; appd, permissiond, both buses, and wallpaperagentd
were ready. The QA evidence directory and files that were created carried the
expected `openwave_qa_evidence_t` label, and appd/wallpaperagentd diagnostic
records were preserved.

Ocean did **not** launch. OpenRC's `start-stop-daemon` failed opening
`/var/log/openwave-qa/ocean.stdout.log` with `EACCES` before creating Ocean's
pidfile or identity record. The OpenRC service configured
`command_user="ocean:ocean"` and `command_background="yes"`; its
start-stop-daemon changes credentials before opening the redirected log. Since
the log directory is intentionally root-only mode 0700, UID 1000 cannot
traverse it. This is the confirmed startup blocker; the actual Ocean wallpaper
call and its nine trusted-shell predicates were **NOT REACHED**. No actual
Ocean D-Bus sender, PID, executable, SELinux context, or QML `Image.status` was
recorded. The error is consistent with DAC and no matching AVC appeared in
serial output.

The root observer did query wallpaperagentd, but those UID 0 requests are not
Ocean. Its `GetCurrent` returned the verified default path/hash. Its
`GetImage` call returned to the shell, but payload validation was not recorded:
the observer invoked `tr` and `wc` by name and neither executable was present
in the packed guest despite the BusyBox applets being compiled. The separate
appd records for root-originated callers failed the configured-session UID
check; this is not evidence about Ocean. A QEMU 544x960 mode was requested, but
the Ocean framebuffer and rendered wallpaper were **NOT REACHED** and no
framebuffer screenshot was captured.

Shutdown was **not clean**. QMP powerdown reached PID 1, which began service
shutdown and stalled at `udevd: waiting for localmount` (50 through 10 seconds).
No QMP `SHUTDOWN` event arrived; the bounded runner forced QEMU termination.
The watcher started but its lock-owner file remained empty, leaving the holder
and operation unknown. The original post-run writable image has SHA-256
`2bcd19995e0497ac0b03fffd1e005b5d2946201e4184c560d4d6c0d66e3b41a5`; its
read-only check reported a pending journal and metadata count differences. A
separate recovery copy passed read-only `e2fsck -fn` and hashes to
`5c8c6ce0d333b3e239c1a56a7641da417d73c5a384ad6e2da37cba99e19479c8`. Logs and
extracted evidence are under
`out/ocean-wallpaper-qa-evidence-20261029/runtime-test-20261029/`. This boot
does not resolve the Ocean authentication cause and no second boot was run.


## Ocean QA log startup correction — 2026-10-30

The prior Ocean startup failure occurred before trusted-shell authentication.
OpenRC's pinned `start-stop-daemon` drops to Ocean's configured UID before
opening output redirects. Pointing it into the intentionally root-only
`/var/log/openwave-qa` caused DAC `EACCES`: UID 1000 could not traverse that
0700 directory. No matching AVC was recorded, and no authentication rule was
reached.

The disposable QA service now writes stdout/stderr under
`/run/user/1000/openwave-logs`, provisioned by `openwave-runtime` before Ocean
as owner/group 1000:1000 and mode 0700. Files are created mode 0600. The SELinux
context is `openwave_qa_evidence_t`, assigned by a filename-specific type
transition for the directory at creation. Ocean has file append and inherited
descriptor access only; it has no directory traversal permission to the
evidence type and no access to appd or wallpaperagentd's private log files.

The QA observer checks the exact Ocean files for ownership, mode, regular-file
type, and symlink substitution before collection. Its wallpaper `GetImage`
probe decodes the complete GVariant byte array, checks the PNG signature, and
compares its SHA-256 with the digest returned by `GetCurrent`. Target BusyBox
fixture tests passed valid payload and tampered-payload checks.

Source, policy, candidate validation, image packing, and read-only ext4 checks
pass. Candidate image:
`out/ocean-wallpaper-qa-logfix-20261029/OpenWave-ocean-qa-logs-final-policy.img`
(SHA-256 `14a72479c8b0f88c8e47343becbd74433e203cce00ff110354bbdd865ee4129c`);
compiled SELinux policy SHA-256
`79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6`.
No guest was booted. Actual Ocean startup, guest log delivery/collection,
trusted-shell authentication, wallpaper display, and the shutdown lock holder
remain **NOT TESTED**.

## System-bus shell identity correction candidate (2026-10-30)

The authorized runtime log identified the owner mismatch precisely: Ocean's
wallpaper call came from system-bus unique name `:1.27` as UID 1000, PID 1509,
but appd's `GetNameOwner("org.openwave.Shell")` returned a different
connection. Source inspection explains why. `ApplicationsClient::Transport`
created its own `GDBusConnection` with `g_dbus_connection_new_for_address_sync`
and registered `org.openwave.Shell` there. `WallpaperClient` used
`g_bus_get_sync(G_BUS_TYPE_SYSTEM)`, GLib's shared process system-bus
connection. The appd client also used its private connection for its readiness
check; trusted-shell verification runs before the catalog RPC, so this mismatch
made Ocean report appd unavailable and left its model empty. appd remained
ready and independently returned five entries to the QA observer.

ApplicationsClient now uses `g_bus_get_sync(G_BUS_TYPE_SYSTEM)` too and does
not close that shared connection when its transport is destroyed. Its
constructor waits for the initial system-bus/name-registration attempt to
finish before returning. `main.cpp` constructs `AppController` (which owns
ApplicationsClient) before `WallpaperClient`, so the shell name is registered
before the wallpaper client's first asynchronous refresh. QA logs record the
registration result, shell connection name, wallpaper sender unique name and
UID/PID, and the current shell-name owner. appd's QA-only diagnostic records
the `GetNameOwner` result plus owner and forwarded-sender UID/PID. None of
these diagnostics changes production authorization decisions.

appd still compares the original unique sender with the shell-name owner, then
checks the protected pidfile and identity record, PID/UID/start time,
executable, and enforcing SELinux label. A host GTestDBus regression proves
that two `g_bus_get_sync` calls share the accepted name owner, while a separate
connection from the same process and UID is still rejected as
`shell-name-owner-mismatch`. Managed application identity remains on its
existing live-instance path. The checks after the owner comparison require a
guest run and remain **NOT TESTED**.

The application catalog still uses the existing `ListApplications` RPC. QA
logs service availability, list errors, returned entry count, launchable
count, favorites, and drawer model sizes. The previous zero count is
consistent with the client failing its pre-catalog shell-owner check; actual
Ocean model population after this correction remains **NOT TESTED**.

The previous guest's `/etc/openwave/kms.json` selects connected `Virtual-1`
mode `544x960`; the service passed `QT_QPA_EGLFS_KMS_CONFIG` and the guest
reported `eglfs` with `eglfs_kms`. The runtime listed `544x960` among the
connector's modes, but the exposed `QQuickWindow` was `720x400`. Thus the
requested mode was not the active Qt window size. Qt documents this same KMS
JSON `outputs[].name`/`outputs[].mode` form and the `QT_QPA_EGLFS_KMS_CONFIG`
variable ([Qt EGLFS/KMS configuration](https://doc.qt.io/qt-6/embedded-linux.html)).
The trace does not show whether Qt parsed the file or why the mode-set failed,
so the precise runtime cause remains **NOT DETERMINED**. The disposable QA
overlay now enables `QT_QPA_EGLFS_DEBUG=1` and
`QT_LOGGING_RULES=qt.qpa.eglfs.kms=true` for the next authorized run. No display
mode or interface change was made.

The prior shutdown trace still ends at `udevd: waiting for localmount`, and the
60-second runner forced QEMU termination. The old background observer recorded
holder details only when `/proc/locks` exposed a matching inode, so an empty
file did not distinguish an absent lock record from a failed observation. The
QA observer now also captures lock-file presence, matching records and holder
metadata, OpenRC state files, and mountinfo synchronously in its `stop` hook,
before `localmount` teardown. This improves evidence capture but does not
resolve the shutdown stall or prove the holder. OpenRC behavior was unchanged.

Focused host validation passed: shared-name ownership and same-process
separate-connection rejection (2 TAP cases); appd trusted-shell and wallpaper
resolver policy checks (9); QA environment overlay (4); observer/GetImage
checks (4); OpenRC boot-observability check; source builds of appd,
`openwave-applications`, and `openwave-wallpaper`; candidate OpenRC validation
(39 enabled services, 20 command ELFs, 8 disabled); host OpenRC dependency
validation (24 scripts, acyclic); packed-file hash comparisons; read-only ext4
validation; shell syntax; and `git diff --check`.

The enforcing disposable candidate is
`out/ocean-shell-bus-fix-20261030/OpenWave-ocean-shell-bus-fix-final.img`,
SHA-256
`028de33c9dab6b76a09ef8d3c5cc37c81a16c3b60c45e436e60aa9cb184e3cc5`.
Artifact hashes and protected inputs are in
`out/ocean-shell-bus-fix-20261030/verification.json`. The candidate preserves
the prior launcher and wallpaperagentd binaries, updates appd and the two
client libraries, and includes candidate-only QA logging. Its SELinux policy
is unchanged at
`79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6`; Waydroid
remains disabled. Production `OpenWave.img`, production rootfs, kernel, prior
QA candidate, and pinned OpenRC checkout remain unchanged. QEMU was **NOT RUN**.

## Shell bus ownership runtime validation — 2026-10-30

One authorized boot of the disposable candidate confirmed Ocean's system-bus
unique name `:1.24` matched `GetNameOwner("org.openwave.Shell")`; appd passed
the trusted-shell PID/UID/start-time, executable, and SELinux checks. Wallpaper
`GetCurrent`/`GetImage` succeeded, its digest verified, and the wallpaper image
reached QML `Ready`. EGLFS set 544x960, Ocean's window was visible/exposed at
544x960, and a frame was swapped. QMP screenshot capture failed with `no
surface`, so visual inspection is not claimed.

The ApplicationsClient path is still **FAIL**: Ocean logged
`appd-service-available=false` with zero apps although the root QA observer's
`ListApplications` returned five entries. The specific Ocean-side verification
failure was not recorded; this remains separate from the now-passing wallpaper
shell identity path. Runtime evidence is in
`out/ocean-shell-bus-fix-20261030/runtime-test-20261030/`.

## ApplicationsClient destination repair candidate (2026-10-30)

The next ApplicationsClient investigation found a concrete transport/policy
mismatch. The client verified appd's unique D-Bus owner and then sent its API
method to that unique name (`:1.x`). The installed system-bus policy permits
`org.openwave.Applications1` method traffic to the well-known destination
`org.openwave.Applications`; it does not grant the default policy a unique-name
destination. The root QA observer used the well-known name, explaining why its
five-entry catalog query succeeded while Ocean did not. The earlier guest did
not capture the client's precise verification or D-Bus error stage, so this is
a source-confirmed defect consistent with the observed failure, not a captured
guest trace of that exact call.

ApplicationsClient now verifies the well-known service's current unique owner
and retains that owner for signal-sender filtering, but sends API calls to the
policy-authorized well-known name. Its existing shared system-bus connection
and `org.openwave.Shell` ownership are preserved. If appd owns its name but its
protected process identity is temporarily unavailable during startup/restart,
the client retries verification at a bounded one-second interval and publishes
availability changes. The retry stops when identity verification succeeds,
the appd name disappears, or the bus closes; while appd keeps the name but its
identity remains invalid, it continues checking once per second. It still
fails closed on missing shell ownership,
non-root appd credentials, or a failed supervised-process identity check.
QA-only logs now distinguish bus connection, shell-name ownership, appd owner
credentials/identity, D-Bus destination/error, reply signature and catalog
count, controller decode/model count, and QML delegate creation. No wallpaper
code, D-Bus policy, SELinux policy, or appd authorization was relaxed.

The `ListApplications` dispatcher requires a live D-Bus sender credential, but
does not require a trusted-shell or permissiond identity. The system-bus policy
allows authenticated local clients to call this read-only catalog method. This
is the current public-catalog contract; it is not an appd service-identity
failure and was not changed. Privileged launch and administration operations
retain their existing per-method checks.

The actual `ApplicationsClient` runtime fixture now asserts the five native
catalog IDs/names/icons and successful method response. The launcher builds
with QA-only catalog entry/model and drawer/favorite delegate logs. Source
contracts verify shared-bus shell registration, appd fail-closed identity and
retry, the policy-authorized destination, the existing controller/QML model
binding, and the five-entry runtime fixture. The fixture compiled for the
OpenWave target but was not executed because that requires the guest runtime.

Screenshot capture is now compatible with the QEMU runner's
`-display egl-headless` plus VNC setup. QMP `screendump` requires a QEMU display
surface and returned `no surface`; the runner's VNC server exposes an RFB
framebuffer instead. `scripts/capture-vnc-framebuffer.py` is a small host-side
RFB client that saves raw framebuffer updates as PNG. It passed a protocol test
against a disposable host QEMU instance with no OpenWave kernel, disk, or guest
boot (640×480 test frame). This validates capture transport only, not Ocean's
next guest screenshot.

The prior shutdown completed: serial reached `reboot: Power down`, QEMU exited
with status 0, and OpenRC unmounted the guest filesystems; the separate QMP
`SHUTDOWN` event was not observed. The `udevd: waiting for localmount`
countdown shows OpenRC waiting on the localmount service's exclusive lifecycle
lock (`svc_wait()` in pinned OpenRC), whose waiter uses a shared flock with a
60-second timeout and 10-second warnings. The previous observer snapshot ran
before the wait and captured no holder. The particular process/operation holding
the lock remains unknown. No shutdown service change was made.

Fresh enforcing disposable candidate:
`out/ocean-applications-client-fix-20261030/OpenWave-ocean-applications-client-fix.img`
(SHA-256
`d81cc32d941e859f238e2909ba839703a232b0e09b09a31ebfadb1f40c222ce3`). It was
created from the prior candidate's disposable `final-rootfs`; production
`OpenWave.img` and the previous candidate remain unchanged. The packed launcher
hash is `6b9483cef4d0522686a02c91f666517aabca7f5fd4c6a116decabfdd189c5ec0` and
the packed `libopenwave-applications.so.0.1.0` hash is
`568066a12da47efde9a7e09977e563b70a9f9da8fcef3085a5a6c57843ae79dc`; both
match the build/staging copies. Packed SELinux config remains
`SELINUX=enforcing`; policy hash
`79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6` and file
contexts hash `3121b588e6eb532d41b459904b63fcc5797a68d279757e8869238355e642ecb1`
are unchanged. The wallpaperagentd and wallpaper client hashes are also
unchanged. Read-only `e2fsck -fn` passed, and packed client/launcher bytes match
the candidate root.

Focused checks passed: ApplicationsClient contracts (5), trusted-shell policy
(9), OpenRC dependency graph (24 scripts), service readiness (3), supervised
identity (6), QEMU runner (8), Python syntax, `git diff --check`, the OpenWave
SDK build, OceanLauncher cross-build, and the target `appd-qa` runtime-test
compile. A host-only QEMU/VNC framebuffer capture test passed. Ocean's five-app
model population, drawer delegates, app launches, post-fix screenshot, and
guest shutdown were **NOT TESTED at candidate preparation time**. The later
authorized runtime result is recorded below.

## ApplicationsClient runtime validation — 2026-10-30

After authorization to test the disposable candidates, one bounded boot used a
writable copy of
`out/ocean-applications-client-fix-20261030/OpenWave-ocean-applications-client-fix.img`.
The immutable candidate SHA-256 matched
`d81cc32d941e859f238e2909ba839703a232b0e09b09a31ebfadb1f40c222ce3`.
Protected kernel, enforcing policy, file-context database, production image,
and pinned OpenRC inputs matched the recorded hashes.

**System startup — PASS.** The guest loaded the policy and entered enforcing
mode (`enforcing=1`); OpenRC 0.64.1 became PID 1 and started native services.
appd reported ready with five applications. permissiond verified appd's
supervised identity. Ocean ran as UID/GID 1000 with `NoNewPrivs=1`, zero
effective capabilities, and the shell SELinux domain. EGLFS displayed a
visible/exposed 544x960 window, swapped a frame, and loaded the default
wallpaper. The framebuffer is preserved at
`out/ocean-applications-client-fix-20261030/runtime-test-20261030/ocean-vnc.png`.

**Catalog — FAIL before the API request.** ApplicationsClient used the shared
system-bus connection (`:1.25`), confirmed ownership of `org.openwave.Shell`,
resolved appd's owner as `:1.6` with UID 0/PID 1064, and rejected appd at
`openwave_supervised_process`. `RegisterShell` failed its precheck; no
`org.openwave.Applications1` method call or catalog response was reached.
Separately, the root QA observer called `ListApplications` and received all
five expected entries.

The first failed operation is attributable to the verifier's privilege
mismatch: it opens `/run/openwave/appd.pid.identity` and reads
`/proc/1064/stat` to compare the process start time. The registrar creates
identity records root:root mode 0600, while Ocean runs as UID 1000; the
`openwave_shell_t` policy also has no read access to `openwave_proc_t`. The
packed source/configuration and observed identities establish that Ocean
cannot pass this direct check under the unchanged DAC and enforcing policy.
The candidate did not record an `open()` errno and no matching AVC was
captured, so neither is claimed as directly observed. The prior well-known-name
destination correction was not exercised because verification stopped first.
Ocean's model remained at zero entries; drawer delegates were not created and
native app launch was **NOT TESTED**.

The wallpaper path remains **PASS**: appd accepted Ocean's trusted-shell
identity, GetCurrent/GetImage succeeded, and QML loaded the returned PNG. No
wallpaper authentication code changed.

**Shutdown — FAIL, forced.** QMP connected and delivered `system_powerdown`,
but no shutdown event arrived within the bounded wait. Serial showed
`udevd: waiting for localmount` at 50, 40, 30, 20, and 10 seconds before QEMU
was terminated. This was not a clean shutdown. The stop-hook snapshot recorded
`localmount_lock=absent`; the later wait's holder remains unknown. The original
writable copy is preserved at
`runtime-test-20261030/guest-writable.img` (SHA-256
`8feec8b1a60ea15a03df4ee0956534a8993f02b57cd204750c28d3859018bb01`). Its
read-only filesystem check reported journal-skipped metadata differences. A
separate journal-recovered copy at `runtime-test-20261030/guest-recovered.img`
passed a subsequent read-only filesystem check. The immutable candidate and
production artifacts remain unchanged.

The runtime report, serial log, extracted QA status, screenshot, QMP report,
and guest copies are under
`out/ocean-applications-client-fix-20261030/runtime-test-20261030/`. A future
correction must keep the root-only identity record and start-time verification
private to a trusted verifier while providing Ocean an authenticated result.
No permissions, SELinux rules, or identity checks were relaxed in this run.

## ApplicationsClient broker runtime validation — 2026-10-30

One bounded QEMU run used the candidate recorded in `out/ocean-applications-client-broker-20261030/verification.json`. Its complete preboot SHA-256 was `d73107612e08553962eced12168eb112cf1e225f680066484753caf3cab89955`; the approved kernel and protected inputs matched their recorded hashes. OpenRC remained pinned and clean. SELinux loaded and entered enforcing mode. OpenRC ran as PID 1; appd, permissiond, Ocean, D-Bus and the native service set started, while Waydroid remained disabled.

The prior direct cross-domain appd identity read prevented Ocean from authenticating the root-owned service record. The new path keeps that verification in permissiond and appd. Runtime evidence confirms Ocean's shared system-bus sender `:1.25` (UID 1000) owned `org.openwave.Shell`; appd was `:1.6` (UID 0, PID 1066); `VerifyApplicationsService` returned the same owner and `identity=verified`. Ocean's `RegisterShell`, `SetSessionReady`, and `GetRunningApplications` calls received valid replies. Wallpaper `GetCurrent`/`GetImage` and the existing trusted-shell checks also passed. This confirms the broker identity path without weakening the root-only identity record or SELinux policy.

The root QA observer independently received the expected five app records from appd. However, Ocean's captured client log did not contain a `ListApplications` call or the AppController/QML model-count diagnostics. The VNC screenshot is a real 544x960 home-screen capture, with the expected three pinned shortcuts; the app drawer was not opened. Therefore the runtime confirms service authentication and basic client calls, but application-model refresh, all five drawer entries, delegate creation, and application launching remain **NOT TESTED**. The candidate's Ocean launcher binary contains the QA diagnostic strings, but Qt `qInfo`/QML diagnostics were not present in the saved QA output; no model result is inferred from the screenshot.

Shutdown was **not clean**. QMP delivered powerdown and PID 1 entered the shutdown runlevel, then serial showed `udevd: waiting for localmount` counting down from 50 to 10 seconds. The observer found no localmount lock file, and no holder was established. No QMP `SHUTDOWN` event arrived before the 60-second bound, so QEMU was forcibly terminated. The writable guest copy hash is `d5a34d5de02ca6966343914c1c705354af8fbc18736599bc1e4adfb1a51eb12c`; read-only `e2fsck -fn` reported journal/orphan metadata differences. That original copy is preserved. A separate recovery copy, hash `5738954a23c6eb4a7d8ebbed5e780a39d47f862ce3900b17e147a7f154664c67`, passed `e2fsck -fn` after journal recovery.

Serial, QMP/runtime report, VNC PNG, original writable copy, recovered copy, and extracted QA evidence are under `out/ocean-applications-client-broker-20261030/runtime-test-20261030/`. The immutable candidate, production image/rootfs, kernel, policy and upstream OpenRC checkout were not modified. No second boot was run.

## ApplicationsClient catalog validation — 2026-10-30

The earlier broker run's missing catalog trace was an evidence-collection gap: the QA observer copied only the last 120 Ocean stderr lines in an early status snapshot. That snapshot did not establish that `ListApplications` had not run. The later diagnostic candidate retained 2,000 lines and launched Ocean with `--drawer` so both the API/model path and visible drawer contents could be checked.

The final disposable candidate is `out/ocean-applications-client-normal-wallpaper-20261030/OpenWave-ocean-normal-wallpaper.img` (SHA-256 `a8cc0720bd0d24fe9e8f088f41191cae2da2d7c19bf409a7c2fc0547776857c7`). It was cloned from the catalog diagnostic candidate and changed only its candidate-specific Ocean service arguments and QA wallpaper environment: the drawer was opened at startup and the test-only SVG wallpaper override was removed. The OpenWave production image, rootfs, kernel, SELinux policy, pinned OpenRC, appd, permissiond, ApplicationsClient, and Ocean launcher were not changed for this overlay.

**Catalog and identity — PASS.** The one complete bounded guest run loaded SELinux enforcing and OpenRC 0.64.1 as PID 1. Ocean ran as UID/GID 1000 with zero effective capabilities and `NoNewPrivs=1`. Its shared system-bus sender `:1.24` owned `org.openwave.Shell`; appd's owner was `:1.5` (UID 0, PID 1060), and permissiond's `VerifyApplicationsService` returned the same verified owner. ApplicationsClient reported `appd-service-available=true`, called `ListApplications` over the system bus, received signature `aa{sv}` with five entries, and decoded five launchable applications. The records were Gallery, Browser, Settings, Water Rouge Gallery, and Files. QML observed five apps, three favorites, and two unpinned drawer entries. The visible 544x960 drawer screenshot shows the three pinned apps and both unpinned delegates. The Water Rouge Gallery record has no icon URL; its text fallback was displayed. The screenshot and full QA/serial evidence are under `out/ocean-applications-client-normal-wallpaper-20261030/runtime-test-20261030/`.

The normal system wallpaper was selected and loaded from the default PNG; the QA-only SVG override was absent. Wallpaper authentication and image delivery remained successful. A non-fatal QML warning at `Main.qml:1283` reports an undefined value assigned to a bool; it did not prevent the catalog or drawer from rendering and was not changed in this focused pass.

**Shutdown and filesystem — PASS.** The runner delivered QMP powerdown and observed the guest `SHUTDOWN` event; OpenRC completed poweroff and QEMU exited with status 0 without forced termination. The existing udevd wait for `localmount` counted down from 50 to 10 seconds before poweroff. Read-only `e2fsck -fn` passed on the post-run writable copy. Its post-run hash is `3c953ef2df54177c346b9ea9bf767ab9d894c94f297f1d3fd08f6e0470b00ebc`.

The diagnostic image's preboot hash matched its manifest, as did the writable copy. The first attempt for the intermediate diagnostic image hit the QMP Unix-socket path-length limit and was forcibly stopped before the observation window; a separate retry completed cleanly. This was a runner setup failure, not a guest catalog failure. The final normal-wallpaper run used a short QMP socket path and completed cleanly.

The exact historical cause of the earlier `appd-service-available=false` report was not reproduced by the full trace and is not claimed as a confirmed authorization defect. The recorded final run shows availability, authenticated identity, D-Bus response, model update, and QML counts all succeeding. The delayed initial catalog refresh remains in AppController so model population does not depend solely on a service-availability signal edge. Native app launching was not exercised; Android/Waydroid remains unrelated and untested.

## Native Settings launch readiness — offline candidate, 2026-10-30

The launch route remains the existing Ocean delegate → `AppController` →
`ApplicationsClient` → authenticated appd `Launch` method. appd validates the
fixed manifest executable, applies the existing single-instance handling,
tracks Starting/Running/Stopped state, and reports launch errors. No additional
launcher or daemon was introduced.

Offline review found appd's Wayland readiness check looking in `/run/ocean`,
while `Main.qml` creates `wayland-0` under Ocean's `XDG_RUNTIME_DIR` at
`/run/user/1000`. The candidate aligns appd's runtime directory with the
compositor and session bus. It also updates the session bus's OpenWave-name
ownership policy for the `ocean` account without granting Settings
`org.openwave.Shell`. Settings keeps its existing `trusted-system` profile;
appd starts it as UID/GID 1000 with the `render` supplementary group and
`NoNewPrivs=1`. Ordinary app sandboxing remains unchanged.

The Settings manifest and executable are present in the base candidate, and
the Qt Wayland client library, QPA plugin, XDG shell integration plugin, and
compositor XdgShell QML module are staged. Ocean's launcher and appd compiled
successfully with the OpenWave cross toolchain. Focused host checks passed for
the runtime/socket contract, Settings manifest and bundle, profile rules,
Wayland plugin presence, launch lifecycle coverage declarations, and existing
client/trusted-shell regressions.

Candidate: `out/ocean-native-launch-readiness-20261030/OpenWave-native-launch-readiness.img`.
Its complete SHA-256 and protected inputs are recorded in that directory's
`verification.json`. Offline ext4 checking passed. The guest lifecycle test in
`userspace/appd/tests/runtime.cpp` includes a Settings launch/state assertion,
but execution, Wayland socket permissions and label, surface creation, focus,
minimize/close, and resource release are **NOT TESTED**. No QEMU boot was
performed for this task.

### Authorized native Settings runtime check — 2026-10-30

Exactly one bounded QEMU run used a writable copy of the approved image. The
immutable candidate hash matched the approved SHA-256
`206fb574c0f2d04b166038556a47ff02c9a32569b5f8063ca16455993151c8f1` before the
run and afterward. The kernel and protected production image matched their
preflight hashes. SELinux loaded in enforcing mode; OpenRC ran as PID 1, reached
its runlevels, and started native services including appd, permissiond, Ocean,
and D-Bus. Waydroid was not started. Ocean was visible at 544x960. appd reported
ready with five applications; Ocean's authenticated `ListApplications` call
returned five launchable entries and the model reported three favorites and
two unpinned items.

The earlier runtime report's claim that a VNC click was sent is unsupported
and is corrected here. Its controller report says the Settings click was
**NOT TESTED** because startup-marker detection failed, and the controller
skips its click branch on that failure. No VNC connection or pointer
press/release audit was saved. The identical before/after screenshots do not
prove input delivery. Catalog visibility passed, but Settings launch is **NOT
VERIFIED**; surface creation, focus, close, and cleanup are **NOT TESTED**.

Shutdown is **FAIL / incomplete**. QMP greeting and capability negotiation
passed, but the controller's line-reader thread exited after an idle socket
timeout, so `system_powerdown` timed out waiting for its reply. Serial shows PID
1 receiving a poweroff request and OpenRC stopping native services, but no
clean QEMU shutdown event was confirmed; the controller forcibly terminated
QEMU. The original writable guest image remains preserved with filesystem
errors after forced termination. A separate journal-recovered copy passed
read-only `e2fsck -fn` (SHA-256
`300ca18cb430727b7f329e29ac093846ffc145bdc2ea6c7366cfbaa6550a6ab7`).

The approved candidate, production image, kernel, SELinux policy, and pinned
OpenRC source were not modified. Preboot and post-run hashes, serial/controller
reports, extracted guest status, and screenshots are preserved under
`out/ocean-native-launch-readiness-20261030/runtime-test-20261030/`. The single
boot authorization has been used; no retry was run.

### Interactive native launch validation — 2026-10-30

The old controller's Settings-click assertion was corrected: stale startup
markers had skipped the click path, and no RFB event audit existed. A new
bounded boot used the same approved candidate in a fresh writable copy. The
candidate SHA-256 matched
`206fb574c0f2d04b166038556a47ff02c9a32569b5f8063ca16455993151c8f1`; the
selected kernel matched
`974ca2629588cd32a3eff45a7cfaf102a464b9da18d4f0d801c85225223d2013`, and the
protected production image matched its recorded hash. SELinux enforcing,
OpenRC PID 1, and appd/permissiond/Ocean service startup passed.

The user clicked Settings and reported that its window appeared. appd recorded
an authenticated launch, spawned the Settings executable as UID 1000 in
`openwave_settings_t`, and reported Running. Files also reached Running and
the user reported that it worked. Browser, Gallery, and Water Rouge Gallery
each spawned under the existing sandboxed UID and `openwave_app_t`, then
immediately transitioned to Crashed. This narrows the failure to after appd
spawn for those apps, but does not identify a cause: current appd QA output
does not include the raw child wait status, and no AVC was captured. Their
shared Bubblewrap/UID/cgroup profile is a correlation, not proof of root cause.

The captured Ocean QA snapshot reports 720x400 EGLFS geometry despite the
544x960 QEMU mode request. Surface creation for Settings and the three failed
apps was not independently recorded. The appd launch trace and boot snapshot
are extracted under
`out/ocean-native-launch-readiness-20261030/runtime-interactive-20261030/extracted/`.

QMP acknowledged `system_powerdown` and emitted `POWERDOWN`, but no `SHUTDOWN`
event arrived. OpenRC shutdown stalled at `udevd: waiting for localmount`; the
bounded runner sent TERM. The original guest copy has SHA-256
`5b6a74bfd6c0a77ed45a40c330a758666876a05eb9c1a121368a61d1139d687b` and fails
read-only `e2fsck -fn` because of unclean-termination journal/orphan metadata.
A separate recovered copy passes `e2fsck -fn`. The immutable candidate,
production image, and kernel were left unchanged; this was the only boot.
# GUI launch test readiness and storage review — 2026-10-30

The prior interactive run did not prove GUI launch delivery: its first click
was sent before Ocean's home screen was ready, and subsequent clicks landed on
the wallpaper. The revised `scripts/run-openwave-settings-interactive.py`
waits for the requested framebuffer dimensions, three consecutive stable
captures, Ocean's QA-ready marker (catalog contains Browser, Gallery, and Water
Rouge Gallery, with the drawer open), and confirmed OpenRC service markers
before it offers one manual click per selected target. A timeout stops the
interaction sequence and preserves the report and captures.

QA-only Ocean logging records physical pointer position and DPR, activated
delegate ID/bounds, AppController launch requests, and ApplicationsClient
requests. The host report classifies the strongest observed stage from input
delivery through appd child/exec/run/crash evidence; it does not treat a QMP
pointer command or screenshot as proof of activation. Each target is offered
once, and an ambiguous result stops later targets. The next guest launch test
remains **NOT RUN**; no target binary was compiled and no image was changed.

The next disposable preparation must preserve the newer appd baseline from
`out/ocean-sandbox-child-stage-diag-20261030/OpenWave-sandbox-child-stage-diagnostics.img`,
SHA-256 `f296069a5dbc23584d4bc686f7e6c92ced51f00998c46fcdb1e39d371db13a3f`.
Its packed appd hash is
`824022fbe4f267f263a812afe82de3833aed0a82ac98706680d16ed8bd2cc288`.
The older `a3a968...` image lacks those post-fork corrections and child-stage
diagnostics. A later rebuilt appd must be packed from its new build output and
verified against that output; do not copy the old image's appd into a new
Ocean readiness candidate. See the execution-boundary review in
`userspace/appd/README.md` for subsequent source-only corrections. A new guest
boot requires separate authorization.

Focused host tests passed: GUI PPM/readiness/timeout and target geometry tests,
QEMU raw/qcow2 format selection tests, and a qcow2 overlay isolation test.
`py_compile`, `bash -n run-openwave`, and `git diff --check` passed. A temporary
QEMU storage harness verified that writes to a qcow2 overlay leave its raw
backing candidate unchanged. The interactive runner checks the absolute
backing filename and raw format before use, requests explicit `format=qcow2`,
then materializes the post-run disk for offline ext4 validation. No guest boot
was performed.

## Compiled GUI probes for the appd stability candidate — 2026-10-30

The required probes are compiled and packed in
`out/ocean-appd-stability-20261030/OpenWave-appd-stability-diagnostics.img`,
SHA-256 `568ecbe532eb251b32d9b7288f92a9cf5b1a9f0ae0f8f9fc26e5d558052b5b7d`.
Ocean is `0ba2c313b989c7ad37bc8cff12fd5e84f6afc97e1903cf00bdc4d128583ea233`;
the launch client library is
`409220f2fa45e6c74485501e5fb0d21059a0535e580fb1ea1d3245e8671c72fd`.
Both packed files match their target builds. The candidate retains the newer
`f296069a...` appd baseline and incorporates its two subsequent stability
corrections; see `userspace/appd/README.md` for the complete preparation record.

Readiness now requires actual pixel stability, settled drawer state, a populated
catalog, and a unique complete colored border on the requested live delegate.
The border supplies dynamic framebuffer bounds before each click. Missing,
clipped, duplicate or obscured target markers stop the sequence. Delegate QA
bindings now reference the real context property rather than an undefined root
property. Pointer/delegate/AppController/client/appd stages are retained in the
protected diagnostic logs. The baseline's drawer-start flag and 2,000-line log
capture were already sufficient and remain unchanged. All probes are gated by
the existing QA environment variable.

GUI/QMP/overlay/observer offline checks pass (35 tests); the image passes
`e2fsck -fn`. No guest was started. Actual pointer delivery, correct launch
activation, app surfaces and this candidate's shutdown remain **NOT TESTED**.

The subsequent single authorized run was closed by the user during early
kernel boot, before `/init` and Ocean. No GUI-readiness or app-click assessment
was reached. The materialized disk remains byte-identical to the candidate and
passes `e2fsck -fn`; this run supplies no evidence of an Ocean or appd defect.
See `userspace/appd/README.md` and the candidate's
`runtime-summary-20261030/assessment.json`. No second boot was started.

A separately authorized retry reached enforcing SELinux and OpenRC's Ocean
started marker, then the host controller aborted on QMP `screendump: no surface`
before GUI readiness. The saved VGA boot-text frame is not Ocean geometry, and
no click or application launch was attempted. The host helper now waits for
this specific missing-surface state within its existing bound, with 23 passing
host tests; unrelated QMP errors still fail. The image and native binaries are
unchanged. Actual rendering and target activation remain **NOT TESTED**.
Evidence and current host-controller hashes are linked in the appd README and
candidate verification record. No additional guest boot was performed.

## Manual Browser assessment of the stability candidate — 2026-10-30

Candidate SHA-256 remains
`568ecbe532eb251b32d9b7288f92a9cf5b1a9f0ae0f8f9fc26e5d558052b5b7d`.
The user confirmed Ocean's drawer, green QA badge and outlined Browser, then
clicked Browser once and reported **"Check log, not opened"**. The test stopped
there; Gallery and Water Rouge Gallery were not offered or tested.

QMP `screendump` continued returning `no surface` while the user could see
Ocean. A preceding automatic capture attempt timed out before any app click.
The explicit, default-off `--manual-assessment` controller path was then used
to complete the user's assessment without changing the guest image. Its
separate target-readiness and app-result prompts, no automatic input, and
stop-on-failure behavior pass 25 host tests. Operator confirmation is recorded
as such; measured framebuffer stability and geometry are not claimed. Guest Qt
logs report 720x400, so the requested 544x960 remains unresolved.

The actual appd request from Ocean UID 1000 is preserved. Browser's Bubblewrap
setup and all child checkpoints through Landlock, seccomp and final pre-exec
completed. PID 2334 then died with SIGSEGV; executable handoff and the exact
fault location remain unconfirmed. This is a genuine child-process failure,
independently of the incomplete pointer/delegate capture and the raw
controller's weaker launch classification.

SELinux enforcing, OpenRC and native services started successfully. Shutdown
stalled at the existing udevd/localmount wait and required bounded TERM; it was
not clean. The original writable state is preserved. Its read-only filesystem
log contains unrecovered metadata differences despite exit status 0; a
separate journal-recovered copy passes `e2fsck -fn`. Candidate and protected
production hashes remain unchanged. Full results and exact recovery hashes are
in `userspace/appd/README.md` and
`out/ocean-appd-stability-20261030/runtime-summary-manual-20261030/assessment.json`.

## Rebuilt appd exec-confirmation candidate — manual assessment (2026-10-30)

One bounded run tested the fresh candidate SHA-256
`85e7a560d99901897b8403ae8e18454e8dc09fea28ac1ca30d1213edb0a458e4`,
containing rebuilt appd and the unchanged Ocean binary, client library and QA
drawer. SELinux enforcing and native startup passed. The user confirmed the
visible green QA badge and outlined Browser, then clicked Browser once and
reported **"Check log, not opened"**. Gallery and Water Rouge Gallery were not
tested after that result.

The real Browser request reached appd. PID 2208 completed every setup checkpoint
but died with SIGSEGV after 14,915 microseconds from spawn to reap. The corrected
parent probe reports ENOENT specifically for `/proc/2208/exe`; appd records no
setup failure and preserves the actual terminating signal. The Browser crash
is unresolved; neither executable startup nor a fault location is established.
This test makes no new display-resolution or screenshot claim.

Shutdown required bounded TERM after the existing localmount stall; neither
kernel poweroff nor QMP `SHUTDOWN` was observed. Original writable-state fsck
failed, while a separate recovered copy passed. See the exact hashes and latest
trace in `userspace/appd/README.md` and
`out/appd-exec-confirmation-runtime-20261030/runtime-summary/assessment.json`.
Production artifacts, the previous candidate and the new immutable candidate
remain unchanged by the guest run.
