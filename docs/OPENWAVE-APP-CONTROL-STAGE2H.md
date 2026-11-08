# Stage 2H — appd confined-app control socket, authenticated quit and normal exit

## Known problem, not a Waydroid mount issue

Stage 2G enforcing guest app-launch acceptance passed, including live Wayland
surfaces, confined processes, appd/Ocean restarts and SIGKILL recovery.
Nevertheless, appd logged AVCs:

\`\`\`
avc: denied { connectto } scontext=...:openwave_appd_t:s0
  tcontext=...:openwave_app_t:s0 tclass=unix_stream_socket permissive=0
\`\`\`

\`appd\` tries to connect to the fixed, per-process socket
\`/run/openwave/apps/<session>/openwave-app-<pid>.socket\`.
The process API attempts a JSON \`{"command":"quit"}\`, authenticates
\`SO_PEERCRED\` UID and exact PID, and waits for \`{"error":""}\`. Previously
an SELinux denial meant this path fell back to SIGTERM, yet the existing
smoke test considered the resulting exit to be acceptable.

## Changed

1. Only this permission was added to SELinux CIL:

   \`(allow openwave_appd_t openwave_app_t (unix_stream_socket (connectto)))\`

   appd already has access to its owned app-control socket-file types.
   The rule authorizes peer **domain**, not one socket pathname, so its
   exact-UID/PID peer checks, application-side supervisor authentication,
   private directory modes and Landlock boundary still matter.

2. appd now records:
   - \`appd: control quit-ack appId=<id> pid=<pid>\` after a valid
     authenticated reply.
   - \`appd: control quit-fallback appId=<id> pid=<pid>\` when the control
     request fails and it uses SIGTERM.
   - \`appd: control quit-timeout appId=<id> pid=<pid>\` before its
     deadline-based SIGKILL fallback.

   A completed connection callback verifies the app is still tracked before
   it dereferences the instance or compares \`SO_PEERCRED\`.

3. Enforcing-QEMU \`--app-launch\` QA now **requires** an authenticated
   quit ACK, an independent \`wait-status=0 stopping=true\` exit, and
   absence of a fallback/timeout for:
   - \`com.openwave.files\`
   - \`com.openwave.photos\`
   - \`com.openwave.ocean\` (Ocean Browser, not the compositor)

   Settings and WaterRouge Gallery currently do not use that per-instance
   channel and are intentionally **not** required to ACK.
   Intentional SIGKILL crash recovery is also excluded.

4. The guest emits \`APP_TEST CONTROL_ACK ...\` for every accepted exit and
   a summary \`APP_CONTROL_PASS ... checks=N\`, which must include all
   three application IDs and match the number of acknowledged runs.
   The host runner writes \`app-control.json\` and rejects missing, duplicate,
   false, or mismatched evidence even when \`APP_SMOKE PASS\` appears.

5. The policy regression test checks the precise allow target and that the
   Waydroid network and mount-helper domains remain excluded. Other SELinux
   restrictions remain unchanged; in particular no \`CAP_FSETID\`,
   \`execmem\`, \`CAP_SYS_ADMIN\`, or \`openwave_default_t\` mount grant.

## Stage 2H follow-up — fast-exit asynchronous reply race

An enforcing guest test reached all five application surfaces; Files, Photos,
and Ocean Browser each exited normally with `wait-status=0 stopping=true`,
yet all three recorded `APP_TEST CONTROL_FAIL`, without `quit-ack` or
`quit-fallback` log entries. The suspected race is that the GLib child
reaper frees the `Instance` before the async socket-response callback.
Previously `command_done()` only reported ACK/fallback when it could
find the live instance. Thus an app could successfully exit with status 0
and still leave **no** appd completion evidence.

Follow-up:
- `Command` owns an immutable copy of the app ID until it completes.
  Authenticated ACKs are logged even after child reaping; failures after
  reaping are explicitly `quit-noack-exited`, not silently ignored.
  Signalling a child only occurs while its tracked instance still exists.
- The guest harness polls for the result for at most 3 seconds **after**
  observing the process exit. It does not treat exit status 0 as an ACK.
- If any application still fails, the guest prints its matching appd log
  lines with the failure to support targeted diagnosis.

Before reusing an existing image or attributing this to SELinux, verify that
the **actual staged and packed appd binary** includes Stage 2H's messages:

```sh
strings rootfs/System/Services/appd | grep -F 'appd: control quit-ack appId='
# Optional: inspect the packed image without mounting or mutating it.
mkdir -p out/boot-recovery/stage2h-inspect
debugfs -R "dump /System/Services/appd out/boot-recovery/stage2h-inspect/appd" \
  out/boot-recovery/ocean-app-control-stage2h.img
strings out/boot-recovery/stage2h-inspect/appd | grep -F 'appd: control quit-ack appId='
sha256sum rootfs/System/Services/appd out/boot-recovery/stage2h-inspect/appd
```

If the marker is absent from the **packed** image, it contains a stale
appd executable. Rebuild `userspace`, then `rootfs`, and create a new
image; do **not** expand SELinux grants to compensate for stale code.

## Build and test (disposable OpenWave guest only)

Do not use the previous Stage 2G disk image as though it contained the new
helper and CIL. Build all changed userspace binaries into staging, rebuild
policy, assemble the rootfs and create **a new** immutable test input:

\`\`\`sh
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/app-control-ipc-stage2h-20261108 ||
  git switch --track origin/waydroid/app-control-ipc-stage2h-20261108

python3 -m unittest discover -s scripts/tests -p 'test_app_control_ipc.py' -v
./build-openwave userspace
./build-openwave selinux
./build-openwave rootfs

# The existing policy tests have three known Stage 2A manager-mount privilege
# negative-test failures; do not report the full suite as clean.
# Run the dedicated new test against the locally built compile-policy:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_appd_can_control_confined_app_but_not_waydroid_mount_helper -v

OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-app-control-stage2h.img

python3 scripts/test-openwave-boot.py \
  --app-launch --mode enforcing --seconds 180 \
  --image out/boot-recovery/ocean-app-control-stage2h.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/app-control-stage2h-evidence
\`\`\`

Use the known-good QEMU kernel image only if the file exists and matches
the prior verified kernel. The runner uses \`-snapshot\`, verifies unchanged
SHA-256s and refuses an existing output directory.

\`\`\`sh
E=out/boot-recovery/app-control-stage2h-evidence
cat "$E/app-control.json"
cat "$E/result.json"
grep -aE 'APP_TEST CONTROL_|APP_CONTROL_(PASS|FAIL)|appd: control quit-|APP_SMOKE|avc:.*connectto' \
  "$E/serial.log"
\`\`\`

**Acceptance:** \`app_launch_passed=true\`,
\`app_control_passed=true\`, \`app-control.json: passed=true\`,
\`APP_CONTROL_PASS\`, no \`APP_CONTROL_FAIL\`, all three app IDs evidenced,
SELinux enforcing, images unchanged, controlled clean exit and no
\`appd_t -> app_t:unix_stream_socket connectto\` denial.

## Limits and follow-ups

- Host-only tests validate script syntax/parser semantics. Real QEMU behavior
  remains unverified until a guest run produces the evidence.
- A clean exit and ACK demonstrate correct delivery of the command; they do
  not prove every application performs every user-data flush before exit.
- The many \`CAP_FSETID\` denials and occasional \`execmem\`, Qt locale and
  font warnings, and WaterRouge Gallery Flow QML issue remain separate.
- Do not transfer Waydroid mount rights or loosen enforcing policy as part
  of application IPC cleanup.
