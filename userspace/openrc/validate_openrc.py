#!/usr/bin/env python3
"""Host-only checks for OpenWave-owned OpenRC integration files."""

from pathlib import Path
import re
import os
import stat
import subprocess
import json

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "userspace/openrc/etc/init.d"
EXPECTED = {
    "openwave-runtime", "powerd", "udevd", "dbus", "nm-dispatcher", "iwd", "bluetooth",
    "NetworkManager", "avahi-daemon", "connectivityd", "timed", "wallpaperagentd",
    "openwave-portal", "promptd", "xdg-desktop-portal-openwave", "pipewire",
    "wireplumber", "openwave-session-bus", "audiod", "appd", "permissiond",
    "openwave-ocean", "waydroid-runtime", "waydroid", "acpid",
}


def dependency_graph(sources):
    """Resolve literal, unconditional depend() calls, including virtual names.

    Conditional shell dependencies still require guest validation. Ignoring
    their bodies avoids inventing edges such as hwclock's optional use root.
    """
    calls = {}
    providers = {}
    for name, source in sources.items():
        calls[name] = []
        body = re.search(r"^depend\(\)\s*\{(.*?)^\}", source, re.M | re.S)
        if not body:
            continue
        depth = 0
        for line in body.group(1).splitlines():
            line = line.strip()
            if re.match(r"^(fi|esac|done)\b", line):
                depth = max(0, depth - 1)
            match = re.match(r"^(need|use|want|after|before|provide)\s+([^#]+)", line)
            if depth == 0 and match:
                order, group = match.groups()
                for dependency in group.split():
                    calls[name].append((order, dependency))
                    if order == "provide":
                        providers.setdefault(dependency, set()).add(name)
            if re.match(r"^(if|case|for|while|until)\b", line):
                depth += 1
    graph = {name: set() for name in sources}
    for name, dependencies in calls.items():
        for order, dependency in dependencies:
            if order == "provide":
                continue
            targets = providers.get(dependency, set()) | ({dependency} if dependency in sources else set())
            for target in targets:
                if target == name:
                    continue
                if order == "before":
                    graph[target].add(name)
                else:
                    graph[name].add(target)
    return graph


def check_dependency_cycles(graph):
    visiting, visited = [], set()

    def visit(name):
        if name in visiting:
            cycle = visiting[visiting.index(name):] + [name]
            raise SystemExit(f"OpenRC dependency cycle: {' -> '.join(cycle)}")
        if name in visited:
            return
        visiting.append(name)
        for dependency in sorted(graph[name]):
            visit(dependency)
        visiting.pop()
        visited.add(name)

    for name in sorted(graph):
        visit(name)


