# Stage 2N — Confined LXC traversal of /var/lib

## Failure observed after Stage 2M

A freshly rebuilt SELinux-enforcing QEMU guest still reports:

- Waydroid manager \`PASS\`, session start \`FAIL\`, LXC \`STOPPED\`.
- \`framework_passed=false\` and \`lxc_config_access=FAIL\`.
- LXC still prints \`lxc_start_main: 191 You lack access to /var/lib/waydroid/lxc\`.
- The Stage 2L \`libunwind.so.1\` permission error did not recur. This does
  **not** prove all loader dependencies are functioning.
- A new, more specific AVC from \`lxc-start\` PID 1951 denies
  \`search\` for \`name="lib"\` on \`openwave_state_t:dir\`.
- The Stage 2M \`/var\` private, \`/proc\` root and \`/sys\` root search
  AVCs are no longer the primary observed traversal failure.

The current file_contexts rules explain the sequence:

\`\`\`text
/var(/.*)?                 openwave_private_t
/var/(lib|log|cache)(/.*)? openwave_state_t
/var/lib/waydroid(/.*)?    openwave_waydroid_state_t
\`\`\`

LXC's dedicated Waydroid state access policy is present, but traversal
through the \`/var/lib\` parent \`openwave_state_t\` was not.

## Single narrow policy addition

\`\`\`cil
(allow openwave_lxc_t openwave_state_t (dir (search)))
\`\`\`

This grants **directory traversal only**, not directory listing,
open, file content access, create/write/unlink, mounton, relabel, or
capabilities. The rule is SELinux *type-wide*, not limited to \`/var/lib\`;
other directories labeled \`openwave_state_t\` also gain search rights.
A later policy hardening pass can introduce a dedicated parent type,
after auditing existing applications dependent on the original label.
Do not change \`file_contexts\` opportunistically during Android bring-up.

Compiled-policy regression tests require search permission but fail on
any \`openwave_state_t\` directory listing/write/mutation or file access.
Host report tests confirm \`lxc_search_denial_types\` captures the
exact types denied for \`openwave_lxc_t\`, and does **not** interpret
an empty denial set as successful Android boot.

## Other failures not addressed in Stage 2N

The same QEMU run also logged LXC SELinux denials for:

- \`openwave_security_t:filesystem { getattr }\` on selinuxfs
- \`openwave_proc_t:file { read }\` on \`/proc/filesystems\`
- \`openwave_proc_t:lnk_file { read }\` on \`/proc/mounts\`

Those may become blockers once LXC reaches later initialization.
They are deliberately **not** broadened into general procfs reads or
SELinux security filesystem privileges in this patch.

Independently, the Waydroid manager's OverlayFS mount emitted
\`statfs failed on '/var/lib/waydroid/overlay_rw/system'\`, and
\`build.prop\` and various device-directory operations were denied.
Investigate these with individual path labels and exact operations
after first clearing the LXC parent path. Do not authorize
\`dac_override\`, \`dac_read_search\`, \`sys_admin\`, unrestricted
device access or disable SELinux.

## Build and rerun under enforcing mode

\`\`\`bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-var-lib-traversal-stage2n-20261108 ||
  git switch --track origin/waydroid/lxc-var-lib-traversal-stage2n-20261108

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux

# Requires local compiled CIL test prerequisites:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_var_lib_state_parent_is_search_only -v

./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-varlib-stage2n.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-varlib-stage2n.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-varlib-stage2n-evidence

E=out/boot-recovery/waydroid-varlib-stage2n-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'WAYDROID_QA|lxc-start|You lack access|avc:.*denied|overlayfs:' \
  "$E/serial.log" | tail -n 170
\`\`\`

Do not overwrite earlier images or evidence. Continue to require
\`mode=enforcing\` and unchanged source-image and kernel hashes.

## Acceptance

For Stage 2N narrowly: the \`name="lib"\`,
\`openwave_lxc_t -> openwave_state_t:dir {search}\` AVC must disappear.
LXC's config-access error should clear if this was the only remaining
path-component denial. If it persists, collect the next exact
\`lxc_search_denial_types\` and LXC message rather than granting a
broader class.

The Android framework is **not** booted until LXC RUNNING, Android
ServiceManager and Zygote running, a live System Server PID,
\`sys.boot_completed=1\`, and an answering Package Manager. Graphics,
Android application shortcuts, and OpenWave's single Android Back
button require separate integration acceptance tests.
