# OpenWave SELinux configuration

OpenWave loads a monolithic SELinux policy in its custom init before starting
services. Normal boot enforces the policy. A missing/corrupt policy, failed
labeling operation, or unavailable SELinux kernel support stops service startup
with an error on the console.

The policy adds mandatory confinement for regular apps alongside their existing
UID, Landlock and cgroup isolation. It does not replace permissiond or the file
broker. System services, the compositor, Settings and Files retain broad trusted
access in this initial policy. Their separate domains identify them for auditing;
they are not yet independently confined service sandboxes.

## Build

```bash
./build-openwave selinux
./build-openwave userspace --image out/OpenWave-selinux.img
./run-openwave --image out/OpenWave-selinux.img run
```

`selinux` builds target libsepol/libselinux and stages policy. The userspace build
also recompiles/stages policy and builds its init/appd integration. `all` includes
both steps. Kernel builds enable SELinux, audit and ext4 security attributes,
and include `selinux` in the configured LSM list.

The CIL compiler is a host program linked to a separately built host libsepol.
It never uses target libraries. Target compilation uses the exported SDK;
development files and host policy tools are excluded from rootfs. Kernel class,
permission and initial-SID declarations are generated from the repository's
Linux headers, rather than maintaining a second hardcoded ABI list. The current
binary policy format is version 35.

Policy sources live in `userspace/security/selinux`. Installed paths are:

- `/etc/selinux/config`
- `/etc/selinux/openwave/policy/policy.35`
- `/etc/selinux/openwave/contexts/files/file_contexts`

The configuration selects `openwave` in enforcing mode. Init mounts selinuxfs,
loads policy, applies labels while services are stopped, and enables enforcement.
It labels the persistent root filesystem and the separate dev/run/tmp/shm
filesystems; proc, sysfs and cgroups use policy filesystem contexts. This does
not depend on the host being able to write `security.selinux` attributes during
fakeroot packing. Relabeling runs on boot, including after image updates.

## Application boundary

appd selects `system_u:system_r:openwave_app_t:s0` for regular application execs
before dropping UIDs and enabling no-new-privileges. It labels newly provisioned
bundles and private data/runtime directories before launch. Applications cannot
select another exec domain, relabel their data, change enforcement, load policy,
execute privileged service entrypoints, or read files labeled
`openwave_private_t`, even if ordinary file permissions would permit access.
Compiler `neverallow` checks reject policy changes granting these privileges.

Runtime libraries/resources remain readable; application data remains writable;
the policy permits the existing Wayland/session/audio IPC and GPU rendering.
File descriptors authorized by the portal still need appropriate file-type
permissions. Network sockets remain available; network permission mediation is
not added by this policy.

The policy uses a single MLS level `s0`, without per-app categories. Different
apps share a SELinux domain/data type; their individual data separation continues
to depend on unique UIDs and Landlock. Namespace, seccomp and per-service policy
hardening remain separate work. Existing Settings/Files trust is unchanged.

Manifests may require `selinux` alongside `uid`, `landlock` and `cgroups-v2`.
An app requiring SELinux is rejected when SELinux is disabled or permissive.
Trusted-component exceptions cannot bypass explicit isolation requirements.

## Recovery and validation

For an explicit diagnostic boot, pass `--append 'openwave.selinux=permissive'`
to `run-openwave`. `openwave.selinux=disabled` skips initialization for recovery.
These boot options and `/etc/selinux/config` are administrator-controlled; apps
cannot request this through their manifest or environment. Recovery modes are
not normal enforcing operation. A boot policy error never silently selects one.

The console reports `OpenWave SELinux: policy loaded, filesystem labeled,
enforcing`. Read `/sys/fs/selinux/enforce` to check mode and
`/proc/PID/attr/current` for a process domain. Denials appear as `avc: denied`
records in the kernel log.

After `./build-openwave selinux`, compiler regression checks run with:

```bash
python3 userspace/security/selinux/tests/test_policy.py
```

The target runtime test in `tests/runtime.c` exercises positive and negative MAC
checks, including a confined process retaining UID 0 to distinguish SELinux
denials from DAC. It must run in a disposable QEMU test image; it creates labeled
fixtures under `/tmp` and requires an enforcing policy. Run it with a fresh image:

```bash
./build-openwave test-selinux out/qa-selinux.img
```

The upstream [CIL access-vector reference](https://github.com/SELinuxProject/selinux/blob/3.11/secilc/docs/cil_access_vector_rules.md)
documents the allow/neverallow syntax used here.
