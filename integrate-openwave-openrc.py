#!/usr/bin/env python3
"""Validate and (on request) install OpenWave OpenRC runlevels in rootfs.

Does not build, download, remove userspace, or generate images. Source of truth:
userspace/openrc/{boot,default,disabled}-services.txt and etc/init.d/.
"""
import argparse
import os
import re
import shutil
import stat
import tarfile
from datetime import datetime, timezone
from pathlib import Path

SERVICE_NAME = re.compile(r"^[A-Za-z0-9_-]+$")
COMMAND = re.compile(r'^\s*command="(/[^"\n]+)"\s*$', re.MULTILINE)


def manifest(path):
    result = []
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if not SERVICE_NAME.fullmatch(line):
            raise ValueError(f"Invalid service name at {path}:{lineno}: {line!r}")
        if line in result:
            raise ValueError(f"Duplicate service {line} in {path}")
        result.append(line)
    return result


def disabled(path):
    result = []
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 2 or fields[0] not in ('boot', 'default', 'sysinit') or not SERVICE_NAME.fullmatch(fields[1]):
            raise ValueError(f"Invalid disabled entry at {path}:{lineno}: {line!r}")
        result.append(tuple(fields))
    return result


def is_executable(path):
    return path.is_file() and os.access(path, os.X_OK)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo', type=Path, default=Path.cwd(), help='OpenWave repository directory')
    ap.add_argument('--apply', action='store_true', help='Back up and modify rootfs')
    args = ap.parse_args()
    repo = args.repo.resolve(strict=True)
    rootfs = repo / 'rootfs'
    source = repo / 'userspace/openrc'
    if not rootfs.is_dir() or rootfs.is_symlink():
        ap.error('rootfs must be an existing non-symlink directory')
    if not source.is_dir() or source.is_symlink():
        ap.error('userspace/openrc missing; restore it from source recovery first')

    for part in ('etc', 'etc/init.d', 'etc/runlevels', 'etc/runlevels/boot', 'etc/runlevels/default'):
        p = rootfs / part
        if p.is_symlink():
            ap.error(f'refusing symlinked staging directory {p}')
    for p in (source / 'boot-services.txt', source / 'default-services.txt', source / 'disabled-services.txt'):
        if not p.is_file() or p.is_symlink():
            ap.error(f'missing or symlinked source manifest: {p}')

    enabled = {level: manifest(source / f'{level}-services.txt') for level in ('boot', 'default')}
    excluded = disabled(source / 'disabled-services.txt')
    for level, name in excluded:
        if name in enabled.get(level, []):
            ap.error(f'{name} is simultaneously enabled and disabled in {level}')

    errors = []
    prereqs = ['bin/busybox', 'bin/sh', 'usr/bin/rc-status',
               'sbin/openrc-init', 'sbin/openrc-run', 'sbin/rc-service',
               'lib/rc/bin/checkpath', 'usr/sbin/acpid',
               'etc/acpi/events/power',
               'System/Services/openwave-runtime-check',
               'System/Services/openwave-register-service-identity',
               'System/Services/openwave-wait-dbus']
    for rel in prereqs:
        p = rootfs / rel
        if rel.startswith('etc/'):
            ok = p.is_file()
        else:
            ok = is_executable(p)
        if not ok:
            errors.append(f'MISSING PREREQUISITE: {rel}')

    service_dir = source / 'etc/init.d'
    if not service_dir.is_dir() or service_dir.is_symlink():
        ap.error(f'missing OpenWave init script directory: {service_dir}')
    sources = sorted(p for p in service_dir.iterdir() if p.is_file() and not p.is_symlink())
    source_names = {p.name for p in sources}
    for level, names in enabled.items():
        for name in names:
            if name not in source_names:
                errors.append(f'MISSING INIT SCRIPT: userspace/openrc/etc/init.d/{name}')
                continue
            content = (service_dir / name).read_text()
            if not content.startswith('#!/sbin/openrc-run\n'):
                errors.append(f'INVALID SHEBANG: {name}')
            command = COMMAND.search(content)
            if command and not is_executable(rootfs / command.group(1).lstrip('/')):
                errors.append(f'MISSING SERVICE EXECUTABLE: {name} -> {command.group(1)}')
            link = rootfs / 'etc/runlevels' / level / name
            if link.exists() and not link.is_symlink():
                errors.append(f'REFUSING NON-SYMLINK RUNLEVEL ENTRY: {link}')
    for p in sources:
        destination = rootfs / 'etc/init.d' / p.name
        if destination.is_symlink():
            errors.append(f'REFUSING SYMLINKED INIT SCRIPT DESTINATION: {destination}')

    print('OpenRC expected enabled services:')
    for level, names in enabled.items():
        print(f'  {level}: {len(names)} — {", ".join(names)}')
    print(f'  disabled runlevel entries: {len(excluded)}')
    if errors:
        print('\nBLOCKERS (no changes made):')
        for error in errors:
            print('  ' + error)
        raise SystemExit(1)

    if not args.apply:
        print('\nPreflight passed. NO CHANGES MADE. Run with --apply to install.')
        return

    backup_dir = repo / 'out/backups'
    if backup_dir.is_symlink():
        ap.error(f'refusing symlinked backup directory: {backup_dir}')
    backup_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    archive = backup_dir / f'openrc-rootfs-before-{timestamp}.tar.gz'
    with tarfile.open(archive, 'x:gz') as tar:
        for rel in ('etc/init.d', 'etc/runlevels'):
            path = rootfs / rel
            if path.exists() and not path.is_symlink():
                tar.add(path, arcname=rel, recursive=True)
    print('Backup:', archive)

    (rootfs / 'etc/init.d').mkdir(parents=True, exist_ok=True)
    for p in sources:
        target = rootfs / 'etc/init.d' / p.name
        shutil.copyfile(p, target)
        target.chmod(0o755)
    for level, names in enabled.items():
        directory = rootfs / 'etc/runlevels' / level
        directory.mkdir(parents=True, exist_ok=True)
        for name in names:
            link = directory / name
            if link.is_symlink():
                link.unlink()
            link.symlink_to('/etc/init.d/' + name)
    for level, name in excluded:
        link = rootfs / 'etc/runlevels' / level / name
        if link.is_symlink() and os.readlink(link) == '/etc/init.d/' + name:
            link.unlink()
    print('Installed OpenWave OpenRC init scripts and reviewed runlevel links.')
    print('NOT performed: image generation, SELinux labeling, boot validation.')


if __name__ == '__main__':
    main()
