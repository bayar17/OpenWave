# Stage 2O — Confined LXC state-cache directory creation and host metadata

## Failure observed after Stage 2N

The enforcing Stage 2N guest still did **not** boot Android:

- Waydroid OpenRC manager PASS; session Start FAIL; LXC STOPPED.
- \`framework_passed=false\`, and all Android framework probes BLOCKED.
- Previously observed \`libunwind.so.1\` loader error and
  \`You lack access to /var/lib/waydroid/lxc\` message did not recur.
- \`lxc_search_denial_types=[]\`, yet that is **not** evidence of LXC success.
  The kernel reports different classes of LXC AVCs from PID 1957:
  - \`openwave_waydroid_state_t:dir { create }\`, \`name=".cache"\`
  - \`openwave_run_t:dir { getattr }\`, \`path="/run"\`
  - \`openwave_security_t:filesystem { getattr }\`, selinuxfs root
  - \`openwave_proc_t:file { read }\`, \`name="filesystems"\`
  - \`openwave_proc_t:lnk_file { read }\`, \`name="mounts"\`

This is further progress into LXC initialization, not successful
Android bootstrap. The failed \`mkdir(".cache")\` is known only by
its target SELinux type and basename; the AVC does **not** establish
the directory's full pathname. Avoid asserting it is
\`/var/lib/waydroid/.cache\` without independent evidence.

The manager also still observes \`overlayfs: statfs failed on
'/var/lib/waydroid/overlay_rw/system'\` even though both image
mounts succeed. This must be investigated separately.

## Minimal Stage 2O policy

Amend only the existing confined Waydroid state directory rights
(from \`search read open getattr write add_name remove_name\` to include
\`create\`). Add metadata-only access to the system run root and
selinuxfs filesystem:

\`\`\`cil
(allow openwave_lxc_t openwave_waydroid_state_t
       (dir (search read open getattr write add_name remove_name create)))
(allow openwave_lxc_t openwave_run_t (dir (getattr)))
(allow openwave_lxc_t openwave_security_t (filesystem (getattr)))
\`\`\`

This is a **type-scoped** directory-creation right: within existing
Waydroid-state writable parents, LXC may create other directories of
that same type, not just a directory named \`.cache\`. It does not
grant file execution, generic state access, host-device operations,
mounting, process privileges or SELinux policy management.

The already-denied \`/proc/filesystems\` and \`/proc/mounts\` reads
are **not** widened to all \`openwave_proc_t\` files or symlinks.
These target classes cover many unrelated procfs objects; an exact
read-only policy design requires further examination of their
runtime labeling and actual LXC requirements. The manager's
\`dac_override\`, \`dac_read_search\`, GPU/device enumeration, and
generic \`build.prop\` AVCs also remain denied.

Compiled-policy tests assert the new three permissions and reject
runtime-directory enumeration/write, Waydroid state relabel/mounton,
and selinuxfs policy administration.

## Improved evidence

\`scripts/openwave_waydroid_report.py\` now adds an
\`lxc_avc_denials\` array containing distinct permission/SELinux
target-type/object-class tuples for \`comm="lxc-start"\` in
\`openwave_lxc_t\`. This addresses the misleading silence of an
empty search-only list. Absence of AVCs is never treated as a
framework pass; exact Android probes remain mandatory.

## Build and test with enforcing unchanged

\`\`\`sh
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-state-cache-stage2o-20261108 ||
  git switch --track origin/waydroid/lxc-state-cache-stage2o-20261108

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux

# Requires an available locally built compile-policy:
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_state_cache_and_read_only_host_metadata_boundary -v

# Reuse previously staged native app binaries; full WaterRouge SDK
# rebuild is a different issue.
./build-openwave rootfs
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-cache-stage2o.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-cache-stage2o.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-cache-stage2o-evidence

E=out/boot-recovery/waydroid-cache-stage2o-evidence
cat "$E/waydroid.json"
cat "$E/result.json"
grep -aEi 'lxc-start|avc:.*denied|overlayfs:|WAYDROID_QA' \
  "$E/serial.log" | tail -n 170
\`\`\`

Use a new image/output directory, do not mutate the previous guest image,
and maintain enforcing mode. QEMU input hashes must remain unchanged.

## Acceptance criteria

Stage 2O narrow acceptance: the exact \`lxc-start\` AVCs for
\`openwave_waydroid_state_t:dir { create }\`,
\`openwave_run_t:dir { getattr }\` and
\`openwave_security_t:filesystem { getattr }\` disappear.
The next issue may be the still-denied procfs reads, overlay mount,
or another LXC operation.

**Android itself is not booted** until LXC reports RUNNING,
ServiceManager and Zygote run, System Server has a live PID,
\`sys.boot_completed=1\` and Package Manager responds. Ocean Shell
graphics, imported app shortcuts and the single Android Back button
remain separate integration milestones.
