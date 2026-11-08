# OpenWave Stage 2F — permissive SELinux AVC debugging in disposable QEMU

## Why we need this

The enforcing Stage 2E snapshot report showed:
- \`openwave_system_t\` is denied \`getattr\` on \`openwave_waydroid_mount_exec_t\`.
- From that observer domain the helper inode, label and ELF probe were blocked.
  Those checks do **not** prove the helper is absent or mislabeled.
- Both EROFS system and ext4 vendor image signatures passed.
- \`/dev/loop-control\` was missing; SELinux permissive cannot create a device.
- Overlay directories were absent before the normal Waydroid startup sequence.
  They may need safe provisioning but their pre-start absence alone is not fatal.
- The kernel audit console interleaved an AVC line into the helper-inode
  gate detail. Stage 2F preserves raw AVCs separately and clips the injected
  audit text from structured gate details.

## Safety model

**OpenWave production boot remains enforcing.** Stage 2F does not modify
\`/etc/selinux/config\`, CIL policy, \`CAP_SYS_ADMIN\`, kernel config, Waydroid
source, OpenRC service wiring or the helper's security checks. It only adds
an explicit QEMU debugging option and host-side report parser.

The **only** permitted combination for a permissive integration probe is:

\`\`\`
--mount-preflight --mode permissive --selinux-debug
\`\`\`

The test runner uses an immutable base image with QEMU \`-snapshot\`, checks
input SHA-256s before and after the run, writes a new evidence directory, and
requests graceful shutdown over QMP. The debug kernel command line adds
\`openwave.selinux=permissive audit=1 log_buf_len=16M printk.time=1\`.
The OpenWave SELinux initializer already recognizes this *per-boot*
\`openwave.selinux=permissive\` selection. The normal default remains enforcing.

Permissive SELinux means LSM denials are audited but not enforced. Other
OpenRC services still execute in this guest, so DO NOT use this mode on a
production image without the disposable QEMU snapshot. It is not an
unprivileged simulation of the real enforcing runtime.

The QA helper \`waydroid-mount\` itself refuses privileged requests unless
SELinux is **enforcing** and the process runs in the exact dedicated mount
domain. Stage 2F does not invoke any privileged mount/unmount helper operation.

The debugger **cannot** make a release gate pass:
- \`mount-preflight.json\`: \`preflight_passed=false\` when permissive.
- \`selinux-avc-debug.json\`: \`release_ready=false\` always.
- \`result.json\`: contains \`selinux_debug_capture_complete\` to distinguish
  successful evidence capture from actual mount validation.

The AVC report only groups source/target domains, permissions, classes and
the permissive flag. AVCs are **not** translated into policy allow rules.
An absence of AVCs is **not** success, and an AVC logged in permissive mode
is an access that would ordinarily be blocked in enforcing mode.

## Reproduce with your existing Stage 2E diagnostic image

Use the already-created diagnostic root image, provided it exists. This
run does **not** need to rebuild the rootfs or the locally modified LLVM
submodule because the permissive behavior is selected by the **kernel boot
arguments** and the diagnostic parser runs on the build host.

\`\`\`sh
cd /mnt/aosp/ThunderOS

git fetch origin
git switch waydroid/permissive-avc-debug-stage2f-20261108 ||
  git switch --track origin/waydroid/permissive-avc-debug-stage2f-20261108

python3 -m unittest discover -s scripts/tests -p test_selinux_debug.py -v

python3 scripts/test-openwave-boot.py \
  --mount-preflight --mode permissive --selinux-debug \
  --seconds 120 \
  --image out/boot-recovery/ocean-waydroid-mount-preflight-2e.img \
  --kernel out/boot-recovery/bzImage-waydroid-51 \
  --output out/boot-recovery/waydroid-mount-debug-permissive-2f
\`\`\`

Use the exact Waydroid-capable kernel that booted the prior snapshot if its
name differs from the example above. The new output directory **must not
already exist**. Use a different directory on every rerun.

For the additional loop-device/sysfs/filesystems inventory implemented in
the Stage 2F guest QA script, you must create a **new** diagnostic image
containing that updated script first, using
\`./build-openwave rootfs\` and then
\`OPENWAVE_IMAGE_SIZE=8G ./build-openwave image rootfs <new-image-path>\`.
Never overwrite the existing Stage 2E image.

## Analyze

\`\`\`sh
cat out/boot-recovery/waydroid-mount-debug-permissive-2f/selinux-avc-debug.json
cat out/boot-recovery/waydroid-mount-debug-permissive-2f/mount-preflight.json
grep -Ei 'avc:[[:space:]]+denied|MOUNT_QA_(BEGIN|GATE|INFO|END)' \
  out/boot-recovery/waydroid-mount-debug-permissive-2f/serial.log
\`\`\`

Look especially for accesses from \`openwave_system_t\` to the helper
executable (\`getattr\`, \`open\`, \`read\`, \`map\`) and \`/dev\` paths.
A \`permissive=1\` AVC is evidence that SELinux would deny the operation,
not evidence that the corresponding allow rule should be added.

The missing loop control device may be due to incomplete devtmpfs/udev
initialization or a kernel and rootfs discrepancy. Investigate kernel
\`CONFIG_BLK_DEV_LOOP=y\`, \`/sys/devices/virtual/misc/loop-control/dev\`,
\`/proc/devices\`, and whether the loop device appears at a later boot
phase. Do not use arbitrary privileged \`mknod\` or weaken SELinux to hide
this issue.

## Next gate

After reviewing actual AVCs and loop-device metadata, make **narrow**
changes to allow only the trusted diagnostic observer read-only metadata
access where needed. Restore enforcing mode for a **new** snapshot and repeat
the preflight. Actual mount authority and Android framework boot are
separate acceptance gates and remain unverified.
