#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = (
    ('etc/appdeploy', 0o755), ('etc/appdeploy/keys', 0o755), ('etc/appdeploy/keys/application', 0o755),
    ('etc/appdeploy/keys/system', 0o755), ('etc/rpm', 0o755),
    ('var/lib/appdeploy', 0o700), ('var/lib/appdeploy/rpmdb', 0o700), ('var/lib/appdeploy/state', 0o700),
    ('var/lib/appdeploy/staging', 0o700), ('var/lib/appdeploy/log', 0o700), ('var/lib/appdeploy/persist', 0o700),
    ('var/lib/appdeploy/inbox', 0o755), ('var/cache/appdeploy', 0o700), ('opt', 0o755), ('usr/local', 0o755),
)
RPM_DATA = ('macros', 'rpmrc', 'platform', 'lua', 'macros.d')


def needed(readelf, path):
    result = subprocess.run([str(readelf), '-d', str(path)], capture_output=True, text=True, check=False)
    names = []
    for line in result.stdout.splitlines():
        if '(NEEDED)' in line and '[' in line:
            names.append(line.split('[', 1)[1].split(']', 1)[0])
    return names


def find_library(name, sysroot):
    for directory in ('usr/lib64', 'usr/lib', 'lib64', 'lib'):
        candidate = sysroot / directory / name
        if candidate.exists() or candidate.is_symlink():
            return candidate, directory
    return None, None


def copy_library(source, directory, root, sysroot):
    destination_dir = root / directory
    destination_dir.mkdir(parents=True, exist_ok=True)
    current = source
    while True:
        destination = destination_dir / current.name
        if current.is_symlink():
            target = os.readlink(current)
            if destination.is_symlink() or destination.exists():
                destination.unlink()
            destination.symlink_to(target)
            current = current.parent / target
            continue
        if not destination.exists() or destination.read_bytes() != current.read_bytes():
            temporary = destination.with_name(destination.name + '.new')
            shutil.copy2(current, temporary)
            temporary.chmod(0o755)
            temporary.replace(destination)
        return current


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('OPENWAVE_SDK') or ROOT / 'toolchain'))
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    parser.add_argument('--build', type=Path, default=ROOT / 'out/os-build/appdeploy')
    args = parser.parse_args()
    root = args.rootfs.resolve()
    sysroot = args.sdk / 'sysroot' / args.target
    readelf = args.sdk / 'bin/llvm-readelf'
    if not (args.build / 'libappdeploy.so').exists():
        print('appdeploy is not built (./build-openwave packaging; cmake --build out/appdeploy-build): skipped')
        return 0
    stage = args.build / 'stage'
    shutil.rmtree(stage, ignore_errors=True)
    subprocess.run(['cmake', '--install', str(args.build), '--prefix', '/usr'], check=True,
                   env=dict(os.environ, DESTDIR=str(stage)), stdout=subprocess.DEVNULL)
    for path in sorted(stage.rglob('*')):
        relative = path.relative_to(stage)
        if path.is_dir() or relative.parts[:2] == ('usr', 'include'):
            continue
        destination = root / relative
        if destination.is_symlink() and not path.is_symlink():
            raise RuntimeError(f'Refusing a symlinked destination: {destination}')
        destination.parent.mkdir(parents=True, exist_ok=True)
        if path.is_symlink():
            if destination.is_symlink() or destination.exists():
                destination.unlink()
            destination.symlink_to(os.readlink(path))
            continue
        temporary = destination.with_name(destination.name + '.new')
        shutil.copy2(path, temporary)
        temporary.chmod(0o755 if relative.parts[:2] in {('usr', 'bin'), ('usr', 'libexec')}
                        or '.so' in destination.name else 0o644)
        temporary.replace(destination)
    queue = [root / 'usr/bin/appdeploy', root / 'usr/libexec/openwave-appdeploy/appdeploy-helper',
             root / 'usr/lib64/libappdeploy.so.0']
    seen, copied = set(), []
    while queue:
        binary = queue.pop()
        if not binary.exists() or binary in seen:
            continue
        seen.add(binary)
        for name in needed(readelf, binary):
            if name.startswith(('libc.so', 'libm.so', 'ld-linux', 'libdl', 'libpthread', 'librt', 'libgcc_s', 'libresolv',
                                'libutil', 'libanl', 'libnss')):
                continue
            if (root / 'usr/lib64' / name).exists() and name.startswith('libappdeploy'):
                queue.append(root / 'usr/lib64' / name)
                continue
            found, directory = find_library(name, sysroot)
            if not found:
                raise RuntimeError(f'{binary.name} needs {name}, which is not in the sysroot')
            copy_library(found, directory, root, sysroot)
            copied.append(name)
            queue.append(root / directory / name)
    rpm_source = sysroot / 'usr/lib/rpm'
    for item in RPM_DATA:
        source = rpm_source / item
        if not source.exists():
            continue
        destination = root / 'usr/lib/rpm' / item
        if source.is_dir():
            shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=False)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
    for path in (root / 'usr/lib/rpm').rglob('*'):
        path.chmod(0o755 if path.is_dir() else 0o644)
    (root / 'usr/lib/rpm').chmod(0o755)
    for relative, mode in DIRECTORIES:
        directory = root / relative
        if directory.is_symlink():
            raise RuntimeError(f'Refusing a symlinked directory: {directory}')
        directory.mkdir(parents=True, exist_ok=True)
        directory.chmod(mode)
    print(f'appdeploy installed: {len(set(copied))} libraries from the sysroot')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
