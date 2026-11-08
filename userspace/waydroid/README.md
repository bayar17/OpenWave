# OpenWave Waydroid boundary

This directory contains OpenWave-owned build and service adapters. The pinned
Waydroid implementation stays in `third_party/waydroid`; it owns initialization,
image mounts, LXC, networking, sessions and Android package operations.

`waydroidd` executes only the upstream `container start` entrypoint with a fixed
environment. It accepts no user-supplied operations or arguments. It is wired
into the userspace CMake build; it is not a replacement container manager.
OpenRC logs are `/run/openwave/waydroid.stdout.log` and
`/run/openwave/waydroid.stderr.log` (tmpfs). Upstream uses
`/var/lib/waydroid/waydroid.log` (persistent when the guest writes it).

The integration is **not operational yet**. Guest initialization passes with
SELinux enforcing; manager and container startup are still being validated.
There is no validated Android surface/shortcut/navigation provider. Keep the service out of
the default runlevel until these checks pass. A zero CLI status is insufficient.

Build target bindings with `./build-openwave waydroid-bindings`. The wider
`waydroid-host-deps` target also builds networking prerequisites and packages
upstream. `prepare_apparmor_free.py` and `verify_upstream_package.py` allow only
the documented generated LXC template adjustment and reject changed or extra
implementation modules. Assembly excludes the retired catalog prototype and
duplicate D-Bus activation. Its recovered source is preserved in `legacy/`.

`kernel.config` and `build-kernel.sh` build missing Android kernel facilities
in a separate output directory. `audit.py` reports static prerequisites.
`scripts/test-openwave-boot.py --waydroid` uses an immutable snapshot, bounded
guest probes, enforcing SELinux and a separate `waydroid.json` report. It returns
failure when Android framework readiness is absent, even if native OpenWave boots.

See [the current audit and evidence](../../docs/WAYDROID-INTEGRATION-AUDIT.md).
