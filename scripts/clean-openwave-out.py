#!/usr/bin/env python3
"""Remove known obsolete outputs; preserve installed SDKs and live VM files.

Dry-run by default. Build configurations and loose QA evidence are archived
before deletion. Current SDK/app caches, kernel/compiler builds, final images,
host utilities and the original mixed-installation backup are retained.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out'
OBSOLETE = {
    'qt-host', 'qt-openwave', 'wpe-webkit-openwave-release',
    'qtwayland-openwave', 'qtwayland-openwave-final', 'qtwayland-openwave-target',
    'sdk-relocation', 'sdk-consumer-build', 'sdk-qmake-test', 'sdk-runtime-probe',
    'browser-engine-qa-root', 'browser-deps', 'browser-engine-fixture',
    'browser-native-tests', 'browser-data-tests', 'browser-tabs-pages',
    'browser-settings-tests', 'connectivity-test-rootfs', 'repack-root',
    'rootfs-previous', 'openwave-userspace', 'wayland-runtime-overlay',
    'before-settings', 'before-standalone-settings', 'ocean-settings-standalone-moved',
    'ocean-settings-backend-tests-moved', 'ocean-settings-backend-tests-pre-move',
    'ocean-settings-qtcreator-pre-move', 'ocean-files-host',
    'settings-preview', 'files-menu-preview', 'app-loading-preview',
    'airplane-icon-preview', 'chevron-preview', 'status-chevron-preview',
    'quality-diagnostics', 'ext4-mode-test-root', 'repack-owner-probe', '__pycache__',
}
CONFIGS = {'CMakeCache.txt', 'config.summary', 'config.opt', 'config.log',
           'meson-log.txt', 'intro-buildoptions.json', '.config'}
LOOSE_EVIDENCE = {'.log', '.txt', '.md', '.json', '.png', '.jpg', '.jpeg',
                  '.svg', '.qml', '.py', '.c', '.cpp', '.h', '.sh', '.s', '.o'}


def live_paths():
    paths = set()
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            handles = [process / 'cwd', *(process / 'fd').iterdir()]
            for handle in handles:
                try:
                    target = Path(os.readlink(handle).removesuffix(' (deleted)'))
                    if target.is_absolute() and target.is_relative_to(OUT):
                        paths.add(target)
                except OSError:
                    pass
        except (PermissionError, FileNotFoundError):
            pass
    return paths


def protected(candidate, live):
    return any(path == candidate or candidate in path.parents for path in live)


def used_bytes():
    return int(subprocess.check_output(['du', '-sx', '-B1', str(OUT)], text=True).split()[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='Archive evidence and remove obsolete outputs')
    args = parser.parse_args()
    live = live_paths()
    candidates, evidence = [], []
    for path in sorted(OUT.iterdir()):
        if path.is_symlink() or protected(path, live):
            continue
        if path.is_dir() and (path.name in OBSOLETE or path.name.endswith('-mount')):
            candidates.append(path)
        elif path.is_file() and path.suffix == '.img' and not path.name.endswith('-final.img'):
            if any(part in path.name.lower() for part in ('before', 'preview', 'test', 'smoke', 'candidate')) or path.name in {'OpenWave-ocean.img', 'OpenWave-mobile-baseline.img', 'OpenWave-sdk-qa.img', 'OpenWave.settings-durability.img'}:
                candidates.append(path)
        elif path.is_file() and path.name != 'rootfs-manifest.json' and path.suffix in LOOSE_EVIDENCE:
            evidence.append(path)

    # Never descend into a mounted filesystem, or remove targets of SDK links.
    mounts = subprocess.check_output(['findmnt', '-rn', '-o', 'TARGET'], text=True).splitlines()
    for candidate in candidates:
        if any(Path(mount) == candidate or candidate in Path(mount).parents for mount in mounts):
            raise SystemExit(f'Refusing mounted output: {candidate}')
    for base in (ROOT / 'toolchain', ROOT / 'qt-host'):
        for directory, dirs, files in os.walk(base, followlinks=False):
            for name in dirs + files:
                link = Path(directory) / name
                if link.is_symlink():
                    target = link.resolve()
                    if any(p == target or p in target.parents for p in candidates):
                        raise SystemExit(f'Installed SDK references obsolete output: {link}')

    before = used_bytes()
    print(f'out/: {before / 1024**3:.2f} GiB; {len(candidates)} obsolete outputs; {len(evidence)} loose evidence files')
    if not args.apply:
        for path in candidates:
            print(f'  remove {path.relative_to(ROOT)}')
        print('Use --apply to archive configurations/evidence and perform cleanup.')
        return

    archive_dir = OUT / 'cleanup'
    archive_dir.mkdir(exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    archive = archive_dir / f'legacy-evidence-{stamp}.tar.gz'
    # Archive logs/configuration first. A failed archive never starts deletion.
    with archive.open('xb') as stream:
        archive.chmod(0o600)
        with tarfile.open(fileobj=stream, mode='w:gz') as tar:
            for path in evidence:
                tar.add(path, arcname=str(path.relative_to(OUT)), recursive=False)
            for candidate in candidates:
                if candidate.is_dir():
                    for directory, _, files in os.walk(candidate, followlinks=False):
                        for name in files:
                            if name in CONFIGS:
                                path = Path(directory) / name
                                tar.add(path, arcname=str(path.relative_to(OUT)), recursive=False)
    removed = []
    for index, path in enumerate(candidates + evidence):
        if index % 25 == 0:
            live = live_paths()
        if protected(path, live):
            raise SystemExit(f'Output became active; leaving it intact: {path}')
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
        removed.append(str(path.relative_to(ROOT)))
    after = used_bytes()
    report = {'before_bytes': before, 'after_bytes': after, 'freed_bytes': before - after,
              'archive': str(archive.relative_to(ROOT)), 'removed': removed}
    (archive_dir / f'report-{stamp}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Freed {(before - after) / 1024**3:.2f} GiB; out/ now {after / 1024**3:.2f} GiB')
    print(f'Evidence archive: {archive.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
