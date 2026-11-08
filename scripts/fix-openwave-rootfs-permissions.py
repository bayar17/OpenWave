#!/usr/bin/env python3
"""OpenWave rootfs mode checks and fakeroot-only ownership exceptions.

This utility NEVER follows rootfs symlinks (including merged-/usr and guest-absolute
symlinks), and does not change real host file ownership. This is deliberately
separate from SELinux policy installation/labeling.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import stat
import sys

# Mirrors trusted paths checked by userspace/openrc/validate_openrc.py and
# baseline filesystem conventions. Do not blanket-chmod system binaries.
DIRECTORY_MODES = {
    ".": 0o755,
    "etc": 0o755,
    "etc/openwave": 0o755,
    "etc/dbus-1": 0o755,
    "etc/selinux": 0o755,
    "etc/selinux/openwave": 0o755,
    "etc/selinux/openwave/policy": 0o755,
    "etc/selinux/openwave/contexts": 0o755,
    "etc/selinux/openwave/contexts/files": 0o755,
    "usr": 0o755,
    "usr/bin": 0o755,
    "usr/share": 0o755,
    "System": 0o755,
    "System/Services": 0o755,
    "Data": 0o755,
    "Data/Users": 0o755,
    "var": 0o755,
    "var/lib": 0o755,
    "var/lib/openwave": 0o700,
    "var/lib/openwave/state": 0o700,
    "var/lib/ocean": 0o750,
    "var/lib/ocean/.config": 0o750,
    "var/lib/ocean/.local": 0o750,
    "var/lib/ocean/.local/state": 0o750,
    "run": 0o755,
    "run/openwave": 0o755,
    "run/user": 0o755,
    "run/user/1000": 0o700,
    "run/user/1000/openwave-logs": 0o700,
    "tmp": 0o1777,
    "var/tmp": 0o1777,
}
FILE_MODES = {
    "etc/passwd": 0o644,
    "etc/group": 0o644,
    "etc/shadow": 0o600,
    "etc/gshadow": 0o600,
    "etc/openwave/appd.conf": 0o644,
    "etc/selinux/config": 0o644,
    "etc/selinux/openwave/policy/policy.35": 0o644,
    "etc/selinux/openwave/contexts/files/file_contexts": 0o644,
    "etc/selinux/openwave/contexts/files/file_contexts.bin": 0o644,
    "etc/udev/rules.d/70-openwave-drm.rules": 0o644,
}
SYMLINK_DIRS = {
    "bin": {"usr/bin", "/usr/bin"},
    "sbin": {"usr/sbin", "/usr/sbin"},
    "lib": {"usr/lib", "/usr/lib"},
    "lib64": {"usr/lib64", "/usr/lib64"},
}


def logical_stat(root: Path, relative: str, expected: str) -> os.stat_result | None:
    """Inspect a node without traversing any symbolic-link ancestor."""
    current = root
    for part in Path(relative).parts:
        current = current / part
        try:
            st = current.lstat()
        except FileNotFoundError:
            return None
        if stat.S_ISLNK(st.st_mode):
            raise ValueError(f"Refusing symlink in protected path: {current}")
        if current != root / relative and not stat.S_ISDIR(st.st_mode):
            raise ValueError(f"Non-directory in protected path: {current}")
    st = (root / relative).lstat() if relative != "." else root.lstat()
    good = stat.S_ISDIR(st.st_mode) if expected == "directory" else stat.S_ISREG(st.st_mode)
    if not good:
        raise ValueError(f"Wrong object type for protected {expected}: {root / relative}")
    return st


def check_modes(root: Path, apply: bool) -> int:
    failures = 0
    fatal = 0
    for rel in ("bin", "sbin", "lib", "lib64"):
        link = root / rel
        if link.is_symlink():
            dest = os.readlink(link)
            if dest not in SYMLINK_DIRS[rel]:
                print(f"ERROR: unrecognized merged-/usr link {rel} -> {dest}", file=sys.stderr)
                failures += 1
                fatal += 1
    for rel, expected, typ in (
        *((k, v, "directory") for k, v in DIRECTORY_MODES.items()),
        *((k, v, "file") for k, v in FILE_MODES.items()),
    ):
        try:
            st = logical_stat(root, rel, typ)
        except (ValueError, OSError) as err:
            print(f"ERROR: {err}", file=sys.stderr)
            failures += 1
            fatal += 1
            continue
        if st is None:
            continue  # optional in partial/staged rootfs
        actual = stat.S_IMODE(st.st_mode)
        if actual != expected:
            print(f"{'FIX' if apply else 'BAD'}: {rel}: {actual:04o} -> {expected:04o}")
            failures += 1
            if apply:
                try:
                    os.chmod(root / rel, expected, follow_symlinks=False)
                except OSError as err:
                    print(f"ERROR: cannot chmod {rel}: {err}", file=sys.stderr)
                    return 1
    # OpenRC service files need to be executable; do not change arbitrary files.
    initd = root / "etc/init.d"
    if initd.is_dir() and not initd.is_symlink():
        for service in initd.iterdir():
            st = service.lstat()
            if not stat.S_ISREG(st.st_mode):
                continue
            if not service.name or service.name.startswith("."):
                continue
            actual = stat.S_IMODE(st.st_mode)
            if actual != 0o755:
                print(f"{'FIX' if apply else 'BAD'}: etc/init.d/{service.name}: {actual:04o} -> 0755")
                failures += 1
                if apply:
                    os.chmod(service, 0o755)
    # Ensure installed per-user home directories cannot be browsed by others.
    homes = root / "Data/Users"
    if homes.is_dir() and not homes.is_symlink():
        for home in homes.iterdir():
            st = home.lstat()
            if stat.S_ISLNK(st.st_mode):
                print(f"ERROR: symlinked user home: {home}", file=sys.stderr)
                failures += 1
                fatal += 1
            elif stat.S_ISDIR(st.st_mode):
                actual = stat.S_IMODE(st.st_mode)
                if actual != 0o700:
                    print(f"{'FIX' if apply else 'BAD'}: Data/Users/{home.name}: {actual:04o} -> 0700")
                    failures += 1
                    if apply:
                        os.chmod(home, 0o700)
    # Protect imported D-Bus policy files; don't change unrelated configurations.
    dbus = root / "etc/dbus-1"
    if dbus.is_dir() and not dbus.is_symlink():
        for conf in dbus.glob("*.conf"):
            st = conf.lstat()
            if not stat.S_ISREG(st.st_mode):
                print(f"ERROR: unsafe D-Bus config type: {conf}", file=sys.stderr)
                failures += 1
                fatal += 1
            elif stat.S_IMODE(st.st_mode) != 0o644:
                print(f"{'FIX' if apply else 'BAD'}: {conf.relative_to(root)} -> 0644")
                failures += 1
                if apply:
                    os.chmod(conf, 0o644)
    # Generic safety audit. Unknown paths are reported rather than silently
    # chmodded: privileged files must be reviewed by their owning package.
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(base) / name
            st = p.lstat()
            if stat.S_ISLNK(st.st_mode):
                continue
            mode = stat.S_IMODE(st.st_mode)
            rel = p.relative_to(root).as_posix()
            if stat.S_ISREG(st.st_mode) and mode & 0o002:
                print(f"ERROR: world-writable file requires review: {rel} ({mode:04o})", file=sys.stderr)
                failures += 1
                fatal += 1
            if stat.S_ISDIR(st.st_mode) and mode & 0o002 and not mode & stat.S_ISVTX:
                print(f"ERROR: world-writable non-sticky directory: {rel} ({mode:04o})", file=sys.stderr)
                failures += 1
                fatal += 1
            if stat.S_ISREG(st.st_mode) and mode & stat.S_ISUID:
                if rel != "usr/libexec/dbus-daemon-launch-helper":
                    print(f"REVIEW: setuid binary (not modified): {rel} ({mode:04o})")
    if apply and not failures:
        print("No mode corrections were necessary.")
    elif apply:
        print("Mode corrections attempted; rerun --check to verify.")
    elif not failures:
        print("PASS: OpenWave rootfs known permission contracts.")
    return 0 if (apply and not fatal) or failures == 0 else 1


def accounts(root: Path) -> dict[str, tuple[int, int]]:
    passwd = root / "etc/passwd"
    if not passwd.is_file() or passwd.is_symlink():
        raise ValueError("Expected regular rootfs/etc/passwd for image ownership mapping")
    result = {}
    for line in passwd.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split(":")
        if len(cols) < 7 or not cols[2].isdigit() or not cols[3].isdigit():
            continue
        name = cols[0]
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", name):
            result[name] = (int(cols[2]), int(cols[3]))
    return result


def set_ownership_tree(path: Path, uid: int, gid: int) -> int:
    """fakeroot-only, never follows directory or file symlinks."""
    if path.is_symlink():
        raise ValueError(f"Refusing to traverse symlinked ownership root: {path}")
    if not path.exists():
        return 0
    count = 0
    for base, dirs, files in os.walk(path, topdown=True, followlinks=False):
        for name in [".", *dirs, *files]:
            target = Path(base) if name == "." else Path(base) / name
            os.lchown(target, uid, gid)
            count += 1
        dirs[:] = [d for d in dirs if not (Path(base) / d).is_symlink()]
    return count


def image_owners(root: Path) -> None:
    if not os.environ.get("FAKEROOTKEY"):
        raise ValueError("--image-owners may ONLY run inside fakeroot; real chown is forbidden")
    passwd = accounts(root)
    # Expected to run after the existing fakeroot chown-to-root step.
    homes = root / "Data/Users"
    if homes.is_dir() and not homes.is_symlink():
        for home in homes.iterdir():
            if home.is_symlink():
                raise ValueError(f"Unsafe symlinked user home: {home}")
            if home.is_dir():
                if home.name not in passwd:
                    raise ValueError(f"Rootfs user directory has no account: {home.name}")
                uid, gid = passwd[home.name]
                print(f"Image ownership: {home.relative_to(root)} -> {uid}:{gid}")
                set_ownership_tree(home, uid, gid)
    ocean = passwd.get("ocean")
    for rel in ("var/lib/ocean", "run/user/1000"):
        p = root / rel
        if p.exists():
            if ocean is None or ocean != (1000, 1000):
                raise ValueError(f"{rel} requires ocean account UID/GID 1000:1000")
            set_ownership_tree(p, *ocean)
    helper = root / "usr/libexec/dbus-daemon-launch-helper"
    if helper.exists() or helper.is_symlink():
        if helper.is_symlink() or not helper.is_file():
            raise ValueError("Unsafe D-Bus launch helper type")
        grp = root / "etc/group"
        groups = [s.split(":") for s in grp.read_text().splitlines() if s.startswith("messagebus:")]
        if len(groups) != 1 or not groups[0][2].isdigit():
            raise ValueError("Required messagebus group missing")
        os.lchown(helper, 0, int(groups[0][2]))
        os.chmod(helper, 0o4750)
    print("Image ownership exceptions applied inside fakeroot.")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("rootfs"))
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true")
    g.add_argument("--apply", action="store_true", help="Repair only explicitly defined POSIX modes")
    g.add_argument("--image-owners", action="store_true", help="Apply account ownership exceptions inside fakeroot")
    args = p.parse_args()
    root = args.root.absolute()
    if not root.is_dir() or root.is_symlink():
        p.error(f"Not a real rootfs directory: {root}")
    try:
        if args.image_owners:
            image_owners(root)
            return 0
        return check_modes(root, args.apply)
    except (ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
