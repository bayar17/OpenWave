#!/usr/bin/env python3
"""Apply account-aware ownership in a fakeroot image-population session ONLY."""
from pathlib import Path
import os
import re
import stat
import sys


def no_symlink_path(root: Path, relative: str) -> Path:
    current = root
    for component in Path(relative).parts:
        current = current / component
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            break
        if stat.S_ISLNK(mode):
            raise RuntimeError(f"Unsafe symlink in ownership path: {current}")
    return root / relative


def accounts(root: Path) -> dict[str, tuple[int, int]]:
    no_symlink_path(root, "etc")
    passwd = root / "etc/passwd"
    if passwd.is_symlink():
        if os.readlink(passwd) != "../var/lib/openwave-accounts/passwd":
            raise RuntimeError("Unexpected passwd symlink in ownership model")
        passwd = no_symlink_path(root, "var/lib/openwave-accounts/passwd")
    else:
        passwd = no_symlink_path(root, "etc/passwd")
    if not passwd.is_file():
        raise RuntimeError("Missing rootfs/etc/passwd")
    result = {}
    for line in passwd.read_text().splitlines():
        parts = line.split(":")
        if len(parts) >= 7 and parts[2].isdigit() and parts[3].isdigit() \
                and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", parts[0]):
            result[parts[0]] = (int(parts[2]), int(parts[3]))
    return result


def chown_tree(path: Path, uid: int, gid: int) -> None:
    if path.is_symlink():
        raise RuntimeError(f"Refusing symlinked ownership root: {path}")
    os.lchown(path, uid, gid)
    for base, dirs, files in os.walk(path, topdown=True, followlinks=False):
        for name in dirs + files:
            entry = Path(base) / name
            os.lchown(entry, uid, gid)
        dirs[:] = [d for d in dirs if not (Path(base) / d).is_symlink()]


def main() -> int:
    if "FAKEROOTKEY" not in os.environ:
        raise RuntimeError("Requires fakeroot; refusing to change host ownership")
    if len(sys.argv) != 2:
        raise RuntimeError("Usage: image-owners.py ROOTFS")
    root = Path(sys.argv[1]).resolve(strict=True)
    if not root.is_dir():
        raise RuntimeError("ROOTFS must be a directory")
    users = accounts(root)
    # Developer-owned staging files represent system installations. Preserve
    # other numeric owners rather than destroying service-specific metadata.
    if not all(os.environ.get(key, '').isdigit() for key in
               ('OPENWAVE_IMAGE_BUILDER_UID', 'OPENWAVE_IMAGE_BUILDER_GID')):
        raise RuntimeError('Pass the real builder UID/GID captured before entering fakeroot')
    builder_uid = int(os.environ['OPENWAVE_IMAGE_BUILDER_UID'])
    builder_gid = int(os.environ['OPENWAVE_IMAGE_BUILDER_GID'])
    for base, dirs, files in os.walk(root, followlinks=False):
        for entry in [Path(base)] + [Path(base) / name for name in dirs + files]:
            info = entry.lstat()
            uid = 0 if info.st_uid == builder_uid else info.st_uid
            gid = 0 if info.st_gid == builder_gid else info.st_gid
            if (uid, gid) != (info.st_uid, info.st_gid):
                os.lchown(entry, uid, gid)
    homes = no_symlink_path(root, "Data/Users")
    if homes.exists():
        if not homes.is_dir():
            raise RuntimeError("Data/Users is not a directory")
        for home in homes.iterdir():
            if home.is_symlink() or not home.is_dir():
                raise RuntimeError(f"Unexpected user home entry: {home}")
            if home.name not in users:
                raise RuntimeError(f"No account for /Data/Users/{home.name}")
            uid, gid = users[home.name]
            chown_tree(home, uid, gid)
            print(f"Image owner /Data/Users/{home.name}: {uid}:{gid}")
    if "ocean" in users:
        uid, gid = users["ocean"]
        for relative in ("var/lib/ocean", "run/user/1000"):
            target = no_symlink_path(root, relative)
            if target.exists():
                if not target.is_dir():
                    raise RuntimeError(f"Not a directory: {relative}")
                if relative == "run/user/1000" and uid != 1000:
                    raise RuntimeError("run/user/1000 requires ocean UID 1000")
                chown_tree(target, uid, gid)
                print(f"Image owner /{relative}: {uid}:{gid}")
    print("PASS: account-aware image ownership assigned inside fakeroot")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
