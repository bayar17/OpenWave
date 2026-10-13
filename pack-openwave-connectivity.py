#!/usr/bin/env python3
"""Pack the staged target connectivity runtime and Settings client while preserving image content."""

from pathlib import Path
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
IMAGE = ROOT / "OpenWave.img"
RUNTIME = ROOT / "rootfs"
READELF = ROOT / "toolchain/bin/llvm-readelf"
STAGING = runpy.run_path(str(ROOT / "stage-openwave-connectivity.py"))


def run(*args, check=True):
    result = subprocess.run(args, text=True, capture_output=True)
    if check and result.returncode:
        raise RuntimeError(f"{' '.join(map(str, args))}:\n{result.stdout}{result.stderr}")
    return result


def debugfs(image, command):
    result = run("debugfs", "-R", command, str(image))
    return result.stdout + result.stderr


def runtime_files():
    files = set(STAGING["PROGRAMS"]) | set(STAGING["CONFIGS"]) | {"init"}
    for directory in STAGING["CONFIG_DIRS"]:
        for path in (RUNTIME / directory).rglob("*"):
            if path.is_file() or path.is_symlink():
                files.add(str(path.relative_to(RUNTIME)))
    for path in (RUNTIME / "usr/lib64/udev").rglob("*"):
        if path.is_file() and path.read_bytes()[:4] == b"\x7fELF":
            files.add(str(path.relative_to(RUNTIME)))
    pending = [name for name in files if (RUNTIME / name).read_bytes()[:4] == b"\x7fELF"]
    while pending:
        relative = pending.pop()
        output = run(str(READELF), "-d", str(RUNTIME / relative)).stdout
        for soname in re.findall(r"Shared library: \[([^]]+)\]", output):
            for directory in ("usr/lib64", "usr/lib", "lib64"):
                candidate = RUNTIME / directory / soname
                if candidate.exists():
                    name = str(candidate.relative_to(RUNTIME))
                    if name not in files:
                        files.add(name)
                        pending.append(name)
                    break
            else:
                raise RuntimeError(f"Missing target library {soname} for {relative}")
    # SONAME entries are often symlinks. Include their versioned targets.
    for relative in list(files):
        source = RUNTIME / relative
        if source.is_symlink():
            target = source.resolve(strict=True)
            if not target.is_relative_to(RUNTIME):
                raise RuntimeError(f"Runtime symlink leaves staging tree: {source}")
            files.add(str(target.relative_to(RUNTIME)))
    return files


def ensure_directory(path, tree, mode=0o755):
    if path == tree or path.is_dir():
        return
    ensure_directory(path.parent, tree)
    path.mkdir(mode=mode)
    os.chown(path, 0, 0)


def overlay(tree, files):
    for relative in sorted(files):
        source, destination = RUNTIME / relative, tree / relative
        ensure_directory(destination.parent, tree)
        if destination.is_file() or destination.is_symlink():
            destination.unlink()
        if source.is_symlink():
            destination.symlink_to(os.readlink(source))
            os.chown(destination, 0, 0, follow_symlinks=False)
        elif source.is_file():
            shutil.copy2(source, destination)
            os.chown(destination, 0, 0)
        else:
            raise RuntimeError(f"Missing staged runtime file: {source}")
    for relative, mode in (("etc/NetworkManager/system-connections", 0o700),
                           ("etc/avahi/services", 0o755),
                           ("var/lib/NetworkManager", 0o700),
                           ("var/lib/iwd", 0o700),
                           ("var/lib/bluetooth", 0o700),
                           ("var/lib/dbus", 0o755)):
        directory = tree / relative
        ensure_directory(directory, tree, mode)
        directory.chmod(mode)
        os.chown(directory, 0, 0)
    link = tree / "var/lib/dbus/machine-id"
    if not link.exists() and not link.is_symlink():
        link.symlink_to("/etc/machine-id")
        os.chown(link, 0, 0, follow_symlinks=False)


