# Stage 2K — Android first-boot LXC root traversal

## Result of the first Android boot attempt (after Stage 2I)

This was a **real** enforcing Waydroid startup test, not just the read-only
mount preflight. The supplied \`waydroid.json\` records:

- \`diagnostics_complete=true\`, \`framework_passed=false\`
- Waydroid initialized, BinderFS provisioned with expected OpenWave labels.
- EROFS system image and loop-backed EXT4 vendor image **were mounted**.
- OpenRC started the Waydroid manager, but the D-Bus
  \`Start(session)\` request returned \`container failed to start\`.
- \`lxc-info\` reported \`STOPPED\`; Android ServiceManager, Zygote,
  System Server, Package Manager and Android graphics were not reached.

The LXC launch generated many identical AVCs:

\`\`\`
denied { search } comm="lxc-start"
scontext=system_u:system_r:openwave_lxc_t:s0
tcontext=system_u:object_r:openwave_default_t:s0
tclass=dir permissive=0
\`\`\`

There were **other independent issues**: manager device-directory
enumeration denials, \`build.prop\` read denial, \`dac_read_search\` and
\`dac_override\` denials, OverlayFS statfs failure on
\`/var/lib/waydroid/overlay_rw/system\`, and unmapped Android
\`vendor_file\` SELinux labels. Do **not** infer that fixing root search
alone guarantees successful Android boot.

The runner's \`shutdown="snapshot discarded after shutdown timeout"\` is
also **not** a clean powerdown success: it exited without a kernel
panic and without modifying the immutable source disk, but the
guest did not report a clean ACPI shutdown.

## Single, least-privilege SELinux change

\`\`\`cil
(allow openwave_lxc_t openwave_default_t (dir (search)))
\`\`\`

LXC now may traverse the generic host root directory before reaching
the already-constrained Waydroid runtime and state trees. This
permission covers directories carrying \`openwave_default_t\`, not
only \`/\`. It does **not** grant enumeration, reading, opening,
writing, modifying, mount-on, executing generic files, or privileges
to bypass DAC. A compiled policy test ensures those permissions remain
absent.

This intentionally **does not** add:
- \`CAP_DAC_OVERRIDE\` or \`CAP_DAC_READ_SEARCH\`
- \`openwave_lxc_t\` file reads in \`openwave_default_t\`
- broad \`openwave_waydroid_manager_t\` device/GPU rights
- \`CAP_SYS_ADMIN\`, Waydroid mount-helper authority, or policy bypasses

The test is not a Waydroid mount-helper privilege migration.

## Diagnostics changed

The guest harness now emits post-failure read-only overlay path and
mountinfo inventory and tails existing LXC diagnostic files when
present. It does not create overlay directories or re-run LXC in a
more privileged domain. The host report separately records
\`stages.session_start=FAIL\` when the Waydroid manager's D-Bus
Start request fails despite its OpenRC service reporting successful
startup. Host tests check these distinctions.

Waydroid upstream normally creates upper/work paths when mounting
OverlayFS, so the preflight's earlier \`overlay_*=missing\` reports
(before startup) do not, by themselves, prove missing provisioning.
The post-start probe and kernel's \`statfs failed\` are needed to
identify the real overlay failure.

## Rebuild and repeat, enforcing only

\`\`\`sh
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-root-traversal-diagnostics-stage2k-20261108 ||
    git switch --track origin/waydroid/lxc-root-traversal-diagnostics-stage2k-20261108

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux

# Compiled-policy test checks search is granted but other default-root
# permissions remain denied. It needs a locally built compile-policy.
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_root_traversal_is_search_only_under_enforcing -v

# Reuse previously staged working native UI; no full userspace build.
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-lxc-root-2k.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-lxc-root-2k.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-lxc-root-2k-evidence

E=out/boot-recovery/waydroid-lxc-root-2k-evidence
cat "$E/waydroid.json"
cat "$E/result.json"
grep -aEi 'WAYDROID_QA|WAYDROID_PROBE_END|avc:.*denied|overlayfs:|lxc-start|LXC_LOG' \
  "$E/serial.log" | tail -n 200
\`\`\`

Do not reuse an existing output/image pathname. The test runs
in a QEMU snapshot. A code/CI success alone does not prove
the guest starts LXC or Android.

## Success criteria

For complete **Android framework boot** require the container
\`RUNNING\`, Android ServiceManager and Zygote \`running\`, a live
System Server PID, \`sys.boot_completed=1\`, and a responsive
Package Manager. \`integration_passed\` stays false until Ocean
graphics and native navigation have their own end-to-end tests.

If the container remains \`STOPPED\`, use the latest guest AVC,
overlay inventory and LXC logs to isolate the next single
denial. Do not respond to a stack of AVCs by granting a
privilege bundle or disabling SELinux.
