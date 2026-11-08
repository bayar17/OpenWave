#!/usr/bin/env python3
"""Host-only checks for disposable OpenRC fstab and startup observability."""

from pathlib import Path
import argparse
import hashlib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--image", type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)

    fstab = root / "etc/fstab"
    entries = [line.split() for line in fstab.read_text().splitlines()
               if line.strip() and not line.lstrip().startswith("#")]
    if entries != [["LABEL=OpenWave", "/", "ext4", "defaults", "0", "0"]]:
        raise SystemExit("fstab must describe only the already-mounted labeled ext4 root")

    runlevels = root / "etc/runlevels"
    for name in ("sysinit", "boot", "default", "nonetwork"):
        if (runlevels / name / "netmount").exists():
            raise SystemExit(f"netmount is still enabled in {name}")
    if not (root / "etc/init.d/netmount").is_file():
        raise SystemExit("upstream OpenRC netmount service was removed from the image")
    if not (runlevels / "default/local").is_symlink():
        raise SystemExit("upstream local service must remain enabled")

    observer = root / "etc/init.d/openwave-qa-observer"
    observer_link = runlevels / "default/openwave-qa-observer"
    if not observer.is_file() or not observer_link.is_symlink():
        raise SystemExit("QA observer service or default-runlevel link is missing")
    if os.readlink(observer_link) != "/etc/init.d/openwave-qa-observer":
        raise SystemExit("QA observer runlevel link has the wrong target")
    busybox = root / "bin/busybox"
    subprocess.run([str(busybox), "ash", "-n", str(observer)], check=True)
    source = observer.read_text()
    for required in (
        "before local", "/usr/bin/rc-status -a", "/sbin/rc-service",
        "/run/dbus/system_bus_socket", "/run/user/1000/bus",
        "/run/openwave/appd/ready", "catalog_api=ready",
        "not-installed", "disabled", "starting", "failed", "running",
        "org.freedesktop.DBus.GetNameOwner", "/proc/$pid/attr/current",
        "/proc/$pid/status", "/proc/$pid/exe", "/proc/$pid/environ", "Groups:*",
        "CapEff:*", "NoNewPrivs:*", "trusted_appd_identity=verified",
        "shell_graphical_readiness=", "logdir=/run/user/1000/openwave-logs",
        'for logfile in "$logdir/ocean.stdout.log" "$logdir/ocean.stderr.log"; do',
        "/bin/ps -o pid,user,group,comm,args", "/dev/input/event*", "/dev/dri/card*",
        "service_logs:", "/bin/tail -n 2000",
        "shutdown_service_logs_begin", "last 2000 lines at observer stop",
        "/sys/fs/selinux/enforce", "/var/log/openwave-qa/boot-status.txt",
        "/var/log/openwave-qa/*.stdout.log", "localmount_status=",
        "holder_state=", "shutdown_lock_observer=failed", "0:0 700",
    ):
        if required not in source:
            raise SystemExit(f"QA observer is missing evidence capture for {required}")
    state_fn = re.search(r"(?ms)^service_state\(\) \{.*?^\}", source)
    if not state_fn:
        raise SystemExit("QA observer has no explicit service-state classifier")
    stop_fn = re.search(r"(?ms)^stop\(\) \{.*?^\}", source)
    if not stop_fn:
        raise SystemExit("QA observer has no shutdown evidence hook")
    busybox = root / "bin/busybox"
    with tempfile.TemporaryDirectory(prefix="openwave-service-state-") as temporary:
        sandbox = Path(temporary)
        for old, new in (
            ("/etc/init.d", str(sandbox / "etc/init.d")),
            ("/etc/runlevels", str(sandbox / "etc/runlevels")),
            ("/run/openrc", str(sandbox / "run/openrc")),
        ):
            state_fn = re.sub(re.escape(old), new, state_fn.group(0) if old == "/etc/init.d" else text)
            text = state_fn
        for path in (sandbox / "etc/init.d", sandbox / "etc/runlevels/default",
                     sandbox / "run/openrc/starting", sandbox / "run/openrc/failed",
                     sandbox / "run/openrc/started"):
            path.mkdir(parents=True, exist_ok=True)
        (sandbox / "etc/init.d/disabled-service").touch()
        (sandbox / "etc/init.d/starting-service").touch()
        (sandbox / "etc/init.d/failed-service").touch()
        (sandbox / "etc/init.d/running-service").touch()
        (sandbox / "etc/runlevels/default/starting-service").symlink_to("/etc/init.d/starting-service")
        (sandbox / "etc/runlevels/default/failed-service").symlink_to("/etc/init.d/failed-service")
        (sandbox / "etc/runlevels/default/running-service").symlink_to("/etc/init.d/running-service")
        (sandbox / "run/openrc/starting/starting-service").touch()
        (sandbox / "run/openrc/failed/failed-service").touch()
        (sandbox / "run/openrc/started/running-service").touch()
        for service, expected in (
            ("missing-service", "not-installed"), ("disabled-service", "disabled"),
            ("starting-service", "starting"), ("failed-service", "failed"),
            ("running-service", "running"),
        ):
            result = subprocess.run([str(busybox), "ash", "-c",
                                     f"{text}\nservice_state {service}"],
                                    capture_output=True, text=True, check=True)
            if result.stdout.strip() != expected:
                raise SystemExit(f"observer state classifier: {service} returned {result.stdout!r}")
    with tempfile.TemporaryDirectory(prefix="openwave-appd-log-capture-") as temporary:
        sandbox = Path(temporary)
        qa = sandbox / "var/log/openwave-qa"
        qa.mkdir(parents=True)
        status_file = qa / "boot-status.txt"
        status_file.write_text("initial-status\n")
        (qa / "localmount-lock-owner.log").write_text("")
        appd_log = qa / "appd.stderr.log"
        appd_log.write_text("".join(f"launch-record-{index:03d}\n" for index in range(160)))
        (sandbox / "run/openrc/exclusive").mkdir(parents=True)
        observer_stop = stop_fn.group(0).replace("/var/log/openwave-qa", str(qa)).replace(
            "/run/openrc", str(sandbox / "run/openrc"))
        result = subprocess.run(
            [str(busybox), "ash", "-c", f"{observer_stop}\nstop"],
            env={**os.environ, "OPENWAVE_QA_SHUTDOWN": "1"},
            capture_output=True, text=True, timeout=15,
        )
        if result.returncode:
            raise SystemExit(f"observer shutdown capture failed: {result.stderr}")
        captured = status_file.read_text()
        if "launch-record-000" not in captured or "launch-record-159" not in captured:
            raise SystemExit("observer shutdown capture truncated appd launch history")
    if 'rc_logger="YES"' not in (root / "etc/rc.conf").read_text():
        raise SystemExit("OpenRC built-in logging is not enabled in the QA image")
    qa_dir = root / "var/log/openwave-qa"
    if qa_dir.exists() and (qa_dir.is_symlink() or not qa_dir.is_dir()
                            or qa_dir.stat().st_mode & 0o777 != 0o700):
        raise SystemExit("QA evidence directory must be a non-symlink mode 0700 directory")

    if args.image:
        image = args.image.resolve(strict=True)
        label = subprocess.run(["dumpe2fs", "-h", str(image)], capture_output=True,
                               text=True, check=True).stdout
        if not re.search(r"Filesystem volume name:\s+OpenWave\b", label):
            raise SystemExit("packed image label does not match the fstab root source")
        debugfs = shutil_which("debugfs")
        for packed_path, expected in (
            ("/etc/fstab", fstab.read_text()),
            ("/etc/init.d/openwave-qa-observer", observer.read_text()),
            ("/etc/init.d/openwave-ocean", (root / "etc/init.d/openwave-ocean").read_text()),
            ("/etc/init.d/openwave-runtime", (root / "etc/init.d/openwave-runtime").read_text()),
            ("/etc/rc.conf", (root / "etc/rc.conf").read_text()),
            ("/etc/selinux/config", (root / "etc/selinux/config").read_text()),
        ):
            actual = subprocess.run([debugfs, "-R", f"cat {packed_path}", str(image)],
                                    capture_output=True, text=True, check=True).stdout
            if actual != expected:
                raise SystemExit(f"packed image differs from candidate staging: {packed_path}")
        for packed_path in ("/System/Services/openwave-runtime-check", "/System/Services/permissiond"):
            source_path = root / packed_path.lstrip("/")
            with tempfile.TemporaryDirectory(prefix="openwave-packed-service-") as temporary:
                extracted = Path(temporary) / "artifact"
                subprocess.run([debugfs, "-R", f"dump {packed_path} {extracted}", str(image)],
                               capture_output=True, text=True, check=True)
                if hashlib.sha256(extracted.read_bytes()).digest() != hashlib.sha256(source_path.read_bytes()).digest():
                    raise SystemExit(f"packed executable differs from candidate staging: {packed_path}")
            metadata = subprocess.run([debugfs, "-R", f"stat {packed_path}", str(image)],
                                      capture_output=True, text=True, check=True).stdout
            if "Mode:  0755" not in metadata or "User:     0   Group:     0" not in metadata:
                raise SystemExit(f"packed executable has unexpected owner or mode: {packed_path}")
        source_services = ROOT / "userspace/openrc/etc/init.d"
        qa_overlaid_services = {"appd", "wallpaperagentd", "openwave-ocean"}
        for service in source_services.iterdir():
            packed_path = f"/etc/init.d/{service.name}"
            actual = subprocess.run([debugfs, "-R", f"cat {packed_path}", str(image)],
                                    capture_output=True, text=True, check=True).stdout
            if service.name not in qa_overlaid_services and actual != service.read_text():
                raise SystemExit(f"packed image differs from OpenWave service source: {packed_path}")
        for service, logs in {
            "appd": ("appd.stdout.log", "appd.stderr.log"),
            "wallpaperagentd": ("wallpaper.stdout.log", "wallpaper.stderr.log"),
            "openwave-ocean": ("ocean.stdout.log", "ocean.stderr.log"),
        }.items():
            text = (root / "etc/init.d" / service).read_text()
            for key, filename in zip(("output_log", "error_log"), logs):
                if f'{key}="/var/log/openwave-qa/{filename}"' not in text:
                    raise SystemExit(f"QA overlay missing {service} {key} destination")
        if "SELINUX=enforcing" not in (root / "etc/selinux/config").read_text():
            raise SystemExit("candidate SELinux configuration is not enforcing")
        runlevel_listing = subprocess.run(
            [debugfs, "-R", "ls -l /etc/runlevels/default", str(image)],
            capture_output=True, text=True, check=True,
        ).stdout
        for service in ("openwave-qa-observer", "local", "dbus", "appd", "permissiond",
                        "promptd", "wallpaperagentd", "openwave-ocean", "bluetooth"):
            if not re.search(rf"\b{re.escape(service)}\s*$", runlevel_listing, re.M):
                raise SystemExit(f"packed default runlevel is missing {service}")
        if re.search(r"\b(netmount|waydroid|waydroid-runtime)\s*$", runlevel_listing, re.M):
            raise SystemExit("packed default runlevel includes disabled optional service")
        for packed_path in ("/etc/selinux/openwave/policy/policy.35",
                            "/etc/selinux/openwave/contexts/files/file_contexts"):
            source_path = root / packed_path.lstrip("/")
            with tempfile.TemporaryDirectory(prefix="openwave-image-check-") as temporary:
                extracted = Path(temporary) / "artifact"
                subprocess.run([debugfs, "-R", f"dump {packed_path} {extracted}", str(image)],
                               capture_output=True, text=True, check=True)
                if hashlib.sha256(extracted.read_bytes()).digest() != hashlib.sha256(source_path.read_bytes()).digest():
                    raise SystemExit(f"packed SELinux input differs from staging: {packed_path}")
            stat = subprocess.run([debugfs, "-R", f"stat {packed_path}", str(image)],
                                  capture_output=True, text=True, check=True).stdout
            if "Mode:  0644" not in stat or "User:     0   Group:     0" not in stat:
                raise SystemExit(f"packed SELinux input has unexpected ownership or mode: {packed_path}")
        print("OPENRC_PACKED_CONTENT_VALIDATION PASS fstab observer rc.conf enforcing policy file_contexts")

    print("OPENRC_BOOT_OBSERVABILITY_VALIDATION PASS fstab netmount_disabled observer rc_logger")


def shutil_which(name: str) -> str:
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(directory) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise SystemExit(f"required host validation tool not found: {name}")


if __name__ == "__main__":
    main()
