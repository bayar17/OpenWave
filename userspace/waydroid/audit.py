#!/usr/bin/env python3
"""Inventory the current checkout and staged runtime; never certify guest boot."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def audit(rootfs, kernel):
    def files(relative):
        return sorted(str(p.relative_to(ROOT)) for p in (ROOT / relative).glob('*') if p.is_file())
    source = ROOT / 'third_party/waydroid'
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']))
    config = set(kernel.read_text().splitlines())
    fragment = (ROOT / 'userspace/waydroid/kernel.config').read_text().splitlines()
    missing = [line for line in fragment if line.startswith('CONFIG_') and line not in config]
    registry = (ROOT / 'userspace/appd/registry.c').read_text()
    process = (ROOT / 'userspace/appd/process.c').read_text()
    importer = any('waydroid' in text.lower() for text in (registry, process))
    launcher = rootfs / 'usr/bin/python3'
    report = {
        'scope': 'STATIC CHECKS ONLY; use guest waydroid.json for runtime evidence',
        'rootfs': str(rootfs), 'kernel_configuration': str(kernel),
        'upstream': {'commit': commit, 'dirty': dirty},
        'kernel_missing_requirements': missing,
        'openrc_sources': files('userspace/openrc/etc/init.d'),
        'waydroid_enabled_by_default': (rootfs / 'etc/runlevels/default/waydroid').is_symlink(),
        'manager_source_present': (ROOT / 'userspace/waydroid/waydroidd.c').is_file(),
        'appd_android_importer_present': importer,
        'python_launcher': str(launcher.readlink()) if launcher.is_symlink() else None,
        'legacy_dbus_activation_present': (rootfs / 'usr/share/dbus-1/system-services/id.waydro.Container.service').exists(),
        'android_images': {},
        'remaining_requirements': [
            'A validated enforcing manager/container policy',
            'Android init/service labels compatible with the host SELinux policy',
            'Real sys.boot_completed, Binder, zygote and PackageManager evidence',
            'Authenticated Android-to-Ocean surface identity and lifecycle mapping',
            'appd discovery, synchronization, icons and asynchronous launch backend',
            'Back-only authenticated Android navigation bridge and runtime tests',
        ],
    }
    record = ROOT / 'out/boot-recovery/waydroid-android17-images.json'
    if record.is_file():
        report['android_images'] = json.loads(record.read_text())
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs', type=Path, default=ROOT / 'rootfs')
    parser.add_argument('--kernel-config', type=Path, default=ROOT / 'out/linux/.config')
    args = parser.parse_args()
    print(json.dumps(audit(args.rootfs.resolve(), args.kernel_config.resolve()), indent=2))