def main():
    candidate_value = os.environ.get("OPENWAVE_OPENRC_ROOT")
    candidate_root = Path(candidate_value) if candidate_value else None
    qa_observability = os.environ.get("OPENWAVE_OPENRC_QA_OBSERVABILITY") == "1"
    busybox = (candidate_root / "bin/busybox") if candidate_root else ROOT / \
        "out/openrc-firstboot-busybox-20261029/busybox-build/busybox"
    if not busybox.is_file():
        raise SystemExit(f"missing BusyBox ash: {busybox}")
    services = {path.name: path for path in SOURCE.iterdir() if path.is_file()}
    if set(services) != EXPECTED:
        raise SystemExit(f"service file set mismatch: {sorted(set(services) ^ EXPECTED)}")
    for name, path in services.items():
        subprocess.run([str(busybox), "ash", "-n", str(path)], check=True)
        source = path.read_text()
        if not source.startswith("#!/sbin/openrc-run\n"):
            raise SystemExit(f"service does not use OpenRC interpreter: {name}")
        if "command_background" in source and not re.search(r"^pidfile=", source, re.M):
            raise SystemExit(f"background service has no pidfile: {name}")
    graph = dependency_graph({name: path.read_text() for name, path in services.items()})
    check_dependency_cycles(graph)
    default_services = set((ROOT / "userspace/openrc/default-services.txt").read_text().split())
    boot_services = set((ROOT / "userspace/openrc/boot-services.txt").read_text().split())
    if boot_services != {"openwave-runtime", "acpid"}:
        raise SystemExit("OpenWave boot services must include runtime provisioning and ACPI shutdown")
    if {"waydroid", "waydroid-runtime"} & default_services:
        raise SystemExit("optional Waydroid service is enabled in the default runlevel")
    if "waydroid-runtime" not in graph["waydroid"]:
        raise SystemExit("Waydroid manager does not require its runtime provisioning service")
    if any("waydroid-runtime" in deps for name, deps in graph.items() if name != "waydroid"):
        raise SystemExit("native service depends on optional Waydroid provisioning")
    if candidate_root and candidate_root.exists():
        installed = {path.name: path.read_text()
                     for path in (candidate_root / "etc/init.d").iterdir()
                     if path.is_file()}
        check_dependency_cycles(dependency_graph(installed))
        readelf = ROOT / "toolchain/bin/llvm-readelf"
        enabled = {}
        for runlevel in ("sysinit", "boot", "default"):
            directory = candidate_root / "etc/runlevels" / runlevel
            enabled[runlevel] = sorted(path.name for path in directory.iterdir() if path.is_symlink())
            for name in enabled[runlevel]:
                service = candidate_root / "etc/init.d" / name
                if not service.is_file():
                    raise SystemExit(f"enabled service has no init script: {runlevel}/{name}")
        shutdown = candidate_root / "etc/runlevels/shutdown"
        for name in ("killprocs", "mount-ro", "savecache"):
            link = shutdown / name
            if not link.is_symlink() or not (candidate_root / "etc/init.d" / name).is_file():
                raise SystemExit(f"missing OpenRC shutdown service: {name}")
        required_commands = {
            "mount": ("sysfs", "cgroups", "localmount", "root", "procfs", "bootmisc"),
            "mkdir": ("sysfs", "cgroups"),
            "hostname": ("hostname",),
            "ip": ("loopback",),
        }
        required_busybox_applets = {
            "chmod", "cp", "ifconfig", "ip", "kbd_mode", "mktemp", "mv", "ps", "readlink", "rmdir",
            "sed", "sleep", "sync", "tail",
        }
        applets = set(subprocess.run([str(busybox), "--list"], capture_output=True,
                                     text=True, check=True).stdout.splitlines())
        missing_applets = required_busybox_applets - applets
        if missing_applets:
            raise SystemExit(f"BusyBox lacks required OpenRC applets: {sorted(missing_applets)}")
        for applet in required_busybox_applets:
            link = candidate_root / "bin" / applet
            if not link.is_symlink() or link.resolve() != busybox.resolve():
                raise SystemExit(f"OpenRC-required BusyBox applet is not staged: {link}")
        for command, services_using_it in required_commands.items():
            path = candidate_root / ("usr/bin" if command in ("mkdir", "hostname", "mount") else "sbin") / command
            if not path.is_file() or not os.access(path, os.X_OK):
                raise SystemExit(f"missing required runtime command: {command} ({path})")
            for service in services_using_it:
                if service not in enabled["sysinit"] + enabled["boot"] + enabled["default"]:
                    raise SystemExit(f"command inventory names a disabled service: {service} -> {command}")
        required_native = {
            "dbus", "appd", "permissiond", "promptd", "wallpaperagentd", "openwave-ocean", "openwave-portal",
            "openwave-session-bus", "pipewire", "wireplumber", "audiod", "xdg-desktop-portal-openwave",
        }
        if not required_native.issubset(enabled["default"]):
            raise SystemExit(f"required native services missing from default: {sorted(required_native - set(enabled['default']))}")
        if "openwave-runtime" not in enabled["boot"] or "openwave-runtime" in enabled["default"]:
            raise SystemExit("runtime provisioning must run in boot, before dynamic device dependencies")
        if "bluetooth" not in enabled["default"] or "bluetoothd" in enabled["default"]:
            raise SystemExit("BlueZ must be exposed as the canonical bluetooth OpenRC service")
        if (candidate_root / "etc/init.d/bluetoothd").exists() or any(
                (candidate_root / "etc/runlevels" / level / "bluetoothd").exists()
                for level in ("sysinit", "boot", "default", "nonetwork")):
            raise SystemExit("obsolete bluetoothd service remains in the candidate")
        if "acpid" not in enabled["boot"]:
            raise SystemExit("acpid must start in boot before the default native session")
        service_text = {
            name: (candidate_root / "etc/init.d" / name).read_text()
            for name in required_native | {"openwave-runtime", "udevd", "NetworkManager", "acpid", "bluetooth"}
        }
        required_fragments = {
            "dbus": ('command="/usr/bin/dbus-daemon"', "--system --nofork"),
            "appd": ('command="/System/Services/appd"', 'DBUS_SYSTEM_BUS_ADDRESS="unix:path=/run/dbus/system_bus_socket"', "openwave-wait-dbus system org.openwave.Applications", "ListApplications"),
            "permissiond": ('command="/System/Services/permissiond"', "need dbus openwave-session-bus appd", "openwave-wait-dbus system org.openwave.Permissions"),
            "promptd": ('command="/System/Services/promptd"', "need dbus openwave-session-bus", 'DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus"', "openwave-wait-dbus session org.openwave.Prompt"),
            "wallpaperagentd": ('command="/System/Services/wallpaperagentd"', "need dbus openwave-session-bus", "openwave-wait-dbus system org.openwave.Wallpaper"),
            "openwave-portal": ('command="/System/Services/openwave-portal"', "openwave-wait-dbus system org.openwave.portal"),
            "xdg-desktop-portal-openwave": ('command="/System/Services/xdg-desktop-portal-openwave"', 'DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus"', "org.freedesktop.portal.Desktop org.freedesktop.impl.portal.desktop.openwave"),
            "openwave-ocean": ('command="/System/Applications/com.openwave.shell/bin/ocean-launcher"',
                               'pidfile="/run/openwave/ocean.pid"', 'command_user="ocean:ocean"', 'USER="ocean"',
                               'no_new_privs="yes"', 'umask="0077"',
                               'Refusing symlinked Ocean log directory', 'Refusing symlinked Ocean log file',
                               "need openwave-runtime udevd openwave-session-bus appd permissiond promptd wallpaperagentd powerd openwave-portal xdg-desktop-portal-openwave",
                               "openwave-wait-dbus system org.openwave.Shell",
                               'OPENWAVE_TRUSTED_SHELL="1"',
                               'QT_QPA_PLATFORM="eglfs"', 'QT_QPA_EGLFS_INTEGRATION="eglfs_kms"',
                               'XDG_RUNTIME_DIR="/run/user/1000"'),
            "pipewire": ('command_user="ocean:ocean"', 'XDG_RUNTIME_DIR="/run/user/1000"'),
            "wireplumber": ('command_user="ocean:ocean"', 'command_args="-p main-systemwide"'),
            "acpid": ('command="/usr/sbin/acpid"', 'command_args_foreground="-n -f"', 'supervisor=supervise-daemon', "need devfs"),
            "bluetooth": ('command="/usr/libexec/bluetooth/bluetoothd"', 'command_args_foreground="-n"', 'supervisor=supervise-daemon'),
            "NetworkManager": ('command="/usr/sbin/NetworkManager"', 'command_args_foreground="--no-daemon --debug"', 'supervisor=supervise-daemon', "need dbus udevd iwd"),
            "dbus": ('command="/usr/bin/dbus-daemon"', 'command_args_foreground="--system --nofork"', 'supervisor=supervise-daemon'),
        }
        for name, fragments in required_fragments.items():
            missing = [fragment for fragment in fragments if fragment not in service_text[name]]
            if missing:
                raise SystemExit(f"service {name} has unexpected command, identity, environment, or dependencies: {missing}")
        log_destinations = {
            "appd": ("/run/openwave/appd.stdout.log", "/run/openwave/appd.stderr.log"),
            "openwave-ocean": ("/run/user/1000/openwave-logs/ocean.stdout.log",
                               "/run/user/1000/openwave-logs/ocean.stderr.log"),
        }
        if qa_observability:
            log_destinations = {
                "appd": ("/var/log/openwave-qa/appd.stdout.log", "/var/log/openwave-qa/appd.stderr.log"),
                "wallpaperagentd": ("/var/log/openwave-qa/wallpaper.stdout.log",
                                     "/var/log/openwave-qa/wallpaper.stderr.log"),
                "openwave-ocean": ("/run/user/1000/openwave-logs/ocean.stdout.log",
                                   "/run/user/1000/openwave-logs/ocean.stderr.log"),
            }
        for service, paths in log_destinations.items():
            text = service_text[service]
            expected = (f'output_log="{paths[0]}"', f'error_log="{paths[1]}"')
            if any(fragment not in text for fragment in expected):
                kind = "QA" if qa_observability else "production"
                raise SystemExit(f"{kind} {service} log destinations do not match: {paths}")
        if not qa_observability and any(
                key in service_text["wallpaperagentd"] for key in ("output_log=", "error_log=")):
            raise SystemExit("production wallpaperagentd must retain its existing logging configuration")
        runtime = service_text["openwave-runtime"]
        udev = service_text["udevd"]
        if 'command_args="--resolve-names=early"' not in udev:
            raise SystemExit("udevd must resolve user/group names early so DRM GROUP rules apply")
        if "--resolve-names=never" in udev:
            raise SystemExit("udevd --resolve-names=never disables the DRM group ownership rules")
        if "need localmount" not in runtime or "stop() { return 0; }" not in runtime:
            raise SystemExit("runtime provisioning must follow local mounts and retain its paths on stop")
        if "keyword -shutdown" in runtime:
            raise SystemExit("runtime provisioning must complete normal shutdown dependency bookkeeping")
        if re.search(r"(?m)^\s*before\s+.*\budevd\b", runtime):
            raise SystemExit("udevd ordering must come from its hard runtime dependency")
        if "need openwave-runtime" in udev or "need devfs sysfs" not in udev:
            raise SystemExit("udevd must require device filesystems without depending on localmount runtime provisioning")
        for directory in ("/run/openwave", "/run/udev"):
            if f"checkpath -d -o root:root -m 0755 {directory} || return 1" not in udev:
                raise SystemExit(f"udevd must provision its early runtime directory: {directory}")
        if "udevadm trigger --action=add" not in udev or "udevadm settle" not in udev:
            raise SystemExit("eudev must replay coldplug events and settle before dependent services")
        udevadm = candidate_root / "usr/bin/udevadm"
        if not udevadm.is_file() or not os.access(udevadm, os.X_OK):
            raise SystemExit("the eudev coldplug executable is missing")
        wait_dbus = candidate_root / "System/Services/openwave-wait-dbus"
        if not wait_dbus.is_file() or not os.access(wait_dbus, os.X_OK):
            raise SystemExit("bounded D-Bus readiness helper is missing")
        identity_helper = candidate_root / "System/Services/openwave-register-service-identity"
        if not identity_helper.is_file() or not os.access(identity_helper, os.X_OK):
            raise SystemExit("supervised-service identity registrar is missing")
        identity_services = {
            "appd": "appd", "permissiond": "permissiond", "openwave-portal": "portal",
            "promptd": "promptd", "wallpaperagentd": "wallpaperagentd", "powerd": "powerd",
            "connectivityd": "connectivityd",
        }
        for service, identity in identity_services.items():
            script = (candidate_root / "etc/init.d" / service).read_text()
            if f"openwave-register-service-identity {identity} || return 1" not in script:
                raise SystemExit(f"service {service} does not register its supervised identity")
            invalidate = f"openwave-register-service-identity --invalidate {identity}"
            if f"{invalidate} || return 1" not in script:
                raise SystemExit(f"service {service} does not invalidate identity before restart")
            if invalidate not in script.split("stop_post()", 1)[-1]:
                raise SystemExit(f"service {service} does not remove identity after stop")
            if script.index(f"{invalidate} || return 1") > script.index("start_post()"):
                raise SystemExit(f"service {service} invalidates identity after process startup")
            if script.index(f"openwave-register-service-identity {identity} || return 1") < script.index("start_post()"):
                raise SystemExit(f"service {service} registers identity before supervised process startup")
        policy_contexts = candidate_root / "etc/selinux/openwave/contexts/files/file_contexts"
        expected_identity_context = (
            "/System/Services/openwave-register-service-identity "
            "system_u:object_r:openwave_service_identity_exec_t:s0")
        if policy_contexts.read_text().splitlines().count(expected_identity_context) != 1:
            raise SystemExit("identity registrar has no dedicated SELinux executable label")
        kms = json.loads((candidate_root / "etc/openwave/kms.json").read_text())
        if not any(output.get("name") == "Virtual1" for output in kms.get("outputs", [])):
            raise SystemExit("Qt KMS config must target Virtual1 (Qt concatenates connector type and ID; kernel name is Virtual-1)")
        for utility in ("sed", "sync", "sleep", "tail"):
            path = candidate_root / "bin" / utility
            if not path.is_symlink() or path.resolve() != busybox.resolve():
                raise SystemExit(f"required BusyBox shutdown/observer utility is not staged: {utility}")
        appd_config = candidate_root / "etc/openwave/appd.conf"
        if not appd_config.is_file() or not re.search(r"(?m)^User=ocean$", appd_config.read_text()):
            raise SystemExit("appd must resolve its trusted session account as ocean")
        for path, mode, expected_type in (
            (candidate_root / "etc", 0o755, "directory"),
            (candidate_root / "etc/openwave", 0o755, "directory"),
            (candidate_root / "etc/dbus-1", 0o755, "directory"),
            (candidate_root / "etc/selinux", 0o755, "directory"),
            (candidate_root / "etc/selinux/openwave", 0o755, "directory"),
            (candidate_root / "etc/selinux/openwave/policy", 0o755, "directory"),
            (candidate_root / "etc/selinux/openwave/contexts", 0o755, "directory"),
            (candidate_root / "etc/selinux/openwave/contexts/files", 0o755, "directory"),
            (appd_config, 0o644, "file"),
            (candidate_root / "etc/selinux/config", 0o644, "file"),
            (candidate_root / "etc/selinux/openwave/policy/policy.35", 0o644, "file"),
            (candidate_root / "etc/selinux/openwave/contexts/files/file_contexts", 0o644, "file"),
        ):
            try:
                info = path.lstat()
            except FileNotFoundError as error:
                raise SystemExit(f"trusted appd metadata path is missing: {path}") from error
            if path.is_symlink() or (expected_type == "directory" and not path.is_dir()) \
                    or (expected_type == "file" and not path.is_file()):
                raise SystemExit(f"trusted appd metadata path has an invalid object type: {path}")
            if stat.S_IMODE(info.st_mode) != mode:
                raise SystemExit(f"trusted appd metadata path has mode {stat.S_IMODE(info.st_mode):04o}: {path}")
        for path in (candidate_root / "etc/dbus-1").glob("*.conf"):
            info = path.lstat()
            if path.is_symlink() or not path.is_file() or stat.S_IMODE(info.st_mode) != 0o644:
                raise SystemExit(f"trusted D-Bus configuration has invalid type or mode: {path}")
        passwd = candidate_root / "etc/passwd"
        accounts = [line.split(":") for line in passwd.read_text().splitlines() if line.startswith("ocean:")]
        if len(accounts) != 1 or len(accounts[0]) < 4 or accounts[0][2:4] != ["1000", "1000"]:
            raise SystemExit("the candidate must provide the ocean account expected by its service definitions")
        group_lines = (candidate_root / "etc/group").read_text().splitlines()
        if "video:x:100:ocean" not in group_lines or "render:x:102:ocean" not in group_lines:
            raise SystemExit("Ocean must be a member of both video and render groups")
        drm_rules = candidate_root / "etc/udev/rules.d/70-openwave-drm.rules"
        if drm_rules.is_symlink() or not drm_rules.is_file() or stat.S_IMODE(drm_rules.stat().st_mode) != 0o644:
            raise SystemExit("OpenWave DRM eudev rule is missing or has unsafe metadata")
        drm_rule_text = drm_rules.read_text()
        expected_drm_rules = {
            'SUBSYSTEM=="drm", KERNEL=="card[0-9]*", GROUP="video", MODE="0660"',
            'SUBSYSTEM=="drm", KERNEL=="renderD[0-9]*", GROUP="render", MODE="0660"',
        }
        if set(line for line in drm_rule_text.splitlines() if line and not line.startswith("#")) != expected_drm_rules:
            raise SystemExit("OpenWave DRM eudev rule does not enforce the primary/render group and mode policy")
        dbus_policy = candidate_root / "usr/share/dbus-1/system.d/org.openwave.Applications.conf"
        dbus_policy_text = dbus_policy.read_text() if dbus_policy.is_file() else ""
        if '<policy user="ocean">' not in dbus_policy_text \
                or '<allow own="org.openwave.Shell"/>' not in dbus_policy_text:
            raise SystemExit("system D-Bus policy must grant the dedicated shell name to ocean only")
        pid_contracts = {
            "appd": "/run/openwave/appd.pid",
            "permissiond": "/run/openwave/permissiond.pid",
            "promptd": "/run/openwave/promptd.pid",
            "wallpaperagentd": "/run/openwave/wallpaper.pid",
            "openwave-portal": "/run/openwave/portal.pid",
            "openwave-ocean": "/run/openwave/ocean.pid",
        }
        for name, expected_pidfile in pid_contracts.items():
            if f'pidfile="{expected_pidfile}"' not in service_text[name] or 'command_background="yes"' not in service_text[name]:
                raise SystemExit(f"service {name} must retain its authenticated child PID contract {expected_pidfile}")
        if "waydroid" in enabled["default"] or "waydroid-runtime" in enabled["default"]:
            raise SystemExit("optional Waydroid service is enabled in the default runlevel")
        power_rule = candidate_root / "etc/acpi/events/power"
        if not power_rule.is_file():
            raise SystemExit("missing ACPI power-button event rule")
        power_text = power_rule.read_text()
        if "event=button/power" not in power_text or "action=/sbin/openrc-shutdown -p now" not in power_text:
            raise SystemExit("ACPI power-button rule does not invoke the fixed OpenRC poweroff command")
        acpid_service = (candidate_root / "etc/init.d/acpid").read_text()
        if 'command="/usr/sbin/acpid"' not in acpid_service or 'command_args_foreground="-n -f"' not in acpid_service or 'supervisor=supervise-daemon' not in acpid_service:
            raise SystemExit("acpid service must run upstream acpid foreground with input/netlink mode")
        if "need devfs" not in acpid_service or "use sysfs" not in acpid_service:
            raise SystemExit("acpid service must wait for device filesystems")
        commands = set()
        for runlevel, names in enabled.items():
            for name in names:
                source = (candidate_root / "etc/init.d" / name).read_text()
                match = re.search(r'^command=["\']?([^"\'\n]+)', source, re.M)
                if match:
                    commands.add(match.group(1))
        library_dirs = (candidate_root / "lib64", candidate_root / "usr/lib64",
                        candidate_root / "lib", candidate_root / "usr/lib")
        commands.add('/sbin/ip')
        for command in sorted(commands):
            executable = candidate_root / command.lstrip("/")
            if not executable.is_file() or not os.access(executable, os.X_OK):
                raise SystemExit(f"enabled service command is missing or not executable: {command}")
            program = subprocess.run([str(readelf), "-l", str(executable)],
                                     capture_output=True, text=True, check=True).stdout
            interpreter = re.search(r"Requesting program interpreter: ([^]]+)", program)
            if interpreter:
                loader = candidate_root / interpreter.group(1).lstrip("/")
                if not loader.is_file():
                    raise SystemExit(f"ELF interpreter is missing for {command}: {interpreter.group(1)}")
            dynamic = subprocess.run([str(readelf), "-d", str(executable)],
                                     capture_output=True, text=True, check=True).stdout
            for soname in re.findall(r"Shared library: \[([^]]+)\]", dynamic):
                if not any((directory / soname).is_file() for directory in library_dirs):
                    raise SystemExit(f"ELF dependency {soname} is missing for {command}")
        disabled = [line.split() for line in (ROOT / "userspace/openrc/disabled-services.txt").read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith("#")]
        for runlevel, service in disabled:
            if os.path.lexists(candidate_root / "etc/runlevels" / runlevel / service):
                raise SystemExit(f"disabled service remains enabled: {runlevel}/{service}")
        if os.path.lexists(candidate_root / "etc/runlevels/default/waydroid"):
            raise SystemExit("optional Waydroid service is enabled in the default runlevel")
        fstab = candidate_root / "etc/fstab"
        fstab_entries = [line.split() for line in fstab.read_text().splitlines()
                         if line.strip() and not line.lstrip().startswith("#")]
        if fstab_entries != [["LABEL=OpenWave", "/", "ext4", "defaults", "0", "0"]]:
            raise SystemExit("candidate fstab must describe only the already-mounted OpenWave ext4 root")
        for runlevel in ("sysinit", "boot", "default", "nonetwork"):
            if os.path.lexists(candidate_root / "etc/runlevels" / runlevel / "netmount"):
                raise SystemExit(f"unused upstream netmount remains active in {runlevel}")
        if not (candidate_root / "etc/init.d/netmount").is_file():
            raise SystemExit("upstream netmount init script must remain installed")

        selinux_config = candidate_root / "etc/selinux/config"
        if selinux_config.read_text().splitlines().count("SELINUX=enforcing") != 1:
            raise SystemExit("candidate must contain one exact SELINUX=enforcing setting")
        observer_link = candidate_root / "etc/runlevels/default/openwave-qa-observer"
        if qa_observability:
            observer = candidate_root / "etc/init.d/openwave-qa-observer"
            if not observer.is_file() or not observer_link.is_symlink():
                raise SystemExit("QA observer must be installed and enabled only in the disposable QA runlevel")
            if os.readlink(observer_link) != "/etc/init.d/openwave-qa-observer":
                raise SystemExit("QA observer runlevel link does not target its installed service")
            subprocess.run([str(busybox), "ash", "-n", str(observer)], check=True)
            observer_text = observer.read_text()
            evidence_dir = candidate_root / "var/log/openwave-qa"
            if evidence_dir.is_symlink() or not evidence_dir.is_dir() or \
                    evidence_dir.stat().st_mode & 0o777 != 0o700:
                raise SystemExit("QA evidence directory must be a non-symlink mode 0700 directory")
            if "before local" not in observer_text or "/usr/bin/rc-status -a" not in observer_text:
                raise SystemExit("QA observer must capture service status before OpenRC local completes")
            for evidence in ("/run/dbus/system_bus_socket", "/run/user/1000/bus",
                             "/run/openwave/appd/ready", "catalog_api=ready",
                             "not-installed", "disabled", "starting", "failed", "running",
                             "org.freedesktop.DBus.GetNameOwner", "/proc/$pid/attr/current",
                             "/proc/$pid/status", "/proc/$pid/exe", "/proc/$pid/environ",
                             "Groups:*", "CapEff:*", "NoNewPrivs:*", "/bin/ps -o pid,user,group,comm,args",
                             "trusted_appd_identity=verified", "shell_graphical_readiness=",
                             "/var/log/openwave-qa/*.stdout.log", "shutdown_lock_observer=failed",
                             "holder_state=", "localmount_status=", "0:0 700", "service_logs:", "/bin/tail -n 80",
                             "/dev/input/event*", "/sys/fs/selinux/enforce"):
                if evidence not in observer_text:
                    raise SystemExit(f"QA observer does not capture required evidence: {evidence}")
            for service, names in {
                "appd": ("appd.stdout.log", "appd.stderr.log"),
                "wallpaperagentd": ("wallpaper.stdout.log", "wallpaper.stderr.log"),
            }.items():
                service_text = (candidate_root / "etc/init.d" / service).read_text()
                for key, name in zip(("output_log", "error_log"), names):
                    if f'{key}="/var/log/openwave-qa/{name}"' not in service_text:
                        raise SystemExit(f"QA {service} {key} does not use the protected evidence directory")
            ocean_service = (candidate_root / "etc/init.d/openwave-ocean").read_text()
            for key, name in (("output_log", "ocean.stdout.log"),
                              ("error_log", "ocean.stderr.log")):
                if f'{key}="/run/user/1000/openwave-logs/{name}"' not in ocean_service:
                    raise SystemExit(f"QA Ocean {key} must stay in its private user runtime directory")
            if "need openwave-runtime" not in ocean_service:
                raise SystemExit("Ocean must require runtime-directory provisioning before startup")
            contexts = (candidate_root / "etc/selinux/openwave/contexts/files/file_contexts").read_text()
            if "/run/user/1000/openwave-logs(/.*)? system_u:object_r:openwave_qa_evidence_t:s0" not in contexts:
                raise SystemExit("Ocean's user-owned log directory must receive its dedicated evidence label")
            runtime_check = candidate_root / "System/Services/openwave-runtime-check"
            if not runtime_check.is_file() or not os.access(runtime_check, os.X_OK):
                raise SystemExit("compiled runtime-directory label checker is missing or not executable")
            runtime_check_source = (ROOT / "userspace/openrc/runtime-check.c").read_text()
            if '"/run/user/1000/openwave-logs", 1000, 1000, 0700, "system_u:object_r:openwave_qa_evidence_t:s0"' not in runtime_check_source:
                raise SystemExit("runtime-directory checker source does not validate the private Ocean evidence label")
            runtime_strings = subprocess.run(
                ["strings", str(runtime_check)], capture_output=True, text=True, check=True).stdout
            if "system_u:object_r:openwave_qa_evidence_t:s0" not in runtime_strings:
                raise SystemExit("packed runtime-directory checker does not validate the Ocean evidence label")
            qa_applets = {"awk", "od", "sha256sum", "stat", "tr", "xxd"}
            missing_qa_applets = qa_applets - applets
            if missing_qa_applets:
                raise SystemExit(f"BusyBox lacks QA observer applets: {sorted(missing_qa_applets)}")
            for command in ("gdbus",):
                path = candidate_root / "usr/bin" / command
                if not path.is_file() or not os.access(path, os.X_OK):
                    raise SystemExit(f"QA observer dependency is missing: {path}")
            if 'rc_logger="YES"' not in (candidate_root / "etc/rc.conf").read_text():
                raise SystemExit("disposable QA candidate must enable OpenRC's standard rc.log capture")
        elif observer_link.exists():
            raise SystemExit("QA observer must not be enabled in a non-QA candidate")
        # Either BusyBox or target iproute2 is valid. The ELF dependency check
        # above catches an unstaged libmnl for the latter.
        for path in (candidate_root / "sbin/openrc-init", candidate_root / "sbin/openrc-shutdown",
                     candidate_root / "sbin/start-stop-daemon",
                     candidate_root / "sbin/supervise-daemon", candidate_root / "sbin/ip"):
            if not path.is_file() or not os.access(path, os.X_OK):
                raise SystemExit(f"missing required executable: {path}")
        acpid_binary = candidate_root / "usr/sbin/acpid"
        if not acpid_binary.is_file() or not os.access(acpid_binary, os.X_OK):
            raise SystemExit("missing upstream acpid executable")
        if (candidate_root / "bin/acpid").exists() and (candidate_root / "bin/acpid").resolve() == busybox.resolve():
            raise SystemExit("legacy BusyBox acpid applet must not shadow upstream acpid")
        for applet in ("mount", "umount", "swapon", "swapoff", "sysctl", "start-stop-daemon", "setfont"):
            applet_path = candidate_root / "bin" / applet
            if applet_path.exists() and applet_path.resolve() == busybox.resolve():
                raise SystemExit(f"incompatible BusyBox applet shadows standalone utility: {applet}")
        for path in (candidate_root / "usr/bin/mount", candidate_root / "usr/bin/umount"):
            if not path.is_file() or not os.access(path, os.X_OK):
                raise SystemExit(f"missing util-linux mount utility: {path}")
        print(f"OPENRC_CANDIDATE_VALIDATION PASS enabled_services={sum(map(len, enabled.values()))} command_ELFs={len(commands)} disabled={len(disabled)}")
    print(f"OPENRC_HOST_VALIDATION PASS scripts={len(services)} dependency_graph=acyclic waydroid=optional")


if __name__ == "__main__":
    main()
