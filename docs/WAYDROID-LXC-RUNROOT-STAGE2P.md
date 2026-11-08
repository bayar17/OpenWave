# Stage 2P — LXC /run root traversal under SELinux enforcing

## Observed Stage 2O result

The supplied Stage 2O enforcing QEMU result reports:

- \`diagnostics_complete=true\`, \`framework_passed=false\`,
  \`integration_passed=false\`.
- Waydroid manager started successfully; session Start failed; LXC
  container remained \`STOPPED\`.
- The Stage 2N LXC config path error and Stage 2O .cache create and
  selinuxfs filesystem metadata AVCs did not recur.
- The *only LXC-specific* denial tuple in \`lxc_avc_denials\` was
  \`openwave_run_t:dir {search}\` (twice), with
  \`comm="lxc-start"\`, \`name="/"\`, \`dev="tmpfs"\`.
- Other processes still see separate AVCs (Waydroid manager, mounting,
  service setup); these are not LXC search denials.
- OverlayFS still reports \`statfs failed on
  '/var/lib/waydroid/overlay_rw/system'\` despite the EROFS system and
  EXT4 vendor image mounting successfully.

These results indicate only that LXC reached another path traversal
boundary. They do not prove that the Android framework can start.

## Minimal SELinux policy correction

Stage 2O added a metadata-only rule for the \`/run\` tmpfs root:

\`\`\`cil
(allow openwave_lxc_t openwave_run_t (dir (getattr)))
\`\`\`

Stage 2P replaces this with:

\`\`\`cil
(allow openwave_lxc_t openwave_run_t (dir (search getattr)))
\`\`\`

This allows the confined \`lxc-start\` process to traverse directory
objects labeled \`openwave_run_t\`. It does **not** permit directory
listing, opening, modification, directory creation, file or socket
reads, mounting, \`CAP_SYS_ADMIN\`, DAC override or SELinux policy
management. The permission is SELinux **type-wide** rather than
limited to \`/run\` root, and may eventually be narrowed with a
dedicated label if runtime evidence justifies it.

## Validation improvements

- The compiled CIL mutation test checks the required \`search\` and
  \`getattr\` permissions while rejecting enumerating, opening, writing,
  relabeling or \`mounton\` access to \`openwave_run_t\` directories
  and file/link read authority.
- Existing Stage 2O test now asserts the updated combined rule.
- Host regression feeds the *exact* Stage 2O AVC twice to the
  Waydroid report evaluator and checks \`lxc_avc_denials\` deduplicates
  it while \`framework_passed\` remains false.

## Build and rerun with enforcing unchanged

\`\`\`bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-runroot-traversal-stage2p-20261109 ||
  git switch --track origin/waydroid/lxc-runroot-traversal-stage2p-20261109

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux

# Local compiled-policy test, where the compiler is available:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_run_root_traversal_is_search_and_metadata_only -v

# Reuse the existing native app staging tree (full WaterRouge SDK
# rebuild remains a separate issue).
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-runroot-stage2p.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-runroot-stage2p.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-runroot-stage2p-evidence

E=out/boot-recovery/waydroid-runroot-stage2p-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'WAYDROID_QA|lxc-start|overlayfs:|avc:.*denied' \
  "$E/serial.log" | tail -n 180
\`\`\`

Use new filenames and disposable QEMU snapshots. The inputs must
remain unchanged and the guest must stay in enforcing mode.

## Acceptance criteria

The immediate Stage 2P target is to remove the exact
\`openwave_lxc_t -> openwave_run_t:dir {search}\` denial. A new
denial or a later LXC error may emerge. The independently observed
OverlayFS failure is not fixed by this permission and will require
its own diagnosis.

Complete Android framework boot requires: LXC \`RUNNING\`,
ServiceManager and Zygote both \`running\`, live System Server PID,
\`sys.boot_completed=1\`, and a responsive Package Manager.
Ocean windows, appd shortcuts and single Android Back button
are separate later acceptance tests.
