# Stage 2M — LXC host-path search under SELinux enforcing

## Verified first-boot failure after Stage 2L

An enforcing QEMU boot using the Stage 2L image reports:

- \`diagnostics_complete=true\`, \`framework_passed=false\`
- Waydroid manager started, BinderFS provisioned, images present
- EROFS system and loop-backed EXT4 vendor images were mounted
- \`Start(session)\` failed; LXC container remained \`STOPPED\`
- \`lxc_failed_library=null\`: the previously observed \`libunwind.so.1\`
  loader failure no longer appeared, **but absence alone does not
  prove all libraries loaded**
- The upstream LXC program itself now reports:

\`\`\`text
lxc-start: waydroid: ../../../third_party/lxc-6.0.6/src/lxc/tools/lxc_start.c:
lxc_start_main: 191 You lack access to /var/lib/waydroid/lxc
\`\`\`

The same \`lxc-start\` PID had enforcing AVCs:
- \`openwave_lxc_t -> openwave_private_t:dir { search }\` on \`/var\`
- \`openwave_lxc_t -> openwave_proc_t:dir { search }\` on \`/proc\`
- \`openwave_lxc_t -> openwave_sysfs_t:dir { search }\` on \`/sys\`

These are **pathname traversal failures**, not evidence that the
Waydroid state directory needs to be made world-readable. The policy
already grants LXC managed access to \`openwave_waydroid_state_t\`
for \`/var/lib/waydroid\`; reaching it requires searching \`/var\`.

## Minimal Stage 2M policy additions

\`\`\`cil
(allow openwave_lxc_t openwave_private_t (dir (search)))
(allow openwave_lxc_t openwave_proc_t (dir (search)))
(allow openwave_lxc_t openwave_sysfs_t (dir (search)))
\`\`\`

Only search on three **directory types**. This is not a pathname
restriction: it permits traversing other paths with the same types.
We do not grant \`read\` or \`open\` on those directory types,
private-file reads, device access, DAC bypass, mount privileges,
\`CAP_SYS_ADMIN\`, generic file access, or SELinux permissive mode.

A compiled-policy regression rejects any new directory
enumeration/mutation rights or broad private-file access. More
granular parent-directory labeling may be desirable as the final
runtime policy is hardened.

## Known independent failures remain

1. OverlayFS reported \`statfs failed on
   /var/lib/waydroid/overlay_rw/system\`, despite the path being
   reported as a directory after startup failed. The result does not
   establish whether this is a permissions, mount configuration, or
   other filesystem problem. Diagnose separately.
2. The manager saw \`read\` denials for /dev/dri and devtmpfs
   enumeration, \`dac_override\` and \`dac_read_search\` capability
   denials, and \`build.prop\` read denial. Do not grant
   these wholesale to get past container startup.
3. Android vendor label warning: \`vendor_file\` is not a valid type
   in the host OpenWave policy. Do not blend Android and host policy
   names without an explicit design.

The Waydroid report now distinguishes \`lxc_config_access=FAIL\`
with the exact denied path. Only a running container can report
\`lxc_config_access=PASS\` in the absence of that error; otherwise
the stage remains \`UNVERIFIED\`.

## Rebuild and run in a fresh disposable QEMU snapshot

\`\`\`sh
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-host-path-stage2m-20261108 ||
    git switch --track origin/waydroid/lxc-host-path-stage2m-20261108

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux

python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_host_path_search_does_not_open_private_or_virtual_filesystems -v

# Do not reconfigure the incomplete WaterRouge SDK.
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
    out/boot-recovery/ocean-waydroid-hostpath-stage2m.img

python3 scripts/test-openwave-boot.py \
    --waydroid --mode enforcing --seconds 600 \
    --image out/boot-recovery/ocean-waydroid-hostpath-stage2m.img \
    --kernel out/boot-recovery/bzImage-waydroid-51 \
    --output out/boot-recovery/waydroid-hostpath-stage2m-evidence

E=out/boot-recovery/waydroid-hostpath-stage2m-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'WAYDROID_QA|You lack access|lxc-start|avc:.*denied|overlayfs:' \
    "$E/serial.log" | tail -n 160
\`\`\`

Never overwrite the previous image or output directory. All changes
are for the OpenWave guest, not the development host.

## Acceptance

First verify that the \`You lack access to /var/lib/waydroid/lxc\`
error and the three targeted directory-search AVCs disappear under
SELinux enforcing. A new failure may surface.

The **full Android framework boot** requires LXC \`RUNNING\`,
ServiceManager and Zygote \`running\`, System Server with a live
PID, \`sys.boot_completed=1\`, and Package Manager responding.
Ocean graphics, app shortcuts, and the Android-only Back button
need a separate integration test.

A green GitHub host-CI run does not certify those guest conditions.
