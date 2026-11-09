# Stage 2R — LXC anonymous config memfd write and self socket connect

## What failed after Stage 2Q

The newly rebuilt disposable QEMU guest remained SELinux **enforcing**.
The Waydroid OpenRC manager started, but the D-Bus Start(session)
request failed, LXC stayed STOPPED, and no Android framework process
became available. The boot report has
\`diagnostics_complete=true\`, \`framework_passed=false\`.

After Stage 2Q allowed \`memfd_file { create }\`, the LXC PID
2012 moved to:

\`\`\`text
avc: denied { write } comm="lxc-start"
    path=2F6D656D66643A2E6C78635F636F6E6669675F66696C65202864656C6574656429
    scontext=...:openwave_lxc_t:s0
    tcontext=...:openwave_lxc_t:s0
    tclass=memfd_file permissive=0
avc: denied { connect } comm="lxc-start"
    scontext=...:openwave_lxc_t:s0
    tcontext=...:openwave_lxc_t:s0
    tclass=unix_stream_socket permissive=0
\`\`\`

The encoded file path decodes to
\`/memfd:.lxc_config_file (deleted)\`: this is **anonymous**
memory-backed LXC configuration, not a persistent file in the host
rootfs. The old self \`memfd_file {create}\` and \`/run\` search
denials were not observed in this run.

The existing LXC self Unix stream rule already permits socket
\`connectto\` (server-side endpoint authorization), but did not
permit \`connect\` (client-side socket operation). Distinguish them.

## Precise policy delta

\`\`\`cil
(allow openwave_lxc_t self (memfd_file (create write)))
(allow openwave_lxc_t self
       (unix_stream_socket (create bind listen accept read write connect connectto)))
\`\`\`

This amends existing **self** rules; it does not authorize opening,
reading, mapping, or executing LXC's memfds; no \`execmem\`;
does not enable cross-domain connections, and does not grant
\`CAP_SYS_ADMIN\`, mount privileges, DAC bypass, device enumeration,
or LSM policy administration.

A future denied memfd \`map\`/\`open\` or self socket \`getopt\`
will need evidence rather than broad preauthorization. The limits
are a diagnostic increment, not a claim that LXC is ready.

## Independent Android mount problem

The manager still logs:

\`\`\`text
overlayfs: statfs failed on '/var/lib/waydroid/overlay_rw/system'
\`\`\`

This occurs even as the system EROFS and vendor EXT4 source images
mount. There are separate enforcing AVCs on
\`openwave_waydroid_manager_t -> openwave_run_t:dir {read write}\`,
\`openwave_default_t:filesystem {getattr}\`, and
\`openwave_default_t:file {read}\` for \`build.prop\`.
There are also \`dac_override\`, \`dac_read_search\` and
GPU/devtmpfs enumeration denials.

This PR does **not** grant those permissions: the overlay error
needs a separate actual mount/upper/work directory investigation
and may prevent a usable Android rootfs after LXC starts.
Do not silently relax SELinux enforcing or accept a container
RUNNING as sufficient framework boot evidence.

## Validation

- Static CI tests parse the exact two AVC tuples and refuse
  to claim framework success while LXC remains STOPPED.
- The compiled policy mutation test rejects memfd read/open/map/
  execute/execmod and \`process execmem\`; explicitly requires
  \`memfd_file {create write}\` and \`unix_stream_socket {connect}\`.
- Actual QEMU guest validation is still REQUIRED.

Use a new image and evidence directory:

\`\`\`bash
cd /mnt/aosp/ThunderOS
git fetch origin
git switch waydroid/lxc-memfd-write-socket-connect-stage2r-20261109 ||
  git switch --track origin/waydroid/lxc-memfd-write-socket-connect-stage2r-20261109

python3 -m unittest discover -s scripts/tests -p test_waydroid_report.py -v
./build-openwave selinux
python3 -m unittest \
  userspace.security.selinux.tests.test_policy.PolicyTests.test_lxc_self_memfd_create_and_write_without_exec_or_mapping -v
./build-openwave rootfs

# Ensure enough host disk space; the rootfs assembly procedure makes
# a new rootfs-previous-* backup on each successful build.
OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs \
  out/boot-recovery/ocean-waydroid-selfio-stage2r.img

python3 scripts/test-openwave-boot.py \
  --waydroid --mode enforcing --seconds 600 \
  --image out/boot-recovery/ocean-waydroid-selfio-stage2r.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-selfio-stage2r-evidence

E=out/boot-recovery/waydroid-selfio-stage2r-evidence
cat "$E/waydroid.json" "$E/result.json"
grep -aEi 'memfd|lxc-start|overlayfs:|avc:.*denied|WAYDROID_QA' \
  "$E/serial.log" | tail -n 200
\`\`\`

Use 12G or 16G if the **apparent** rootfs size will not fit
8G. Do not overwrite the previous image or delete the
last known-good rootfs to create room.

## Acceptance

Stage 2R only clears when the precise self
\`memfd_file {write}\` and \`unix_stream_socket {connect}\`
AVCs are gone. New denials may follow.

Framework acceptance still requires LXC RUNNING, ServiceManager,
Zygote and System Server, \`sys.boot_completed=1\`, and Package
Manager response. Ocean app shortcuts, Android window display and
single Android Back button are separate integration gates.
