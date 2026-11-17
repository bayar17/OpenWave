#!/usr/bin/env python3
import argparse
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import sdk_relocatable

EXCLUDED = ('cmake/openwave-x86_64.ini', 'cmake/openwave-aarch64.ini', 'share/openwave/relocate.json',
            'bin/openwave-sdk-relocate')


def check_links(sdk):
    problems = []
    count = 0
    for path in sdk.rglob('*'):
        if not path.is_symlink():
            continue
        count += 1
        target = os.readlink(path)
        if os.path.isabs(target):
            problems.append(f'absolute link {path.relative_to(sdk)} -> {target}')
            continue
        resolved = Path(os.path.normpath(path.parent / target))
        try:
            resolved.relative_to(sdk)
        except ValueError:
            problems.append(f'link leaves the SDK: {path.relative_to(sdk)} -> {target}')
            continue
        if not os.path.lexists(resolved):
            problems.append(f'broken link {path.relative_to(sdk)} -> {target}')
    return count, problems


def main():
    parser = argparse.ArgumentParser(description='Pack an OpenWave SDK directory into a tar.gz that works from any directory.')
    parser.add_argument('--sdk', type=Path, default=ROOT / 'out/os-sdk')
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    name = f'openwave-sdk-{args.target}'
    output = (args.output or ROOT / 'out' / f'{name}.tar.gz').resolve()
    if not (sdk / 'sysroot' / args.target / 'usr/include').is_dir():
        sys.exit(f'{sdk} has no sysroot for {args.target}')
    count, problems = check_links(sdk)
    if problems:
        sys.exit('refusing to pack:\n  ' + '\n  '.join(problems[:20]))
    prefixes = sorted({str(sdk), str(ROOT / 'toolchain'), str(ROOT / 'out/os-sdk')})
    shutil.copy2(ROOT / 'scripts/sdk-env.sh', sdk / 'env.sh')
    rewritten, embedded, rewrites = 0, 0, {}
    for path in sorted(sdk.rglob('*')):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 16 << 20:
            continue
        relative = path.relative_to(sdk).as_posix()
        if relative in EXCLUDED:
            continue
        data = path.read_bytes()
        if not any(prefix.encode() in data for prefix in prefixes):
            continue
        if b'\0' in data[:4096]:
            embedded += 1
            continue
        try:
            rewrites[relative] = sdk_relocatable.rewrite(relative, data, prefixes)
        except ValueError as error:
            sys.exit(str(error))
        rewritten += 1
    print(f'{count} symbolic links, all relative and inside the SDK; {rewritten} files made independent of '
          f'their location; {embedded} binary files carry the build path only as an embedded string')
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_name(output.name + '.part')
    compressor = shutil.which('pigz') or 'gzip'
    with partial.open('wb') as stream:
        compress = subprocess.Popen([compressor, '-6'], stdin=subprocess.PIPE, stdout=stream)
        archive = tarfile.open(fileobj=compress.stdin, mode='w|', format=tarfile.GNU_FORMAT)

        def add(path, arcname, data=None):
            info = archive.gettarinfo(str(path), arcname)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            if data is None:
                if info.isreg():
                    with path.open('rb') as handle:
                        archive.addfile(info, handle)
                else:
                    archive.addfile(info)
            else:
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))

        add(sdk, name)
        for path in sorted(sdk.rglob('*')):
            relative = path.relative_to(sdk).as_posix()
            if relative in EXCLUDED:
                continue
            if relative in rewrites:
                add(path, f'{name}/{relative}', rewrites[relative])
            else:
                add(path, f'{name}/{relative}')
        archive.close()
        compress.stdin.close()
        if compress.wait():
            partial.unlink(missing_ok=True)
            sys.exit('packing failed')
    partial.rename(output)
    print(f'{output} ({output.stat().st_size / 1e6:.0f} MB)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
