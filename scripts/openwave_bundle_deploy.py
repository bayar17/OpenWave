#!/usr/bin/env python3

import glob
import os
from pathlib import Path
import shutil
import subprocess

WEB_ENGINE_PAYLOAD = [
    ('usr/lib64/libWPEWebKit-2.0.so.1', 'lib64/libWPEWebKit-2.0.so.1'),
    ('usr/libexec/wpe-webkit-2.0/WPEWebProcess', 'sbin/WPEWebProcess'),
    ('usr/libexec/wpe-webkit-2.0/WPENetworkProcess', 'sbin/WPENetworkProcess'),
    ('usr/libexec/wpe-webkit-2.0/WPEGPUProcess', 'sbin/WPEGPUProcess'),
    ('usr/libexec/gstreamer-1.0/gst-plugin-scanner', 'sbin/gst-plugin-scanner'),
    ('usr/lib64/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so',
     'plugins/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so'),
    ('usr/lib64/gstreamer-1.0/*.so', 'plugins/gstreamer-1.0/'),
    ('usr/lib64/gio/modules/*.so', 'plugins/gio/modules/'),
    ('usr/lib64/qt6/qml/org/wpewebkit/qtwpe/*', 'qml/org/wpewebkit/qtwpe/'),
    ('usr/share/icu/*/icudt*.dat', 'share/data/icu/'),
]
OPTIONAL = {'usr/lib64/gio/modules/*.so'}
PAYLOAD_DIRECTORIES = ('lib64', 'sbin', 'plugins', 'qml')


def is_elf(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


def rpath_for(installed_as):
    return f'{installed_as}/lib64:/usr/lib64:/usr/lib'


def set_rpath(patchelf, installed_as, path):
    wanted = rpath_for(installed_as)
    current = subprocess.run([patchelf, '--print-rpath', str(path)], capture_output=True, text=True, check=True).stdout.strip()
    if current != wanted:
        subprocess.run([patchelf, '--set-rpath', wanted, str(path)], check=True)


def materialise(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink() or destination.exists():
        destination.unlink()
    shutil.copy2(source.resolve(), destination)
    destination.chmod(0o755 if destination.parent.name == 'sbin' else 0o644)


def deploy_payload(bundle, installed_as, roots, payload=WEB_ENGINE_PAYLOAD, patchelf='patchelf'):
    copied = []
    for source_pattern, destination in payload:
        into_directory = destination.endswith('/')
        matches = []
        for root in roots:
            matches = sorted(Path(match) for match in glob.glob(str(Path(root) / source_pattern)))
            if matches:
                break
        target_of = (lambda match: bundle / destination / match.name) if into_directory else (lambda match: bundle / destination)
        if not matches:
            already = (any((bundle / destination).glob('*')) if into_directory and (bundle / destination).is_dir()
                       else not into_directory and (bundle / destination).exists())
            if already or source_pattern in OPTIONAL:
                continue
            raise RuntimeError(f'Engine file not found in any runtime tree: {source_pattern}')
        for match in matches:
            target = target_of(match)
            if target.exists():
                continue
            materialise(match, target)
            copied.append(target)
    for directory in PAYLOAD_DIRECTORIES:
        for path in sorted((bundle / directory).rglob('*')):
            if is_elf(path):
                set_rpath(patchelf, installed_as, path)
    return copied


def private_closure(bundle, installed_as, libraries, base_directories, needed, patchelf='patchelf'):
    copied = []
    queue = [path for directory in PAYLOAD_DIRECTORIES for path in sorted((bundle / directory).rglob('*')) if is_elf(path)]
    seen = set()
    private = bundle / 'lib64'
    private.mkdir(exist_ok=True)
    while queue:
        path = queue.pop()
        if path in seen:
            continue
        seen.add(path)
        for name in needed(path):
            if (private / name).is_file() or (bundle / 'lib' / name).is_file():
                continue
            if any((Path(directory) / name).is_file() for directory in base_directories):
                continue
            if name not in libraries:
                raise RuntimeError(f'{path.relative_to(bundle)} needs {name}, which neither /usr nor any runtime tree provides')
            target = private / name
            materialise(libraries[name], target)
            set_rpath(patchelf, installed_as, target)
            copied.append(target)
            queue.append(target)
    return copied


if __name__ == '__main__':
    raise SystemExit('openwave_bundle_deploy is a library used by scripts/assemble-openwave-rootfs.py')
