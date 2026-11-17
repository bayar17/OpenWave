#!/usr/bin/env python3
import argparse
import fnmatch
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    sdk = ROOT / 'toolchain/sysroot' / args.target / 'usr'
    keep = ROOT / 'out/os-sdk/sysroot' / args.target / 'usr'
    patterns = [line.strip() for line in (ROOT / 'scripts/sdk-sysroot-denylist.txt').read_text().splitlines()
                if line.strip() and not line.startswith('#')]
    victims = []
    for pattern in patterns:
        directory, _, leaf = pattern.rpartition('/')
        base = sdk / directory
        if not base.is_dir():
            continue
        for entry in sorted(base.iterdir()):
            if fnmatch.fnmatch(entry.name, leaf):
                victims.append(entry)
    victims = sorted(set(victims))
    missing = [v for v in victims if not (keep / v.relative_to(sdk)).exists() and not (keep / v.relative_to(sdk)).is_symlink()]
    if missing:
        sys.exit('out/os-sdk lacks copies of: ' + ', '.join(str(m.relative_to(sdk)) for m in missing[:5]) + ' ...')
    size = 0
    for victim in victims:
        if victim.is_dir() and not victim.is_symlink():
            size += sum(f.stat().st_size for f in victim.rglob('*') if f.is_file() and not f.is_symlink())
        elif victim.is_file() and not victim.is_symlink():
            size += victim.stat().st_size
    print(f'{len(victims)} entries, {size / 1e6:.0f} MB {"removed" if args.apply else "would be removed"} from toolchain/sysroot')
    if not args.apply:
        return 0
    for victim in victims:
        if victim.is_dir() and not victim.is_symlink():
            shutil.rmtree(victim, ignore_errors=True)
        else:
            victim.unlink(missing_ok=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
