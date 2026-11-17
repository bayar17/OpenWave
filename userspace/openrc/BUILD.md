# OpenWave PID 1 and OpenRC handoff audit

Audit performed 2026-10-29 from source, the pinned OpenRC checkout, the packed wallpaper-caller-auth candidate, and its prior authorized runtime report. No guest was booted and no image, rootfs, kernel, policy, or upstream source was changed for this audit.

## Findings

The tested candidate is not using OpenRC through a symlink. Its kernel command line selects `/init`; the image contains an OpenWave bootstrap at `/init` and a separate regular `/sbin/openrc-init`. The candidate SHA-256 is `1496506f181743e5804f720256e63e34947af6df4dad54f6cac738e8c5690bd4`. Read-only inspection found packed `/init` is an x86-64 ELF, 24,088 bytes, SHA-256 `c4586f9e8a30a2f8ade312eb285a8a748f5913ae2fc60510bc7274c4bdbed411`; `/sbin/openrc-init` is a separate x86-64 ELF, 19,712 bytes, mode 0755.

The normal build target is `openwave-bootstrap` in `userspace/CMakeLists.txt`; it installs as `/init`. The candidate builder also compiles `userspace/openrc/bootstrap.c` directly into its candidate root. The old supervisor in `userspace/init.c` is explicitly `EXCLUDE_FROM_ALL` and is not installed by normal CMake. The staging tree currently contains a different `/init`: a 42,000-byte ELF with SHA-256 `560bb2e3ef311be834831e9728829834798e05e14e02a7cd76288abd9ef024fa`. This is stale staging relative to the current target definition; it is not the `/init` packed into and booted from the approved candidate. Correct staging during a separately authorized preparation; production files were left untouched for this audit.

## Bootstrap responsibilities and handoff semantics

Before handoff, `userspace/openrc/bootstrap.c`:

- Requires PID 1 and safely creates/validates `/dev`, `/proc`, `/sys`, `/run`, and `/tmp` mountpoints, rejecting symlinks and non-directories.
- Mounts and verifies devtmpfs, procfs, sysfs, selinuxfs, tmpfs for `/run`, `/tmp`, and `/dev/shm`, and devpts. It establishes the selinuxfs mountpoint only after sysfs is mounted and verified.
- Calls `openwave_selinux_boot()`. That routine validates the protected policy file, loads it, sets and verifies configured enforcement, and labels early filesystem paths. It does not call `setcon()` or `setexeccon()`.
- Attaches `/dev/console` to descriptors 0–2, closes the extra descriptor, and sets `PATH`, `HOME`, and the system-bus address.
- Calls `execv("/sbin/openrc-init", ["/sbin/openrc-init", NULL])`; on failure it logs and PID 1 waits in the emergency path.

The bootstrap does not set UID/GID, supplementary groups, capabilities, cwd, session, process group, or namespaces. `execv` replaces the same PID 1 image; it does not fork a second init. It preserves credentials, capabilities, namespaces, cwd, umask, session/process group, signal mask, and open descriptors except close-on-exec descriptors. Caught signal handlers reset on exec. Console descriptors remain 0–2. The environment is preserved, with the three bootstrap values overwritten or added; OpenRC then sets its default `PATH`. Neither step converts kernel command-line tokens into environment variables.

A diagnostic gap is confirmed: QA flags supplied as unknown kernel command-line tokens are not exported as `OPENWAVE_QA_IDENTITY` or `OPENWAVE_QA_SHUTDOWN`. Bootstrap ignores incoming init arguments and execs OpenRC with only its own argv. Thus source diagnostics gated on `getenv("OPENWAVE_QA_IDENTITY")` could not emit the intended reason. This explains missing diagnostic detail, not the wallpaper authorization denial.

The SELinux policy maps the kernel and init initial SIDs to `openwave_system_t`. Bootstrap does not change that process context, so the handoff remains in the same PID and initial domain. Generated executable transitions apply when OpenRC starts labeled services. There is no source evidence that the handoff itself changes Ocean's caller identity or SELinux domain.

## Legacy supervisor versus OpenRC

