#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SHARED = ('bin', 'lib', 'libexec', 'include', 'doc', 'metatypes', 'mkspecs', 'modules', 'plugins', 'qml', 'sbom', 'share')


def install_env(sdk, os_sdk):
    for prefix in (sdk, os_sdk):
        shutil.copy2(ROOT / 'scripts/sdk-env.sh', prefix / 'env.sh')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    parser.add_argument('--refresh', action='store_true', help='recreate out/os-sdk from toolchain/')
    args = parser.parse_args()
    sdk, os_sdk = ROOT / 'toolchain', ROOT / 'out/os-sdk'
    sysroot = os_sdk / 'sysroot' / args.target
    if sysroot.exists() and not args.refresh:
        install_env(sdk, os_sdk)
        print(f'{os_sdk} exists already (use --refresh to recreate it); env.sh refreshed')
        return 0
    if os_sdk.exists():
        shutil.rmtree(os_sdk)
    source = sdk / 'sysroot' / args.target
    if not (source / 'usr/include').is_dir():
        sys.exit(f'No sysroot to copy: {source}')
    os_sdk.mkdir(parents=True)
    for name in SHARED:
        if (sdk / name).exists():
            (os_sdk / name).symlink_to(os.path.relpath(sdk / name, os_sdk))
    (os_sdk / 'sysroot').mkdir()
    shutil.copytree(source, sysroot, symlinks=True)
    shutil.copytree(sdk / 'cmake', os_sdk / 'cmake')
    old = str(source).encode()
    new = str(sysroot).encode()
    changed = 0
    for path in sysroot.rglob('*'):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 4 << 20:
            continue
        data = path.read_bytes()
        if old in data and b'\0' not in data[:4096]:
            path.write_bytes(data.replace(old, new))
            changed += 1
    for path in (os_sdk / 'cmake').rglob('*'):
        if path.is_file() and path.suffix == '.ini':
            path.write_text(path.read_text().replace(str(source), str(sysroot)))
    install_env(sdk, os_sdk)
    print(f'created {os_sdk}: {changed} files re-pointed at the copy')
    return 0


if __name__ == '__main__':
    sys.exit(main())
