#!/usr/bin/env python3
import argparse
import fnmatch
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]


def denied(relative, patterns):
    text = relative.as_posix()
    for pattern in patterns:
        directory, _, leaf = pattern.rpartition('/')
        parent = str(relative.parent.as_posix()) if relative.parent.as_posix() != '.' else ''
        parts = relative.parts
        for depth in range(len(parts)):
            head = '/'.join(parts[:depth])
            if head == directory and fnmatch.fnmatch(parts[depth], leaf):
                return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    source = ROOT / 'out/os-sdk/sysroot' / args.target
    destination = ROOT / 'toolchain/sysroot' / args.target
    if not source.is_dir():
        sys.exit(f'{source} does not exist (scripts/make-os-sdk.py)')
    patterns = [line.strip() for line in (ROOT / 'scripts/sdk-sysroot-denylist.txt').read_text().splitlines()
                if line.strip() and not line.startswith('#')]
    copied = 0
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        if denied(relative.relative_to('usr') if relative.parts[:1] == ('usr',) else relative, patterns) and relative.parts[:1] == ('usr',):
            continue
        target = destination / relative
        if path.is_dir() and not path.is_symlink():
            if not args.dry_run:
                target.mkdir(parents=True, exist_ok=True)
            continue
        if path.is_symlink():
            if target.is_symlink() and os.readlink(target) == os.readlink(path):
                continue
            if not args.dry_run:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.unlink(missing_ok=True)
                target.symlink_to(os.readlink(path))
            copied += 1
            continue
        stat = path.stat()
        translate = None
        if stat.st_size <= 4 << 20:
            data = path.read_bytes()
            old_path, new_path = str(source).encode(), str(destination).encode()
            if old_path in data and b'\0' not in data[:4096]:
                translate = data.replace(old_path, new_path)
        if translate is not None:
            if target.is_file() and target.read_bytes() == translate:
                continue
        elif target.is_file() and not target.is_symlink() and target.stat().st_size == stat.st_size \
                and target.stat().st_mtime_ns >= stat.st_mtime_ns:
            continue
        copied += 1
        if not args.dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            if translate is not None:
                target.write_bytes(translate)
            else:
                shutil.copy2(path, target)
    print(f'{copied} files {"would be " if args.dry_run else ""}synchronised into {destination}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