def filesystem_uuid(image):
    output = run("dumpe2fs", "-h", str(image)).stdout
    match = re.search(r"^Filesystem UUID:\s+(\S+)", output, re.M)
    if not match:
        raise RuntimeError("Cannot read image UUID")
    return match.group(1)


def validate(image, files):
    # Repairs to an unpublished candidate are evidence of a failed pack.
    check = run("e2fsck", "-fy", str(image), check=False)
    if check.returncode:
        raise RuntimeError(f"Candidate needed filesystem repair:\n{check.stdout}{check.stderr}")
    run("e2fsck", "-fn", str(image))
    for relative in files:
        if "Inode:" not in debugfs(image, f"stat /{relative}"):
            raise RuntimeError(f"Packed file missing: {relative}")
    plugin_dir = "usr/lib64/NetworkManager/1.50.0"
    for plugin in ("libnm-device-plugin-wifi.so", "libnm-device-plugin-adsl.so"):
        if not re.search(r"User:\s*0\s+Group:\s*0\b",
                         debugfs(image, f"stat /{plugin_dir}/{plugin}")):
            raise RuntimeError(f"NetworkManager plugin must be owned by root: {plugin}")
    for relative in ("init", "usr/bin/ocean-settings", "usr/sbin/NetworkManager", "usr/sbin/avahi-daemon", "usr/libexec/iwd",
                     "usr/libexec/bluetooth/bluetoothd", "usr/libexec/connectivityd",
                     "etc/NetworkManager/NetworkManager.conf",
                     "etc/avahi/avahi-daemon.conf",
                     "usr/share/dbus-1/system.d/org.openwave.Connectivity.conf"):
        with tempfile.NamedTemporaryFile() as output:
            debugfs(image, f"dump /{relative} {output.name}")
            if Path(output.name).read_bytes() != (RUNTIME / relative).read_bytes():
                raise RuntimeError(f"Packed file differs from staging: {relative}")


def main():
    if not os.environ.get("FAKEROOTKEY") and os.geteuid() != 0:
        os.execvp("fakeroot", ["fakeroot", "--", sys.executable, __file__])
    if not IMAGE.is_file() or not RUNTIME.is_dir():
        raise RuntimeError("Missing OpenWave.img or staged rootfs")
    if run("fuser", str(IMAGE), check=False).returncode == 0:
        raise RuntimeError("OpenWave.img is in use")
    if run("e2fsck", "-fn", str(IMAGE), check=False).returncode:
        raise RuntimeError("Existing image failed filesystem check")
    files = runtime_files()
    backup = ROOT / "out/OpenWave-before-connectivity.img"
    if backup.exists() and not backup.samefile(IMAGE):
        stamp = time.strftime("%Y%m%d-%H%M%S")
        backup = ROOT / f"out/OpenWave-before-repack-{stamp}-{os.getpid()}.img"
    with tempfile.TemporaryDirectory(prefix="openwave-repack-", dir=ROOT / "out") as work:
        tree = Path(work) / "tree"
        tree.mkdir()
        debugfs(IMAGE, f"rdump / {tree}")
        overlay(tree, files)
        candidate = Path(work) / "OpenWave.img"
        with candidate.open("wb") as output:
            output.truncate(IMAGE.stat().st_size)
        run("mke2fs", "-q", "-t", "ext4", "-L", "OpenWave", "-U",
            filesystem_uuid(IMAGE), "-d", str(tree), str(candidate))
        validate(candidate, files)
        candidate.chmod(IMAGE.stat().st_mode & 0o7777)
        if run("fuser", str(IMAGE), check=False).returncode == 0:
            raise RuntimeError("OpenWave.img became busy")
        if not backup.exists():
            os.link(IMAGE, backup)
        candidate.replace(IMAGE)
    print(f"Repacked {IMAGE} with {len(files)} target runtime files; old image: {backup}")


if __name__ == "__main__":
    main()
