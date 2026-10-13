#!/usr/bin/env python3
"""Copy OpenWave connectivity runtime and its Settings client from fakeroot to rootfs.

The ELF dependency walk is confined to fakeroot. Missing target libraries stop
staging; nothing is ever resolved through the host's library directories.
"""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "fakeroot"
DEST = ROOT / "rootfs"
READELF = ROOT / "toolchain/bin/llvm-readelf"

PROGRAMS = [
    "usr/bin/dbus-daemon",
    "usr/bin/udevadm",
    "usr/sbin/udevd",
    "usr/sbin/NetworkManager",
    "usr/sbin/avahi-daemon",
    "usr/libexec/iwd",
    "usr/libexec/bluetooth/bluetoothd",
    "usr/libexec/connectivityd",
    "usr/bin/ocean-settings",
    "usr/libexec/nm-dhcp-helper",
    "usr/libexec/nm-priv-helper",
    "usr/libexec/nm-dispatcher",
    "usr/lib64/NetworkManager/1.50.0/libnm-device-plugin-adsl.so",
    "usr/lib64/NetworkManager/1.50.0/libnm-device-plugin-wifi.so",
]

CONFIGS = [
    "lib/firmware/rt2870.bin",
    "lib/firmware/regulatory.db",
    "lib/firmware/regulatory.db.p7s",
    "etc/NetworkManager/NetworkManager.conf",
    "etc/avahi/avahi-daemon.conf",
    "etc/iwd/main.conf",
    "etc/udev/udev.conf",
    "etc/passwd",
    "etc/group",
    "etc/machine-id",
    "usr/share/dbus-1/system.conf",
]

CONFIG_DIRS = [
    "etc/avahi/services",
    "etc/bluetooth",
    "usr/share/dbus-1/system.d",
    "usr/share/dbus-1/system-services",
    "usr/lib64/udev",
]


def needed(path: Path) -> list[str]:
    output = subprocess.check_output([READELF, "-d", path], text=True)
    return re.findall(r"Shared library: \[([^]]+)\]", output)


def resolve_library(name: str) -> Path:
    for directory in ("usr/lib64", "usr/lib", "lib64"):
        candidate = SOURCE / directory / name
        if candidate.exists():
            return candidate
    raise RuntimeError(f"Target library missing from fakeroot: {name}")


def copy_file(relative: Path) -> None:
    source = SOURCE / relative
    destination = DEST / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        link = os.readlink(source)
        if destination.is_symlink() or destination.is_file():
            destination.unlink()
        destination.symlink_to(link)
        resolved = source.resolve().relative_to(SOURCE)
        copy_file(resolved)
        return
    if not source.is_file():
        raise RuntimeError(f"Required runtime file is absent: {relative}")
    # Replacement works for pre-existing root-owned files in writable staging
    # directories. It does not modify the source development sysroot.
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        shutil.copy2(source, temporary_path)
        temporary_path.replace(destination)
    finally:
        temporary_path.unlink(missing_ok=True)


def main() -> None:
    runtime = set(PROGRAMS)
    runtime.add("init")
    for helper in (SOURCE / "usr/lib64/udev").rglob("*"):
        if helper.is_file() and helper.read_bytes()[:4] == b"\x7fELF":
            runtime.add(str(helper.relative_to(SOURCE)))

    pending = list(runtime)
    while pending:
        relative = pending.pop()
        source = SOURCE / relative
        if source.read_bytes()[:4] != b"\x7fELF":
            raise RuntimeError(f"Expected a target ELF: {relative}")
        for library in needed(source):
            library_path = resolve_library(library)
            library_relative = str(library_path.relative_to(SOURCE))
            if library_relative not in runtime:
                runtime.add(library_relative)
                pending.append(library_relative)

    for relative in sorted(runtime):
        copy_file(Path(relative))
    for relative in CONFIGS:
        copy_file(Path(relative))
    for relative in CONFIG_DIRS:
        shutil.copytree(SOURCE / relative, DEST / relative,
                        dirs_exist_ok=True, symlinks=True)

    profiles = DEST / "etc/NetworkManager/system-connections"
    profiles.mkdir(parents=True, exist_ok=True)
    profiles.chmod(0o700)
    (DEST / "etc/avahi/services").mkdir(parents=True, exist_ok=True)
    print(f"Staged {len(runtime)} target ELF paths and runtime configuration into {DEST}")


if __name__ == "__main__":
    main()