The retained legacy implementation (`userspace/init.c`, also present at repository commit `7b3e3dd1`) forked and exec'd a hard-coded service array, then reaped/restarted children and implemented its own power-request shutdown path. It did not call `setuid`, `setgid`, or `setgroups` before service exec. Its children therefore inherited PID 1's root credentials and supplementary groups. It set shared `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, system/session D-Bus addresses, `XDG_CURRENT_DESKTOP`, and `XDG_RUNTIME_DIR=/run/ocean`; Ocean additionally received EGLFS variables. The session bus and Ocean ran against the root-created `/run/ocean` runtime. The earliest imported version wrote PID files for Ocean, appd, and promptd, not the present registrar identity format.

Current OpenRC definitions keep appd, permissiond, promptd, wallpaperagentd, portals, and system D-Bus at root by default. Ocean, its session bus, PipeWire, and WirePlumber run as `ocean:ocean` (UID/GID 1000); the account/group configuration includes video and render access. Service definitions supply explicit per-service bus and XDG variables. The registrar invalidates identity records before restart and writes a protected PID, UID, and process-start-time record after the supervised pidfile exists. The retained legacy writer's two-field PID/start-time record cannot satisfy the current three-field trusted-shell verifier.

Both launch paths use `openwave_system_t` as the SELinux transition source for listed native service entrypoints. Generated transitions assign dedicated service types, including `openwave_shell_t` for Ocean. Verified differences include Unix identity, runtime directory/session bus, explicit service environment, dependency handling, and identity-record format—not a second PID 1 or an observed bootstrap SELinux transition.

| Service | Legacy retained supervisor | Current OpenRC |
|---|---|---|
| appd | root; fixed executable; shared environment; legacy record | root; fixed executable; explicit bus/XDG environment; registrar record after pidfile |
| permissiond | root; fixed executable; shared environment; legacy record | root; fixed executable; explicit system/session bus; registrar record after pidfile |
| promptd | root; fixed executable; shared environment; legacy record | root; fixed executable; explicit system/session bus; registrar record after pidfile |
| wallpaperagentd | root; fixed executable; shared environment; legacy record in retained source | root; fixed executable; system-bus default; registrar record after pidfile |
| Ocean | root; fixed launcher; EGLFS overrides; `/run/ocean` environment | UID/GID 1000; fixed launcher; EGLFS and `/run/user/1000` environment; registrar record after pidfile |
| D-Bus | both buses root; root-created session bus | system bus root; session bus runs as Ocean in `/run/user/1000` |

The previous candidate runtime report confirms SELinux enforcing and that OpenRC, appd, permissiond, wallpaperagentd, and Ocean started. It does not establish a wallpaper authorization cause.

## Wallpaper request and authorization

The current path is Ocean → `org.openwave.Wallpaper1` on the system bus → wallpaperagentd → appd's `ResolveWallpaperUser`. wallpaperagentd obtains the original system-bus unique sender's UID/PID and forwards that unique sender. appd authenticates the requester as the supervised wallpaper service, then independently queries the forwarded caller's credentials. It accepts only a live trusted Ocean shell or live managed app.

Trusted-shell checks include configured Ocean UID, unique D-Bus name ownership of `org.openwave.Shell`, protected pidfile owner/PID, protected identity record with UID/start-time match, exact `/proc/PID/exe`, and—when enforcing—matching D-Bus and `/proc/PID/attr/current` labels equal to `system_u:system_r:openwave_shell_t:s0`. No code substitutes wallpaperagentd credentials for Ocean's forwarded sender.

The older wallpaper service also used the system bus, but only resolved caller Unix UID; its `GetCurrent` did not call appd. Since legacy init ran Ocean as root, Ocean selected root wallpaper storage directly. That bypassed the modern trusted-shell/managed-app authorization path; it was not equivalent authentication.

In the authorized OpenRC runtime, the root QA `GetCurrent` succeeded, while Ocean's request was rejected by appd's trusted-shell-or-managed-app check. The exact failed predicate was not logged. Available evidence does not identify whether UID, D-Bus name, identity record, executable, or SELinux context failed. The bootstrap handoff has no demonstrated direct route to alter those checks. Its failure to propagate QA kernel arguments did prevent collecting the sub-check diagnostic. No authentication bypass or speculative correction was made.

## Direct `openrc-init` assessment

The upstream OpenRC 0.64.1 init guide supports `init=/sbin/openrc-init` and documents `openrc-shutdown` for power actions ([pinned init guide](https://github.com/OpenRC/openrc/blob/83fb7385f4e9115a1c804b1c035bfbad04ebc46a/init-guide.md)). Its PID 1 source initializes sysinit, boot, and the configured default runlevel, handles signals, and reaps children. However, this build used `-Dselinux=disabled`; OpenRC's optional built-in SELinux policy-load path is absent. Directly booting the current `/sbin/openrc-init` would skip OpenWave's required policy validation/load/enforcement before services. Direct boot is therefore not safe or ready with the present candidate.

Direct OpenRC boot can be considered only after a supported pre-service path validates and loads this exact OpenWave policy, establishes selinuxfs and required filesystems, and proves correct domain transitions. Possibilities are an upstream OpenRC SELinux-enabled build proven to honor OpenWave's policy root and validation requirements, or an earlier minimal bootstrap (for example in an initramfs) that mounts the real root, validates/loads policy, then execs OpenRC. OpenRC sysinit already owns normal cgroup/system mount work, and `openwave-runtime` provisions OpenWave runtime directories. Do not replace `/init` with a symlink until security preconditions and failure recovery are tested.

## Responsibility inventory

| `/init` responsibility | Classification |
|---|---|
| Require PID 1 and attach console | Essential early boot; keep a fail-closed early path |
| Establish/verify `/dev`, `/proc`, `/sys`, selinuxfs, `/run`, `/tmp`, `/dev/shm`, devpts | Early prerequisites currently established by bootstrap; ordinary mounts may move to OpenRC only if SELinux ordering remains intact |
| Validate/load policy, enforce mode, label early filesystems | Essential before services; not replaced by this OpenRC build |
| Set `PATH`, `HOME`, system-bus address | Bootstrap defaults; OpenRC sets PATH and services set their own environment |
| Mount cgroup2 | Not in current bootstrap; OpenRC sysinit cgroups owns this |
| Create OpenWave runtime/user/persistent directories | OpenRC `openwave-runtime` service |
| Order, supervise, reap, restart, shut down daemons | Legacy responsibility replaced by OpenRC |
| Set Ocean graphics/session bus/user and register identity | Current OpenRC Ocean service and trusted identity registrar |
| Waydroid-specific setup and Android lifecycle | Optional Waydroid services; not needed for native startup |

## Focused host validation

- PASS: `python3 userspace/openrc/tests/test_bootstrap.py` — mount order, fail-closed paths, and exec handoff.
- PASS: `python3 userspace/openrc/validate_openrc.py` — 24 service scripts, acyclic dependency graph, Waydroid optional.
- PASS: `python3 userspace/appd/tests/test_trusted_shell_policy.py` — 8 policy regression checks.
- PASS: `bash userspace/appd/tests/run-trusted-shell-host-test.sh` — live D-Bus credentials, name ownership, executable, protected identity, and wallpaper resolver host tests.
- NOT TESTED: another guest boot, direct OpenRC boot, GUI wallpaper authorization in a new candidate, or clean shutdown.

No implementation correction was required for this audit. The staging `/init` mismatch and inert kernel-argument QA flags are documented findings for separately authorized preparation; neither was changed here.

## Wallpaper authorization diagnostics candidate — 2026-10-29

The earlier diagnostic boot passed `OPENWAVE_QA_IDENTITY=1` and
`OPENWAVE_QA_SHUTDOWN=1` as unknown kernel arguments. The bootstrap intentionally
does not translate kernel command-line tokens into the environment. OpenRC
services therefore never received those values, so environment-gated identity
logs and the localmount-lock watcher stayed off. The public authorization error
was correctly generic; the missing QA-only service environment prevented
identifying its first rejected predicate.

`enable-wallpaper-qa.py` now adds explicit, narrow exports only to a disposable
enforcing candidate when the candidate builder is invoked with
`OPENWAVE_OPENRC_QA_OBSERVABILITY=1`. appd and wallpaperagentd receive
`OPENWAVE_QA_IDENTITY=1`; Ocean receives `OPENWAVE_QA_GRAPHICS=1` and the
`bundled-svg` comparison selector; the default-runlevel QA observer receives
`OPENWAVE_QA_SHUTDOWN=1`. The wallpaper service's QA stdout/stderr go to
root-private `/run/openwave` logs with umask 0077, which the existing observer
collects. Production service sources contain no diagnostic exports. The helper
requires an enforcing SELinux configuration, an installed observer, and
regular service files, and is idempotent.

appd's trusted-shell check now emits QA-only stable stage codes for request and
configured UID, D-Bus name-owner lookup, sender UID and PID lookups, protected
pidfile, identity-record lookup and PID/UID comparisons, process start-time
comparison, exact executable, SELinux identity, and final authorization. The
actual wallpaperagentd sender UID/PID lookup and unchanged original unique
sender are logged under QA. Existing D-Bus public errors remain generic and
callers still cannot supply a PID or UID as proof. The registrar remains the
trusted writer: it accepts Ocean as UID/GID 1000 at the fixed launcher path,
checks the live pidfile/executable/UID/start time, atomically writes
`/run/openwave/ocean.pid.identity` as root:root 0600, and invalidates the record
before service start and after stop. The existing host identity tests cover
valid Ocean registration, UID tampering, stale PID/start time, executable
mismatch, and idempotent invalidation.

The final candidate is
`out/ocean-wallpaper-qa-diagnostics-20261029/OpenWave-wallpaper-qa-diagnostics.img`.
It was cloned from the previously verified wallpaper-caller-auth candidate and
updated only with freshly built appd/wallpaperagentd, a freshly compiled
`userspace/openrc/bootstrap.c` `/init`, and the candidate-only QA service
overlay. Candidate SHA-256 is
`f301e4aa7ea7b084260c766b887f4cdb9fdd69afe17504981591adc34debef95`.
`/init` built to SHA-256
`c4586f9e8a30a2f8ade312eb285a8a748f5913ae2fc60510bc7274c4bdbed411`; the packed
`/init` was extracted and matched byte-for-byte. The checked-in `rootfs/init`
remains the known legacy ELF (`560bb2e3ef311be834831e9728829834798e05e14e02a7cd76288abd9ef024fa`)
and was neither copied over the candidate nor modified. The candidate builder
now enforces this packed-bootstrap identity check after image creation.

Packed appd SHA-256 is
`1fe6b60d53d5ff8cdb167e99036988ccd19baa718ff06ddc485a66adda81949c` and
wallpaperagentd SHA-256 is
`e041080f4b73d0ab5999420baecd3303853943c0fc5cc42c8bbf2b9421c8747d`; both
match the cross-build outputs. The packed Ocean launcher remains
`c03ea8e2e1ec3c1793e07a7a5ee62a4fca37061e0f22f26ef977adf1e294165d` and
matches the unchanged base candidate. SELinux remains enforcing; policy
`fd941372c4ece05bee539c963a5a265cd04b56ed4bd85b118c36fd722764ec2d`, file
contexts `fb13306e548ea57ef6434608d1858971a150b33fd269a3ec31c27fbceac179e0`,
and the seeded default wallpaper are unchanged. Full hashes and packed service
contents are recorded in the candidate's `verification.json`.

Focused validation passed: QA environment overlay/idempotence and production
default-off tests; Ocean registration and permissiond identity tests; live
D-Bus trusted-shell/managed-app host tests; OpenRC service dependency
validation; observer and Ocean service shell checks; packed `/init` exact-match
and mismatch tests; ELF interpreter checks; and `e2fsck -fn`. OpenRC remains
clean at pinned commit `83fb7385f4e9115a1c804b1c035bfbad04ebc46a`. Production
`OpenWave.img`, `rootfs`, kernel, SELinux policy, previous candidate, and pinned
upstream source remain unchanged. No QEMU boot was performed. The exact Ocean
trusted-shell predicate and wallpaper rendering remain **NOT TESTED** until a
separately authorized runtime test.

## Caller-authentication diagnostic boot preflight — blocked (2026-10-29)

The approved diagnostic image hash was rechecked and matches
`f301e4aa7ea7b084260c766b887f4cdb9fdd69afe17504981591adc34debef95`.
Its packed `/init`, enforcing SELinux config, policy, file contexts, appd,
wallpaperagentd, Ocean launcher, and seeded wallpaper match the hashes in
`verification.json`. The packed OpenRC services contain the requested QA
environment, daemon log paths, and observer log collection; Waydroid is absent
from the default runlevel. The kernel hash matches the recorded input and the
pinned OpenRC checkout is clean at the recorded commit.

The boot was **NOT RUN** because the observer's evidence path is not shown to
be writable under the packed enforcing policy. `/var/log/openwave-qa` matches
the generic `/var(/.*)?` label `openwave_private_t`; the observer starts in the
documented OpenRC system domain and creates that directory and its status and
lock-owner logs. The packed policy source grants `openwave_system_t` no
create/write access to `openwave_private_t`. The appd and wallpaperagentd
diagnostic logs are also placed under `/run/openwave`, whose generic label is
`openwave_runtime_t`; the recorded policy grants `openwave_system_t` directory
search/getattr there, but no file create/write permission. This is an offline
policy/configuration incompatibility, not a runtime AVC observation. As a
result, this candidate cannot yet be relied upon to preserve the evidence
required to identify the caller predicate or shutdown-lock owner. No policy,
source, or image correction was made under this no-rebuild authorization.

The current staging file `rootfs/etc/selinux/config` says `SELINUX=permissive`
and differs from the manifest hash. The approved image's packed config itself
is `SELINUX=enforcing` and matches the manifest; `rootfs` is not used by this
boot. Production/staging files and the approved image were left untouched.
Ocean authentication, wallpaper rendering, and shutdown-lock ownership remain
**NOT TESTED**. A corrected diagnostic candidate and a new runtime authorization
are required before another boot.


## SELinux-compatible wallpaper diagnostic evidence candidate — 2026-10-29

The failed preflight identified a logging-context problem, not an authentication
failure. OpenRC 0.64.1 `supervise-daemon` opens configured output/error files
with `O_WRONLY|O_CREAT|O_APPEND` and mode 0600 before spawning each service.
Those descriptors are inherited by the service and writes are checked in the
writer's current SELinux domain. OpenWave's bootstrap transitions the services
to distinct domains: appd to `openwave_appd_t`, wallpaperagentd to
`openwave_wallpaper_t`, and Ocean to `openwave_shell_t`. The OpenRC supervisor
and QA observer use `openwave_system_t`. appd and wallpaperagentd have no
`command_user`, so OpenRC starts them as root; Ocean is explicitly
`ocean:ocean` (UID/GID 1000). These are configuration-derived identities;
the live guest identities remain unverified until boot.

The normal appd and wallpaper logs under `/run/openwave` were labeled
`openwave_runtime_t`; Ocean's log location under `/run/user/1000` was not
writable by its shell domain either. The observer's `/var/log/openwave-qa`
path matched the broad private-file context and the system domain lacked the
needed creation/write permissions. `/dev/console` was not a usable common
alternative: the compiled policy does not grant these service domains console
writes, so routing the evidence there would either lose records or require
additional console policy. The candidate instead uses a dedicated
`openwave_qa_evidence_t` label only for `/var/log/openwave-qa(/.*)?`. Its
root-owned directory is mode 0700; OpenRC creates producer log files as root
mode 0600, and producer processes append through inherited descriptors. The
system domain can manage evidence files and search the existing private parent
directories; appd, wallpaperagentd, and Ocean receive only file append/write
access to the dedicated evidence type plus `fd use` against the supervisor's
`openwave_system_t` domain, which is required to write through inherited log
descriptors. No write permission to
`openwave_private_t` or `openwave_runtime_t` was added, and no authentication
predicate or caller-facing error was changed.

`inspect_qa_policy.c` parses the compiled policy with libsepol and verifies the
three producer-domain grants, absence of unrelated private/runtime file write
grants, and the existing process transitions. The matching file-context rule
is present in the packed context database. This offline inspection does not
prove runtime labeling or successful writes; AVC and log delivery checks still
require the separately authorized guest boot.

The enforced-config mismatch was real in staging: `rootfs/etc/selinux/config`
currently says `SELINUX=permissive` (SHA-256
`6b4ed92ec352a750324c9f9909d00f2dfc92a7325cd9cf193675a5a1bc704635`), while
the authoritative `userspace/security/selinux/config` says enforcing. The
production rootfs was left untouched. The candidate builder now fails unless
the authoritative policy config and pinned base image both say enforcing; it
then requires the assembled candidate config to match the policy staging config
byte-for-byte and rechecks the exact enforcing setting before packaging. This
candidate was cloned from the enforcing diagnostic image, not assembled from
the permissive production staging tree. Do not use the current `rootfs` as an
enforcing image source until its config discrepancy is deliberately resolved.

Fresh disposable candidate:
`out/ocean-wallpaper-qa-evidence-20261029/OpenWave-wallpaper-qa-evidence-final.img`.
SHA-256: `2640a00c1670d071e5c060f92f21c0f33d8df51ce0cf875c209359f504b07291`.
The compiled policy SHA-256 is
`ca8d1bc136b7586b5b164d6a0d8a9842e7ac56cd346ece9991ab3bffdeec5de0`.
The earlier same-task image without the inherited-descriptor permission is
preserved as `OpenWave-wallpaper-qa-evidence-pre-fd-use.img` and is superseded.
The candidate is enforcing and includes the QA-only service exports, dedicated
log destinations, updated policy and file contexts, and observer checks for
the evidence directory owner/mode and shutdown lock state. `verification.json`
records the protected input and packed artifact hashes. The preserved base
candidate, production image/rootfs, kernel, and clean OpenRC 0.64.1 checkout
remain unchanged. Read-only `e2fsck -fn` passed.

Focused validation passed: compiled-policy inspection; candidate OpenRC
validation (39 enabled services, 20 command ELFs, 8 disabled services); host
OpenRC dependency validation (24 scripts, acyclic graph, Waydroid optional);
QA overlay (4 tests); observer (3 tests); trusted-shell policy (8 tests); live
trusted-shell identity (2 TAP cases); packed observability checks; bootstrap
identity verification; Python/shell syntax checks; and `git diff --check`.
No QEMU boot was performed. Actual SELinux labels after restorecon, service
stdout/stderr delivery, the first rejected Ocean identity predicate, wallpaper
rendering, and shutdown lock ownership remain **NOT TESTED**.

## Authorized enforcing wallpaper diagnostics boot — 2026-10-29

Exactly one boot used a distinct writable copy of
`out/ocean-wallpaper-qa-evidence-20261029/OpenWave-wallpaper-qa-evidence-final.img`.
The approved image hash matched before boot:
`2640a00c1670d071e5c060f92f21c0f33d8df51ce0cf875c209359f504b07291`.
The packed enforcing policy hash matched
`ca8d1bc136b7586b5b164d6a0d8a9842e7ac56cd346ece9991ab3bffdeec5de0`; the packed
file-context database hash was
`ec3abc36ea71d56c069100c75cf5239302419ce8f05455ad8b455f0f025b71a2`. The
recorded kernel hash matched `974ca2629588cd32a3eff45a7cfaf102a464b9da18d4f0d801c85225223d2013`.
OpenRC remained clean at pinned commit
`83fb7385f4e9115a1c804b1c035bfbad04ebc46a`. The original candidate and
production image were not written by QEMU.

**Boot and core services — PASS.** `/init` loaded policy and the kernel audit
record reported `enforcing=1`; OpenRC 0.64.1 ran as PID 1 and reached its
runlevels. System and session D-Bus sockets were present. appd was ready,
permissiond authenticated appd's registered process identity, and
wallpaperagentd owned `org.openwave.Wallpaper`. Waydroid was disabled. The QA
directory and captured appd, wallpaperagentd, and observer files had the
expected runtime SELinux xattr `system_u:object_r:openwave_qa_evidence_t:s0`.
Nonempty `boot-status.txt`, appd stdout/stderr, and wallpaperagentd stderr
records were preserved. The wallpaper stdout file was empty.

**Ocean startup — FAIL before wallpaper authentication.** OpenRC reported
`start-stop-daemon` could not redirect stdout to
`/var/log/openwave-qa/ocean.stdout.log` (`Permission denied`), then marked
`openwave-ocean` failed. `org.openwave.Shell` was not registered; no Ocean
pidfile, identity record, process environment, wallpaper request, or QML image
state was produced. The directory was root-owned mode 0700. The service used
`command_background="yes"` with `command_user="ocean:ocean"`; pinned OpenRC's
`start-stop-daemon` changes target credentials before opening its output
redirect, so UID 1000 cannot traverse the root-only directory. This is the
first confirmed blocker and is consistent with a DAC `EACCES`; the captured
serial log contains no AVC identifying an SELinux denial. Preserve the
root-only directory and arrange for a root supervisor to open and pass the
protected log descriptors before dropping Ocean's UID. No permission or policy
was changed during this test.

The nine trusted-shell checks for an actual Ocean request are all **NOT
REACHED**: D-Bus sender verification, UID/PID resolution, executable check,
identity-record lookup, recorded PID match, process-start-time match, SELinux
context check, trusted-shell authorization, and managed-application fallback.
This boot therefore does not identify Ocean's first rejected identity
predicate. Separately, the root QA observer's calls were not Ocean: its
wallpaperagentd log entries identify UID 0 and PIDs 2025/2032, and appd logged
root-originated checks whose first failing configured-session predicate was
`session-uid-mismatch`. Those results must not be attributed to Ocean.

The observer's root `GetCurrent` returned the seeded default path and digest;
wallpaperagentd logged `GetCurrent` and `GetImage` requests from the root
observer. `GetImage` returned to the observer, but payload classification was
**NOT RECORDED** because guest commands `tr` and `wc` are missing from PATH.
Preflight checked the BusyBox applet build list but failed to verify command
links in the packed image. QEMU was configured for 544x960, but Ocean and its
framebuffer were not reached; no screenshot was captured. A second QMP client
could not obtain a greeting while the bounded runner owned the active QMP
connection.

**Shutdown — FAIL; lock cause inconclusive.** The runner delivered QMP
`system_powerdown`; PID 1 entered shutdown and serial output showed
`udevd: waiting for localmount` at 50, 40, 30, 20, and 10 seconds. The runner
observed no QMP `SHUTDOWN` event and forcibly terminated QEMU after its bounded
window. Its process exit code was 0, but the run was forced and is not a clean
guest shutdown. The opt-in lock watcher started, but
`localmount-lock-owner.log` remained empty, so no lock path, holder, or
operation was identified.

The post-run writable copy was preserved at
`out/ocean-wallpaper-qa-evidence-20261029/runtime-test-20261029/guest-writable.img`
with SHA-256
`2bcd19995e0497ac0b03fffd1e005b5d2946201e4184c560d4d6c0d66e3b41a5`. Read-only
`e2fsck -fn` skipped journal replay and reported post-crash free-count/orphan
metadata differences. A separate recovery copy was journal-recovered and
then passed read-only `e2fsck -fn`; its hash is
`5c8c6ce0d333b3e239c1a56a7641da417d73c5a384ad6e2da37cba99e19479c8`. The
unrecovered original remains preserved. Serial/QMP reports and extracted guest
evidence are under
`out/ocean-wallpaper-qa-evidence-20261029/runtime-test-20261029/`. No source,
policy, candidate, production image, or upstream component was changed, and no
second boot was run.


## Ocean QA logging correction — 2026-10-30

The confirmed `EACCES` was DAC path traversal, not an observed SELinux AVC.
Pinned OpenRC 0.64.1's `start-stop-daemon` changes to `command_user` before it
opens `--stdout` and `--stderr` with `O_CREAT|O_APPEND`. Ocean therefore tried
to create its logs as UID 1000 inside `/var/log/openwave-qa`, which is correctly
root-owned mode 0700. That prevents traversal before any wallpaper caller-
authentication check runs.

Ocean now logs only to `/run/user/1000/openwave-logs/ocean.stdout.log` and
`ocean.stderr.log`. `openwave-ocean` requires `openwave-runtime`; the runtime
service safely provisions the directory as `ocean:ocean` (1000:1000), mode
0700, before its dependents start. Its compiled runtime checker rejects a
symlink or wrong owner, group, mode, or SELinux context. OpenRC's `umask=0077`
and output creation mode produce private mode-0600 files. POSIX mode semantics
give UID 1000 directory access and deny traversal to unrelated UIDs; a live
cross-UID `start-stop-daemon` invocation was not possible on the host because
the host has no `ocean` account and the target binary uses guest runtime
libraries. The pinned source-order regression verifies the credential change
precedes both log opens.

The dedicated context is
`system_u:object_r:openwave_qa_evidence_t:s0`. OpenRC assigns it at creation
through a filename-specific type transition for `openwave-logs` beneath the
runtime type; the supervisor receives no general directory relabel permission.
The supervisor can create and read the diagnostic files. Ocean's domain can
append to its inherited stdout/stderr descriptors but cannot traverse the
evidence directory. appd, wallpaperagentd, and the observer continue using the
root-owned `/var/log/openwave-qa` directory (verified as root:root 0700 in the
packed ext4 image); Ocean is not routed there.

The observer separately validates the Ocean directory and exact log files for
owner, mode, regular-file type, and absence of symlinks before collecting
them. `GetImage` validation now parses the full GVariant byte array, reconstructs
the payload with the packaged BusyBox `xxd`, checks the PNG signature, and
compares its SHA-256 with `GetCurrent`. A target BusyBox fixture passed valid
decoding/signature checks and detected a changed-payload hash. The shutdown
watcher remains bounded to 900 seconds and records lock inode, holder PID,
process state, command, active OpenRC state, and mount information; its actual
guest lock holder and the existing `udevd`/`localmount` stall remain untested.

The fresh disposable candidate is
`out/ocean-wallpaper-qa-logfix-20261029/OpenWave-ocean-qa-logs-final-policy.img`
(SHA-256
`14a72479c8b0f88c8e47343becbd74433e203cce00ff110354bbdd865ee4129c`). Its
compiled enforcing policy is SHA-256
`79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6`; the
packed file-context source hash is
`3121b588e6eb532d41b459904b63fcc5797a68d279757e8869238355e642ecb1`. The
packed `/init` and runtime checker match their build outputs, and the packed
OpenRC service, observer, context map, and enforcing config were inspected.
The final policy inspector confirms the named transition, no general relabel
permissions, preserved service domains, and narrow inherited-FD write access.
Read-only `e2fsck -fn` passes. Candidate details and protected input hashes are
in `out/ocean-wallpaper-qa-logfix-20261029/verification.json`.

Focused tests pass: Ocean logging (7), QA overlay (4), observer (4, including
the GetImage parser fixture), candidate validation (39 enabled services, 20
command ELFs, 8 disabled), OpenRC dependency validation (24 scripts, acyclic),
target BusyBox payload verification, policy compilation/inspection, shell and
Python syntax, packed-content comparison, and `git diff --check`. No QEMU boot
was performed. Runtime creation/labeling, log delivery under Ocean's UID,
observer access inside the guest, trusted-shell authentication, wallpaper
rendering, shutdown behavior, and the lock holder are **NOT TESTED**.

## Ocean QA logging candidate runtime test — 2026-10-30

One bounded QEMU run tested a writable copy of
`out/ocean-wallpaper-qa-logfix-20261029/OpenWave-ocean-qa-logs-final-policy.img`.
The immutable candidate hash matched its recorded value before the run:
`14a72479c8b0f88c8e47343becbd74433e203cce00ff110354bbdd865ee4129c`. The
approved kernel hash matched at `out/linux/arch/x86/boot/bzImage`:
`974ca2629588cd32a3eff45a7cfaf102a464b9da18d4f0d801c85225223d2013` (the
runner's default `out/linux-waydroid-confinement` kernel is a different build
and was not used). The pinned OpenRC checkout remained clean at
`83fb7385f4e9115a1c804b1c035bfbad04ebc46a`; production `OpenWave.img` remained
`d29da5f99fb3a8fcf6fb6717e9f66293fe47b83cb7ef846088a59c8246dbb1a3`.

**Boot and native services — PASS.** The kernel loaded the OpenWave SELinux
policy and audit recorded `enforcing=1`; `/init` reported policy load, labeling,
and enforcing mode. OpenRC 0.64.1 ran as PID 1 and reached the default runlevel.
The guest runtime directory checker verified ownership, modes, and SELinux
labels. eudev, system and session D-Bus, appd, permissiond, promptd,
wallpaperagentd, portal services, audio services, and Ocean started. appd's
ready marker and catalog API responded, and permissiond recorded successful
authentication of appd. Waydroid remained disabled.

**Ocean log delivery — PASS.** The captured `boot-status.txt` reports both
`/run/user/1000/openwave-logs/ocean.stdout.log` and `ocean.stderr.log` as
`1000:1000`, mode `0600`, and includes their content. The process snapshot shows
`ocean-launcher` running as UID/GID 1000 with zero effective capabilities and
`NoNewPrivs=1`. Ocean emitted its QPA/QML diagnostics into the expected files;
the observer read them successfully. The wallpaper API itself was responsive:
the root observer obtained `GetCurrent`, decoded `GetImage`, verified its PNG
signature, and matched the image digest.

**Ocean integration — FAIL.** Ocean's own wallpaper `GetCurrent` requests were
rejected with `org.openwave.Wallpaper.Unauthorized` because the caller was not
recognized as the trusted shell or a live managed application. Ocean reported
`appd-service-available=false`, zero apps, and an exposed window of `720x400`.
The connector's available modes included `544x960`, but the captured window
size did not match that requested resolution. Thus this run validates the
logging repair while confirming the trusted-shell/appd caller path and actual
display sizing remain unresolved. The observer could not read
`/sys/fs/selinux/enforce` (`selinux_enforce=unavailable`) and process SELinux
contexts were unavailable in its status file; enforcing is confirmed by the
kernel audit record and `/init` output. Serial output contained no AVC record.

**Shutdown — FAIL; filesystem recovery required.** The bounded runner used one
QMP connection, delivered `system_powerdown`, and observed OpenRC stop native
services. Shutdown stalled at `udevd: waiting for localmount`; no QMP `SHUTDOWN`
event arrived in 60 seconds, so the runner forcibly terminated QEMU. This was
not a clean guest shutdown. The preserved writable test image has SHA-256
`6417367cd046b52344dd2cc9648eb167e3720d565bb250cb7c89228c574ac700`; its
read-only `e2fsck -fn` reported journal-skipped free-count/orphan metadata
differences. A separate recovery copy was journal-recovered and passed
`e2fsck -fn` with SHA-256
`0288d4dcfd20a65656407e671d72d2991cccc7a448cef08db987c14efa00c5e9`.

Serial log, QMP report, extracted guest QA status, original writable test copy,
and recovered copy are under
`out/ocean-wallpaper-qa-logfix-20261029/runtime-test-20261030/`. The tested
candidate, production image, production rootfs, kernel, and pinned OpenRC source
were not modified. No additional boot or speculative fix was attempted.

## Ocean shell-bus identity and shutdown observation candidate — 2026-10-30

The prior runtime exposed the exact owner mismatch: wallpaper's original
sender was `:1.27` (UID 1000, PID 1509), while appd reported that a different
unique connection owned `org.openwave.Shell`. In source, ApplicationsClient
had made a private GDBus connection for shell-name registration and appd RPC;
WallpaperClient used GLib's shared `g_bus_get_sync(G_BUS_TYPE_SYSTEM)`
connection. The appd client failed its trusted-shell precheck before issuing
`ListApplications`, explaining Ocean's unavailable state and zero model while
the observer successfully queried appd's five-entry catalog.

ApplicationsClient now uses the shared GDBus system connection, waits for its
initial connection and shell-name registration attempt before returning, and
only unreferences (never closes) that shared connection. Ocean constructs the
ApplicationsClient through AppController before constructing WallpaperClient.
appd still enforces owner-equals-original-sender followed by protected
pidfile/identity record, PID/UID/start time, executable, and SELinux identity
checks. QA-only records include the shell owner and UID/PID alongside the
original sender and credentials. The wallpaper QA client reports its sender
name and whether that name owns the shell identity. No UID-only shortcut or
second identity mechanism was added.

The QA shutdown observer now runs a synchronous lock snapshot from its OpenRC
`stop` hook. Before `localmount` teardown, it records the lock file's presence
and inode, matching `/proc/locks` entries and holder process details when
visible, OpenRC state files, and mountinfo. It still does not remove locks or
alter OpenRC. This is an evidence correction; the holder and reason for
`udevd: waiting for localmount` remain guest-only questions.

The last guest advertised DRM mode `544x960` and the Ocean service passed
`/etc/openwave/kms.json` through `QT_QPA_EGLFS_KMS_CONFIG`, but Qt's exposed
window was `720x400`. Qt's documented KMS JSON schema accepts the installed
connector/mode form ([Qt EGLFS/KMS configuration](https://doc.qt.io/qt-6/embedded-linux.html));
the saved runtime log has no active-mode or parse diagnostic, so the specific
mode-set failure is **NOT DETERMINED**. The disposable overlay now enables
Qt's EGLFS/KMS debug logs for the next authorized boot.

Host validation: GTestDBus shared-owner test and same-process distinct-connection
rejection (2 cases); appd identity policy checks (9); QA overlay (4); observer
checks (4); OpenRC candidate validation (39 enabled services, 20 command ELFs,
8 disabled); OpenRC service-graph validation (24 scripts, acyclic); candidate
boot-observability validation; target builds for appd and the two client
libraries; packed artifact hash comparison; image packing and `e2fsck -fn`;
shell syntax and `git diff --check` all **PASS**.

Fresh enforcing candidate:
`out/ocean-shell-bus-fix-20261030/OpenWave-ocean-shell-bus-fix-final.img`
(SHA-256
`028de33c9dab6b76a09ef8d3c5cc37c81a16c3b60c45e436e60aa9cb184e3cc5`). Full
artifact/protected-input hashes are in
`out/ocean-shell-bus-fix-20261030/verification.json`. It retains the prior
Ocean launcher and wallpaperagentd binaries, updates appd and
`libopenwave-applications`/`libopenwave-wallpaper`, preserves the enforcing
policy hash `79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6`,
and keeps Waydroid disabled. The previous candidate, production image/rootfs,
kernel, and clean pinned OpenRC checkout are unchanged. QEMU was **NOT RUN**.

Guest verification remains **NOT TESTED** for the shared sender/owner in the
actual daemon, downstream appd identity checks, successful Ocean catalog and
wallpaper delivery, actual 544x960 active KMS mode, shutdown lock holder, and
clean shutdown.

## Authorized shell D-Bus ownership runtime validation — 2026-10-30

Exactly one bounded QEMU boot used a writable copy of the approved candidate
`out/ocean-shell-bus-fix-20261030/OpenWave-ocean-shell-bus-fix-final.img`.
The immutable image hash matched the approved SHA-256
`028de33c9dab6b76a09ef8d3c5cc37c81a16c3b60c45e436e60aa9cb184e3cc5`; the
writable copy matched before boot. The recorded kernel hash matched
`974ca2629588cd32a3eff45a7cfaf102a464b9da18d4f0d801c85225223d2013`, production
`OpenWave.img` remained at
`d29da5f99fb3a8fcf6fb6717e9f66293fe47b83cb7ef846088a59c8246dbb1a3`, and the
preserved base candidate matched its recorded hash. OpenRC was clean at pinned
commit `83fb7385f4e9115a1c804b1c035bfbad04ebc46a`.

**Boot, policy, and native services — PASS.** The kernel audit record reported
`enforcing=1`; `/init` logged policy load, labeling, and enforcing mode. OpenRC
reached its default runlevel. The guest QA snapshot reports D-Bus, eudev,
appd, permissiond, promptd, wallpaperagentd, portal services, audio services,
and Ocean running. Waydroid remained disabled. permissiond verified appd's
supervised identity.

**Shell name ownership and wallpaper caller authentication — PASS.** Ocean's
system-bus sender was `:1.24` (UID 1000, PID 1493), and appd's owner lookup
reported `org.openwave.Shell` owner `:1.24`. The caller's protected pidfile,
identity record, PID, UID, start time, executable, and SELinux identity all
verified; appd accepted the trusted-shell request. Ocean's `GetCurrent` and
`GetImage` completed; the returned PNG signature and SHA-256 matched, and the
QML wallpaper source reached `Ready`. This confirms the original private-vs-
shared D-Bus connection mismatch is corrected in this candidate.

**Application catalog — FAIL, separate unresolved path.** The root QA observer
successfully called `ListApplications` and received five entries, but Ocean
reported `appd-service-available=false` and its catalog model remained at zero
apps. The boot snapshot does not record the exact failed Ocean-side appd
transport verification predicate. The wallpaper trusted-shell check passing
does not establish that the ApplicationsClient/appd API path works.

**Display — PASS for geometry; screenshot NOT CAPTURED.** EGLFS/KMS logs show
the 544x960 mode being set, Ocean's QWindow visible and exposed at 544x960, and
the first frame swapped. QMP `screendump` returned `no surface`, so no visual
screenshot was obtained.

**Shutdown — PASS by guest poweroff evidence; QMP event NOT OBSERVED.** QMP
accepted `system_powerdown` and emitted `POWERDOWN`. OpenRC stopped native
services; serial showed `udevd: waiting for localmount` countdowns, followed by
filesystem unmounts and kernel `reboot: Power down`. QEMU exited with status 0.
The QMP `SHUTDOWN` event was not observed; the guest's serial poweroff and
successful process exit are the basis for classifying this as a clean guest
poweroff. The synchronous observer found no localmount lock file at its stop
snapshot while localmount still appeared started, so the wait's holder/cause
remains unidentified. Read-only `e2fsck -fn` passed without errors.

The post-run writable copy hash is
`b7cafbeb6a90e9270827c3cea334fbba7104eef1984b16d8c82d7005c4857759`; the
approved candidate itself was not booted writable or changed. Serial, QMP
controller report, extracted QA logs, and the writable guest image are under
`out/ocean-shell-bus-fix-20261030/runtime-test-20261030/`. No second boot was
performed.

## Sandboxed application crash-diagnostics candidate — 2026-10-30

The disposable appd diagnostics candidate is
`out/ocean-sandbox-crash-diagnostics-20261030/OpenWave-sandbox-crash-diagnostics.img`
(SHA-256
`a3a968082fe823b6e2a9e9fa86b2d3b61f42e2fa0a9081c2a507b1bbec19ed4d`). It was
copied from the previously verified native-launch candidate
`out/ocean-native-launch-readiness-20261030/OpenWave-native-launch-readiness.img`
(SHA-256
`206fb574c0f2d04b166038556a47ff02c9a32569b5f8063ca16455993151c8f1`). The
packed replacements are limited to `/System/Services/appd` and
`/etc/init.d/openwave-qa-observer`; both are root-owned mode 0755. The appd
service still exports the opt-in `OPENWAVE_QA_IDENTITY=1` and writes into the
root-only `/var/log/openwave-qa` evidence directory. The observer collects
2,000 lines at startup and again in its stop hook, preserving interactive app
launch/termination records produced after the initial snapshot.

The QMP runner and bounded shutdown code were not modified. Host QMP tests
passed. The `openwave-qa-observer` source passed its OpenRC/BUSYBOX shell and
shutdown-log preservation fixture. A broad image validation helper was not a
match for this baseline's Ocean log destination (the helper expects the later
wallpaper QA overlay); the candidate's appd and observer bytes, modes/owners,
enforcing config, SELinux policy, and file-context hashes were verified
directly. The policy and contexts remain at the recorded hashes
`79d24e3ec7f8a4102ce55f8aff1b4fea7ce367aaff285d506084e348ca06c6b6` and
`3121b588e6eb532d41b459904b63fcc5797a68d279757e8869238355e642ecb1`.
Read-only `e2fsck -fn` passed. The candidate and inputs are summarized in
`out/ocean-sandbox-crash-diagnostics-20261030/verification.json`.

No QEMU run was authorized or performed for this candidate. OpenRC startup,
SELinux runtime labels, service-log writes under enforcing, actual app child
setup, exec/loader behavior, Qt startup, Wayland surfaces, and graceful
shutdown are **NOT TESTED**.

## Sandboxed application crash-diagnostics runtime test — 2026-10-30

One bounded QEMU boot used the candidate above after its SHA-256 matched
`a3a968082fe823b6e2a9e9fa86b2d3b61f42e2fa0a9081c2a507b1bbec19ed4d`. The
kernel, baseline image, and production image matched the hashes in
`verification.json`; OpenRC remained clean at the pinned 0.64.1 commit. QEMU
used a separate writable copy. The immutable candidate hash remained
`a3a968082fe823b6e2a9e9fa86b2d3b61f42e2fa0a9081c2a507b1bbec19ed4d` after
the run.

**Boot and native shell — PASS.** `/init` logged policy load, filesystem
labeling, and enforcing mode. OpenRC 0.64.1 started as PID 1. Its serial format
was `* Starting <service> ... [ ok ]`; appd, permissiond, and Ocean all reached
that status. appd reported ready with five applications. The QA status file
records Ocean visible and exposed at 544x960, with five catalog entries.

**Application launch diagnosis — NOT CONFIRMED.** The runtime controller
incorrectly waited for the legacy `OpenWave started <service>` text and timed
out, although OpenRC's `[ ok ]` records showed that the services had started.
During the same boot, three VNC pointer events were transmitted at the Browser,
Gallery, and Water Rouge Gallery icon coordinates. RFB provides no guest input
acknowledgement. Captures after each click still showed Ocean's home screen;
no mapped application surface was observed. Recovered appd stderr and the QA
boot status contain no `OCEAN_QA launch-result` or child lifecycle records for
these apps, so the evidence does not prove that Ocean handled the clicks or
requested appd launches. Consequently, no application setup/exec result or
first application failure can be attributed from this run. The per-app input
and capture evidence is in
`out/ocean-sandbox-crash-diagnostics-20261030/runtime-test-20261030/manual-vnc-interactions.json`.

**Shutdown — FAIL; filesystem recovered separately.** QMP accepted
`system_powerdown`; OpenRC stopped services and then `udevd` waited on
`localmount`, counting down from 50 to 40 seconds. No QMP `SHUTDOWN` event was
observed. The bounded controller used its TERM fallback; this was not a clean
guest shutdown. The original writable copy is preserved with SHA-256
`6561b4e687d211d071b4b41c1891d8d58c77fd02dfe12b98900a1a0283fd97ba`; its
read-only `e2fsck -fn` found journal and metadata differences. A separate copy
was journal-recovered and then passed `e2fsck -fn`; its SHA-256 is
`1bad13ff430ecd205c6133040c55044ceb26a51004bf8d9c607e689eb46e4fcb`. The
localmount lock observer produced no holder record.

Serial, QMP, screenshots, original writable image, recovered image, and
extracted QA logs are under
`out/ocean-sandbox-crash-diagnostics-20261030/runtime-test-20261030/`; the
runtime report records the controller marker mismatch and results. Production
OpenWave, the approved candidate, kernel, policy, and prior baseline were not
modified. No second boot was performed. The appd crash protocol remains
**NOT TESTED at runtime** because the GUI launch request was not observed.
## GUI diagnostic storage and copy-on-write review — 2026-10-30

Host storage had 693,620,736 bytes available at this inspection. No QEMU process
was running and no open file handle was found for the inspected guest images.
Correction: the current child-stage baseline is
`out/ocean-sandbox-child-stage-diag-20261030/OpenWave-sandbox-child-stage-diagnostics.img`
(SHA-256 `f296069a5dbc23584d4bc686f7e6c92ced51f00998c46fcdb1e39d371db13a3f`).
Its packed appd is `824022fbe4f267f263a812afe82de3833aed0a82ac98706680d16ed8bd2cc288`.
The older `a3a968...` crash-diagnostic image predates the post-fork correction
and child-stage instrumentation and must not replace this baseline during
future preparation. Both images were preserved.

The one specifically documented superseded output is
`out/ocean-wallpaper-qa-evidence-20261029/OpenWave-wallpaper-qa-evidence-pre-fd-use.img`.
It is a regular 2-GiB sparse ext4 image allocated in 1,569,624 filesystem
blocks (803,647,488 bytes), mode 0600, owner 1001:1001. The adjacent
`verification.json` labels it a preserved intermediate superseded by the
final-policy image; it is not listed as a runtime evidence source. Removing
this one file would recover approximately 766 MiB and raise available space to
about 1.39 GiB. The user subsequently authorized deletion of exactly this file;
its recorded hash was verified, it alone was removed, and available space
became 1,497,088,000 bytes. Other output directories contain runtime logs,
writable/recovered filesystems, or have not been individually reviewed; they
were not cleanup targets.

The interactive runner now creates a qcow2 overlay with an absolute backing
path and explicit raw backing format, verifies the resulting backing chain,
and directs QEMU to open the overlay as qcow2. Host-only QEMU image tests
confirmed overlay writes do not alter the raw base. Post-run validation is
designed around a materialized writable image rather than running ext4 checks
on the qcow2 container. This storage path is **NOT guest-tested**. No cleanup,
image build, repack, or guest boot was performed for the source/storage review;
the separate authorized deletion is recorded above.

## Candidate base configuration correction — 2026-10-31

The example `prepared-enforcing-rootfs` path was a placeholder, not a supplied
staging directory. Its bare directory check exited silently. The builder now
reports the missing path and explains that omitting `OPENWAVE_OPENRC_BASE_ROOT`
uses the existing `rootfs` tree.

The base-tree enforcing check was also redundant: the builder already stages
the authoritative SELinux configuration alongside its freshly built policy.
A permissive base is now accepted as input, copied into the disposable output,
and overlaid with that authoritative enforcing configuration. The original base
is not changed. The authoritative source must still be enforcing; an immediate
byte comparison after the overlay and the existing final comparison/enforcing
checks reject a mismatched candidate. Symlinked or invalid policy destinations
are rejected before copying. Existing images are refused with a named error.

Host validation passed: eight actual-script staging tests (using a fixture
policy output and a barrier before native compilation), four QA environment
tests, seven DRM policy tests, shell syntax, OpenRC's 24-service dependency
validation, and `git diff --check`. The tests cover permissive-base preservation,
missing paths, policy path symlinks, wrong staged configuration, and preserving
an existing image. Production config hashes and the clean pinned OpenRC checkout
were unchanged. No native or policy compilation, candidate assembly, artifact
deletion, or QEMU boot was performed for this correction.
