#!/usr/bin/env python3
"""Verify pinned upstream implementation and the two documented LXC edits."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from prepare_apparmor_free import APPARMOR

COMMIT = '5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991'


def verify(source, package, no_dbus_activation=False):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args])
    if git('rev-parse', 'HEAD').decode().strip() != COMMIT or git('status', '--porcelain').strip():
        raise RuntimeError(f'Waydroid source must be clean at {COMMIT}')
    records = {}
    paths = git('ls-tree', '-r', '--name-only', COMMIT).decode().splitlines()
    # Verify every installed implementation/config/resource, including moved
    # desktop metadata and networking scripts, not just selected Python files.
    for rel in paths:
        if rel == 'waydroid.py' or rel.startswith(('tools/', 'data/')):
            destination = 'usr/lib/waydroid/' + rel
            name = Path(rel).name
            if rel == 'data/AppIcon.png':
                destination = 'usr/share/icons/hicolor/512x512/apps/waydroid.png'
            elif rel.startswith('data/') and len(Path(rel).parts) == 2:
                for suffix, directory in (
                        ('.desktop', 'usr/share/applications'),
                        ('.directory', 'usr/share/desktop-directories'),
                        ('.menu', 'etc/xdg/menus/applications-merged'),
                        ('.metainfo.xml', 'usr/share/metainfo')):
                    if name.endswith(suffix):
                        destination = directory + '/' + name
                        break
            expected = git('show', COMMIT + ':' + rel)
            if rel in ('data/configs/config_1', 'data/configs/config_3'):
                expected = APPARMOR.sub(b'', expected)
            actual = package / destination
            if actual.is_symlink() or not actual.is_file() or actual.read_bytes() != expected:
                raise RuntimeError(f'Upstream package mismatch: {destination}')
            records[destination] = hashlib.sha256(expected).hexdigest()
    for path in (package / 'usr/lib/waydroid').rglob('*'):
        relative = path.relative_to(package).as_posix()
        if path.is_symlink() or (path.is_file() and path.suffix in ('.py', '.so')
                                 and relative not in records):
            raise RuntimeError(f'Unexpected upstream implementation: {relative}')
    link = package / 'usr/bin/waydroid'
    if not link.is_symlink() or link.readlink() != Path('../lib/waydroid/waydroid.py'):
        raise RuntimeError('Unexpected Waydroid CLI symlink')
    for name in ('id.waydro.Container.conf', 'id.waydro.Container.policy'):
        directory = 'usr/share/dbus-1/system.d' if name.endswith('.conf') else 'usr/share/polkit-1/actions'
        if (package / directory / name).read_bytes() != git('show', COMMIT + ':dbus/' + name):
            raise RuntimeError(f'Upstream D-Bus metadata mismatch: {name}')
    activation = package / 'usr/share/dbus-1/system-services/id.waydro.Container.service'
    if no_dbus_activation and (activation.exists() or activation.is_symlink()):
        raise RuntimeError('D-Bus activation conflicts with OpenRC manager ownership')
    return {'upstream': 'Waydroid', 'commit': COMMIT,
            'verified_files': len(records), 'files': records,
            'adjustments': ['config_1/config_3: remove AppArmor assignment only']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--no-dbus-activation', action='store_true')
    args = parser.parse_args()
    print(json.dumps(verify(args.source, args.package, args.no_dbus_activation), indent=2))
