#!/usr/bin/env python3
"""Enable the narrowly scoped wallpaper diagnostics in a disposable root."""

from pathlib import Path
import argparse


SERVICE_FLAGS = {
    "appd": ("OPENWAVE_QA_IDENTITY=1",),
    "wallpaperagentd": ("OPENWAVE_QA_IDENTITY=1",),
    "openwave-ocean": (
        "OPENWAVE_QA_GRAPHICS=1",
        "OPENWAVE_QA_WALLPAPER_IMAGE=bundled-svg",
        "QT_QPA_EGLFS_DEBUG=1",
        "QT_LOGGING_RULES=qt.qpa.eglfs.kms=true",
    ),
    "openwave-qa-observer": ("OPENWAVE_QA_SHUTDOWN=1",),
}

QA_LOGS = {
    "appd": ("appd.stdout.log", "appd.stderr.log"),
    "wallpaperagentd": ("wallpaper.stdout.log", "wallpaper.stderr.log"),
}

OCEAN_LOGS = (
    "/run/user/1000/openwave-logs/ocean.stdout.log",
    "/run/user/1000/openwave-logs/ocean.stderr.log",
)


def configure(root: Path) -> None:
    root = root.resolve(strict=True)
    if not (root / "etc/selinux/config").is_file() or \
            "SELINUX=enforcing" not in (root / "etc/selinux/config").read_text():
        raise SystemExit("refusing QA overlay: SELinux enforcing candidate is required")
    if not (root / "etc/init.d/openwave-qa-observer").is_file():
        raise SystemExit("refusing QA overlay: disposable QA observer is missing")

    ocean_service = root / "etc/init.d/openwave-ocean"
    ocean_text = ocean_service.read_text()
    for key, expected in zip(("output_log", "error_log"), OCEAN_LOGS):
        values = [line.split("=", 1)[1].strip().strip('"')
                  for line in ocean_text.splitlines() if line.startswith(f"{key}=")]
        if values != [expected]:
            raise SystemExit(f"Ocean {key} must use its private user runtime log: {expected}")

    for service, assignments in SERVICE_FLAGS.items():
        path = root / "etc/init.d" / service
        if not path.is_file() or path.is_symlink():
            raise SystemExit(f"refusing QA overlay: trusted service file missing or symlinked: {path}")
        text = path.read_text()
        lines = text.splitlines()
        for assignment in assignments:
            key, value = assignment.split("=", 1)
            existing = [line for line in lines if line.startswith(f'export {key}="')]
            expected = f'export {key}="{value}"'
            if existing and existing != [expected]:
                raise SystemExit(f"conflicting QA setting in {path}: {key}")
            if not existing:
                insert_at = next((i + 1 for i, line in enumerate(lines)
                                  if line.startswith("description=")), None)
                if insert_at is None:
                    raise SystemExit(f"service has no description anchor: {path}")
                lines.insert(insert_at, expected)
        if service in QA_LOGS:
            # Root-owned services write diagnostic logs only to the protected
            # system evidence directory. Ocean keeps its private runtime logs.
            for key, filename in zip(("output_log", "error_log"), QA_LOGS[service]):
                expected = f'{key}="/var/log/openwave-qa/{filename}"'
                existing = [line for line in lines if line.startswith(f"{key}=")]
                if existing and existing != [expected]:
                    lines = [line for line in lines if not line.startswith(f"{key}=")]
                    existing = []
                if not existing:
                    anchor = next((i + 1 for i, line in enumerate(lines)
                                   if line.startswith("pidfile=")), None)
                    if anchor is None:
                        raise SystemExit(f"service has no pidfile anchor: {path}")
                    lines.insert(anchor, expected)
            if service == "wallpaperagentd" and not any(line == 'umask="0077"' for line in lines):
                anchor = next((i + 1 for i, line in enumerate(lines)
                               if line.startswith("pidfile=")), None)
                if anchor is None:
                    raise SystemExit(f"service has no pidfile anchor: {path}")
                lines.insert(anchor, 'umask="0077"')
        path.write_text("\n".join(lines) + "\n")

    # The same QA flag gates both the observer's startup diagnostic and its
    # bounded localmount-lock watcher.
    ocean = (root / "etc/init.d/openwave-ocean").read_text()
    appd = (root / "etc/init.d/appd").read_text()
    wallpaper = (root / "etc/init.d/wallpaperagentd").read_text()
    observer = (root / "etc/init.d/openwave-qa-observer").read_text()
    required = {
        "appd": (appd, "OPENWAVE_QA_IDENTITY"),
        "wallpaperagentd": (wallpaper, "OPENWAVE_QA_IDENTITY"),
        "Ocean": (ocean, "OPENWAVE_QA_GRAPHICS", "OPENWAVE_QA_WALLPAPER_IMAGE"),
        "shutdown observer": (observer, "OPENWAVE_QA_SHUTDOWN"),
    }
    for label, values in required.items():
        text, *keys = values
        if any(f'export {key}="' not in text for key in keys):
            raise SystemExit(f"QA environment not installed for {label}")

    for service, filenames in QA_LOGS.items():
        text = (root / "etc/init.d" / service).read_text()
        for key, filename in zip(("output_log", "error_log"), filenames):
            if f'{key}="/var/log/openwave-qa/{filename}"' not in text:
                raise SystemExit(f"QA evidence log destination missing for {service}: {key}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path,
                        help="disposable candidate root, never the production rootfs")
    args = parser.parse_args()
    configure(args.root)


if __name__ == "__main__":
    main()
