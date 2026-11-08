#!/usr/bin/env python3
"""Install architecture-independent X11 Compose data into the target SDK.

Requires libx11-data on the build host, or explicit source/license paths. No
host libraries or executables are copied. Qt's XKB compose input context needs
these tables in addition to the keyboard layouts from xkeyboard-config.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=Path('/usr/share/X11/locale'))
    parser.add_argument('--license', type=Path, default=Path('/usr/share/doc/libx11-data/copyright'))
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    sysroot = args.sysroot.resolve(strict=True)
    if source == sysroot or sysroot in source.parents or source in sysroot.parents:
        parser.error('Compose source and target sysroot must be separate trees')
    files = [source / 'compose.dir', source / 'locale.alias', *sorted(source.rglob('Compose'))]
    if not (source / 'en_US.UTF-8/Compose').is_file() or not args.license.is_file():
        parser.error('Install libx11-data or supply its Compose source and license paths')
    # Validate every input before installing anything.
    for path in [*files, args.license]:
        if not path.is_file() or (path != args.license and not path.resolve().is_relative_to(source)):
            parser.error(f'Invalid Compose input: {path}')
        if path.read_bytes().startswith(b'\x7fELF'):
            parser.error(f'Compose data must not contain ELF objects: {path}')
    records = {}
    for path in files:
        relative = Path('usr/share/X11/locale') / path.relative_to(source)
        destination = sysroot / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        destination.chmod(0o644)
        records[str(relative)] = hashlib.sha256(destination.read_bytes()).hexdigest()
    license_path = sysroot / 'usr/share/licenses/x11-compose/copyright'
    license_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.license, license_path)
    license_path.chmod(0o644)
    records[str(license_path.relative_to(sysroot))] = hashlib.sha256(license_path.read_bytes()).hexdigest()
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({'source': str(source), 'files': records}, indent=2) + '\n')
    print(f'Installed {len(files)} Compose data files and their license into target SDK')


if __name__ == '__main__':
    main()
