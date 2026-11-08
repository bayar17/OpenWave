# Third-party software integration policy

OpenWave preserves imported software as upstream-owned components. Every
imported component must have a traceable upstream identity: repository or
release source, pinned version or commit, and an integrity check suitable for
the source form. Upgrades must be reviewable independently from OpenWave host
integration.

OpenWave code must not rewrite imported source files, build scripts, or
internal service implementations. Use the component's original entrypoints,
supported APIs, D-Bus interfaces, and documented configuration. Host-specific
behavior belongs in OpenWave-owned code, service definitions, and operating
system facilities. Do not reproduce functionality the component already
provides. When the unmodified component lacks a host requirement, record the
observed gap and implement or plan the compatibility facility on the OpenWave
side. Any exception requires explicit authorization for the named upstream
change.

For each integration, validate both sides of the boundary:

- Record the upstream version/commit and verify the checkout is clean before
  packaging.
- Compare installed upstream implementation files with the pinned source, or
  verify their hashes against a build manifest.
- Keep OpenWave adapters and service definitions outside the imported source
  tree, with tests for the public interface they consume.
- Do not claim runtime support until the original upstream path has been
  exercised with the required host facilities.

## Waydroid

Waydroid 1.6.3 at commit
`5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991` is the authoritative Android
runtime. Its original container manager, LXC configuration/startup, image
mounting, network helper, session manager, application discovery and launch,
and generated desktop shortcuts remain upstream-owned. OpenWave's role is to
provide Linux kernel facilities, PID 1 supervision, security policy, and
OceanShell integration through stable OpenWave-owned interfaces.

The existing OpenWave native lifecycle backend and manager-to-helper path are
not an alternate Android runtime. They must not start, stop, mount, or supervise
Android alongside Waydroid's manager. The active app launch path should invoke
the pinned `/usr/bin/waydroid app launch <package>` entrypoint and let
Waydroid's own session and container managers control lifecycle. Remove
OpenWave-specific transformations of Waydroid Python modules and internal
services from package staging. The upstream checkout must remain pristine.

OpenWave's D-Bus daemon was built without traditional service activation, so
PID 1 supervises the pinned Waydroid manager shim, which executes the original
`waydroid container start` entrypoint. The original manager owns its D-Bus
name. appd invokes `waydroid app launch` as the dedicated UID/GID 990 Waydroid
session account with a fixed persistent data directory and access to Ocean's
Wayland socket; the upstream command starts and owns the normal session flow.
OpenWave does not start Android's home screen automatically.

### Host compatibility status (2026-10-28)

The normal `./build-openwave waydroid-host-deps` target now cross-builds and
stages iproute2 7.3.0 and legacy iptables/ip6tables 1.8.13, then packages
Waydroid from a `git archive` of the pinned commit. The package verifier checks
the installed upstream files. The only generated-config adjustment is an
OpenWave-owned packaging step that removes only Waydroid's optional AppArmor
assignments from `config_1` and `config_3`; the pinned Waydroid checkout remains
clean. Waydroid selects `config_3` for LXC 3 and newer; `config_1` is its legacy
LXC 1/2 fallback. OpenWave's staged LXC 6.0.6 parser rejects the original
active `config_3` AppArmor directive and accepts the generated config after
that line is removed. This keeps AppArmor disabled and does not change SELinux
policy.

The tested kernel configurations have legacy IPv4/IPv6 xtables enabled and
nftables disabled, so Waydroid is packaged with its legacy iptables backend.
`waydroid0` is marked unmanaged in NetworkManager so Waydroid's original
network helper owns its bridge. dnsmasq was already available in target
staging. The target `ip`, `iptables`, and `ip6tables` binaries, Python `dbus`,
`gi`, and `gbinder` modules, and Waydroid 1.6.3 CLI were exercised on the host
through OpenWave's target loader; the CLI reports version 1.6.3. The manager
entrypoint is wired for PID 1 supervision after system D-Bus starts. These
checks establish build and host-side entrypoint readiness, not a successful
guest service activation. Waydroid runtime setup is optional in PID 1: a
missing manager or failed Waydroid-only setup skips the manager while native
services and Ocean continue to start, and a failed manager is not restarted
in a loop.

Appd imports eligible upstream-generated `waydroid.<package>.desktop` files
from the Waydroid session account's applications directory. It checks package
identity, visibility, file ownership/mode, and the exact supported
`waydroid app launch <package>` command, then launches by validated package
identity through the upstream CLI. It does not execute arbitrary desktop
commands. The earlier OpenWave catalog-sync prototype is unlinked from appd
and its D-Bus methods and policy grants are removed. Host-only parser tests
cover valid entries, hidden entries, invalid commands, mismatched package
identities, and symlinks.

PolicyKit is not needed by the currently wired manager start and app-launch
paths; upstream initialization paths that use PolicyKit are not integrated.
The production enforcing SELinux policy's access for the persistent Waydroid
manager, its required capabilities, D-Bus name ownership, and actual Android
startup have not been validated in a guest. Binder availability also depends
on which kernel is booted: the Waydroid confinement kernel config enables
Binder IPC/BinderFS, while the recorded production `out/linux/.config` does
not. No rootfs, production image, kernel, or SELinux policy was changed for
these host compatibility checks, and no QEMU guest was booted.
