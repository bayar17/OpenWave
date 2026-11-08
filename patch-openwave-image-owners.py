#!/usr/bin/env python3
"""Safely add account-aware fakeroot ownership to OpenWave's image builder.

This program edits only build-openwave and scripts/image-owners.py. It never
changes rootfs content or creates a disk image. It requires the known build_image
structure and refuses unknown layouts.
"""
from pathlib import Path
import argparse
import difflib
import os
import shutil
import stat
import subprocess
import tempfile

HELPER = '''#!/usr/bin/env python3
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
    passwd = no_symlink_path(root, "etc/passwd")
    if not passwd.is_file():
        raise RuntimeError("Missing rootfs/etc/passwd")
    result = {}
    for line in passwd.read_text().splitlines():
        parts = line.split(":")
        if len(parts) >= 7 and parts[2].isdigit() and parts[3].isdigit() \\
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
'''


def transform(content: str) -> str:
    start = content.find('build_image() (')
    end = content.find('\nbuild_test_permissions() (', start)
    if start < 0 or end < 0:
        raise RuntimeError('Could not isolate build_image() function; no changes made')
    block = content[start:end]
    old = '    mke2fs -q -t ext4 -L OpenWave -d "$1" "$2"\n'
    new = '    python3 "$3" "$1"\n' + old
    if block.count(old) != 1:
        raise RuntimeError('Unexpected mke2fs command; no changes made')
    if 'python3 "$3" "$1"' not in block:
        block = block.replace(old, new, 1)
    old_args = "' _ \"$RUNTIME\" \"$TEMP_IMAGE\""
    new_args = "' _ \"$RUNTIME\" \"$TEMP_IMAGE\" \"$ROOT/scripts/image-owners.py\""
    if block.count(old_args) != 1 and block.count(new_args) != 1:
        raise RuntimeError('Unexpected fakeroot argument list; no changes made')
    if new_args not in block:
        block = block.replace(old_args, new_args, 1)
    if 'find "$1" -print0 | xargs -0 -r chown -h 0:0' not in block:
        raise RuntimeError('Ownership baseline differs; no changes made')
    return content[:start] + block + content[end:]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo', type=Path, default=Path.cwd())
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    repo = args.repo.resolve()
    source = repo / 'build-openwave'
    target = repo / 'scripts/image-owners.py'
    if not source.is_file() or not (repo / 'userspace').is_dir():
        ap.error(f'Not an OpenWave checkout: {repo}')
    if target.exists() and target.read_text() != HELPER:
        ap.error(f'Existing helper differs; refusing to overwrite {target}')
    original = source.read_text()
    rewritten = transform(original)
    with tempfile.TemporaryDirectory(prefix='openwave-image-ownership-') as temp:
        test = Path(temp) / 'build-openwave'
        test.write_text(rewritten)
        subprocess.run(['bash', '-n', str(test)], check=True)
        htest = Path(temp) / 'image-owners.py'
        htest.write_text(HELPER)
        subprocess.run(['python3', '-m', 'py_compile', str(htest)], check=True)
    diff = ''.join(difflib.unified_diff(original.splitlines(True), rewritten.splitlines(True),
                                        fromfile='build-openwave', tofile='build-openwave (proposed)'))
    print(diff or 'build-openwave already patched.')
    print('Will install/verify:', target)
    if not args.apply:
        print('Read-only preview. Run with --apply to install.')
        return
    if rewritten == original and target.exists():
        print('Already applied; no changes made.')
        return
    if rewritten != original:
        backup = repo / 'out/backups/build-openwave-before-image-owners'
        backup.parent.mkdir(parents=True, exist_ok=True)
        if backup.exists():
            ap.error(f'Backup already exists: {backup}; preserve it before retrying')
        shutil.copy2(source, backup)
        print('Backup:', backup)
    target.parent.mkdir(exist_ok=True, parents=True)
    target.write_text(HELPER)
    target.chmod(0o755)
    if rewritten != original:
        fd, name = tempfile.mkstemp(prefix='.build-openwave.', dir=str(source.parent))
        try:
            os.fchmod(fd, stat.S_IMODE(source.stat().st_mode))
            with os.fdopen(fd, 'w') as stream:
                stream.write(rewritten)
            os.replace(name, source)
        finally:
            if os.path.exists(name):
                os.unlink(name)
    print('PASS: build_image() now reapplies guest ownership exceptions inside fakeroot')
    print('No rootfs files or disk images changed.')


if __name__ == '__main__':
    main()
