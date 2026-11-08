# Stage 2Q — Confined LXC anonymous memfd creation in enforcing mode

## Observed Stage 2P guest outcome

A disposable QEMU run using Stage 2P kept SELinux enforcing and shut
down cleanly, but Waydroid \`Start(session)\` failed and LXC remained
\`STOPPED\`. The Android framework never started.

The earlier \`/run\` \`openwave_run_t:dir {search}\` denial disappeared.
The only LXC AVC in the Stage 2P report was:

\`\`\`text
avc: denied { create } for pid=1962 comm="lxc-start" anonclass=[memfd]
scontext=system_u:system_r:openwave_lxc_t:s0
tcontext=system_u:object_r:openwave_lxc_t:s0
tclass=memfd_file permissive=0
\`\`\`

This is an **anonymous memfd creation** denial, not a pathname/ELF
loader denial. The host SELinux policy supports the \`memfd_file\`
class and other OpenWave domains already have bounded memfd grants;
\`openwave_lxc_t\` did not.

## Exact Stage 2Q fix

\`\`\`cil
(allow openwave_lxc_t self (memfd_file (create)))
\`\`\`

The rule grants *only* the observed create operation for LXC's own
anonymous memfd objects. It does not authorize opening, reading,
writing, mapping, executing, relabeling other files, \`execmem\`,
\`CAP_SYS_ADMIN\`, mount privileges or SELinux policy administration.

There is no claim that memfd creation alone satisfies all LXC
requirements. A later bounded \`read\`/\`write\`/\`map\` denial may
emerge and must be evaluated individually. Do not add broad
\`execute\`, \`execmod\`, \`execmem\`, DAC bypass, or permissive policy.

Compiled-CIL mutation tests affirm the new \`create\` grant and
the absence of additional memfd file rights. Host regression tests
feed the exact Stage 2P AVC into \`openwave_waydroid_report.evaluate\`
and assert \`lxc_avc_denials\` reports \`memfd_file {create}\`, while
\`framework_passed=false\`.

## Independent OverlayFS issue

\`overlayfs: statfs failed on
'/var/lib/waydroid/overlay_rw/system'\` still occurs in the
Waydroid manager before LXC startup. EROFS system and EXT4 vendor
images **do mount**, but this does not establish that the overlaid
Android root is ready. The manager also has separate device, DAC
and \`build.prop\` read denials. Do not grant those permissions as
part of the LXC memfd fix.

## Rebuild and rerun with enforcing unchanged

\`\`\`bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-memfd-create-stage2q-20261109 ||
  git switch --track origin/waydroid/lxc-memfd-create-stage2q-20261109

python3 -m unittest discover -s scripts/tests \
  -p test_waydroid_report.py -v
./build-openwave selinux

# Requires locally built compile-policy and kernel.cil:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_self_memfd_create_only_after_run_root_traversal -v

./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-memfd-stage2q.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-memfd-stage2q.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-memfd-stage2q-evidence

E=out/boot-recovery/waydroid-memfd-stage2q-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'memfd|lxc-start|overlayfs:|avc:.*denied|WAYDROID_QA' \
  "$E/serial.log" | tail -n 180
\`\`\`

Use a fresh image/output path and a QEMU snapshot; verify enforcing
and unchanged input hashes.

## Acceptance

Stage 2Q narrowly passes if the exact
\`openwave_lxc_t self:memfd_file {create}\` AVC disappears with
SELinux enforcing, without new excessive permissions. Another
operation may become the next blocker.

Android framework completion requires LXC RUNNING, Android
ServiceManager and Zygote \`running\`, System Server live,
\`sys.boot_completed=1\`, and Package Manager answering. Ocean's
window embedding, appd shortcuts, and single Android Back button
are separate integration milestones.
