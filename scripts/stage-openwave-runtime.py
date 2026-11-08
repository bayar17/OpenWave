#!/usr/bin/env python3
"""Stage TARGET Qt/OpenWave runtime assets from the SDK sysroot into an assembled rootfs.

Does not compile software or create a disk image. No host SDK binaries, headers,
CMake files or pkg-config metadata are packaged. Run after rootfs assembly.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from collections import deque
from pathlib import Path

LIB_DIRS = ('usr/lib64', 'usr/lib', 'lib64', 'lib')
QT_PLUGIN_DIRS = ('usr/plugins', 'usr/lib64/qt6/plugins', 'usr/lib/qt6/plugins',
                  'usr/lib64/qt/plugins', 'usr/lib/qt/plugins')
QML_DIRS = ('usr/qml', 'usr/lib64/qt6/qml', 'usr/lib/qt6/qml',
            'usr/lib64/qml', 'usr/lib/qml')
GRAPHICS_DIRS = ('usr/lib64/dri', 'usr/lib/dri', 'usr/lib64/gbm', 'usr/lib/gbm')
FONT_DIRS = ('usr/share/fonts/openwave',)
COMPOSE_DIRS = ('usr/share/X11/locale', 'usr/share/licenses/x11-compose')
NEEDED = re.compile(r'\(NEEDED\).*?\[([^]]+)\]')
RUNPATH = re.compile(r'\((?:RPATH|RUNPATH)\).*?\[([^]]+)\]')
DYNAMIC_NAME = re.compile(r'\.so(?:\.|$)')


def in_tree(path: Path, tree: Path) -> bool:
    return path == tree or tree in path.parents


def resolve_links(path: Path, tree: Path) -> Path:
    """Resolve sysroot symlinks, interpreting absolute targets inside sysroot."""
    current = path
    seen = set()
    for _ in range(40):
        if not in_tree(current, tree) or current in seen:
            raise RuntimeError(f'Symlink loop or escape: {path}')
        seen.add(current)
        if not current.is_symlink():
            if not current.exists():
                raise RuntimeError(f'Broken source link: {path} -> {current}')
            return current
        value = os.readlink(current)
        current = (tree / value.lstrip('/')) if value.startswith('/') else \
            Path(os.path.normpath(current.parent / value))
    raise RuntimeError(f'Symlink depth exceeded for {path}')


def elf_machine(path: Path) -> int | None:
    try:
        with path.open('rb') as f:
            hdr = f.read(20)
        if len(hdr) < 20 or hdr[:4] != b'\x7fELF':
            return None
        endian = '<' if hdr[5] == 1 else '>'
        return struct.unpack(endian + 'H', hdr[18:20])[0]
    except OSError:
        return None


def parse_dynamic(path: Path, readelf: str) -> tuple[list[str], list[str]]:
    cp = subprocess.run([readelf, '-d', str(path)], text=True, capture_output=True)
    if cp.returncode:
        # Static ELF without dynamic section is fine; other readelf errors aren't.
        if 'There is no dynamic section' in cp.stdout + cp.stderr:
            return [], []
        raise RuntimeError(f'ELF analysis failed: {path}: {cp.stderr.strip()}')
    return NEEDED.findall(cp.stdout), RUNPATH.findall(cp.stdout)


class Stager:
    def __init__(self, args):
        self.args = args
        self.repo = args.repo.absolute()
        self.sdk = (args.sdk or self.repo / 'toolchain').absolute()
        self.sysroot = (args.sysroot or self.sdk / 'sysroot' / args.target).absolute()
        self.rootfs = (args.rootfs or self.repo / 'rootfs').absolute()
        if not self.sysroot.is_dir() or not self.rootfs.is_dir():
            raise RuntimeError('Target sysroot and existing rootfs directory are both required')
        if self.rootfs == self.sysroot or in_tree(self.rootfs, self.sdk) or in_tree(self.sysroot, self.rootfs):
            raise RuntimeError('Refusing overlapping rootfs/sysroot locations')
        if self.rootfs.is_symlink() or self.sysroot.is_symlink():
            raise RuntimeError('Rootfs and sysroot must not be symlinks')
        self.machine = {'x86_64': 62, 'aarch64': 183}.get(args.target.split('-')[0])
        if self.machine is None:
            raise RuntimeError(f'Unsupported target architecture for verification: {args.target}')
        self.readelf = str(self.sdk / 'bin/llvm-readelf')
        if not Path(self.readelf).is_file():
            self.readelf = shutil.which('readelf') or ''
        if not self.readelf:
            raise RuntimeError('Target llvm-readelf or host readelf is required')
        self.library_index = {}
        self.queue = deque()
        self.seen = set()
        self.missing = set()
        self.rpaths = set()
        self.added = set()
        self.seeds = {'qt': 0, 'openwave': 0, 'qt_plugins': 0,
                      'qml_modules': 0, 'graphics': 0, 'fonts': 0}
        for rel in LIB_DIRS:
            directory = self.sysroot / rel
            if directory.is_dir():
                for entry in sorted(directory.iterdir()):
                    if DYNAMIC_NAME.search(entry.name):
                        self.library_index.setdefault(entry.name, entry)

    def safe_destination(self, rel: Path) -> Path:
        if rel.is_absolute() or '..' in rel.parts or not rel.parts:
            raise RuntimeError(f'Invalid destination relative path: {rel}')
        candidate = self.rootfs / rel
        # The assembled runtime uses directory aliases such as
        # /usr/share/fonts -> ../lib/fonts. Resolve them as guest paths;
        # an absolute link must never redirect a write into the host root.
        for _ in range(40):
            if not in_tree(candidate, self.rootfs):
                raise RuntimeError(f'Destination link escapes rootfs: {rel}')
            parts = candidate.relative_to(self.rootfs).parts
            for depth in range(1, len(parts)):
                parent = self.rootfs.joinpath(*parts[:depth])
                if parent.is_symlink():
                    value = os.readlink(parent)
                    target = self.rootfs / value.lstrip('/') if value.startswith('/') else parent.parent / value
                    candidate = Path(os.path.normpath(target.joinpath(*parts[depth:])))
                    break
            else:
                return candidate
        raise RuntimeError(f'Destination symlink loop: {rel}')

    def record_file(self, source: Path, rel: Path):
        dest = self.safe_destination(rel)
        if source.is_symlink():
            real = resolve_links(source, self.sysroot)
            self.record_file(real, real.relative_to(self.sysroot))
            link = os.readlink(source)
            if link.startswith('/'):
                # Rewrite build-host absolute source symlinks to target-root paths.
                link = '/' + str(real.relative_to(self.sysroot))
            elif not in_tree(Path(os.path.normpath(source.parent / link)), self.sysroot):
                raise RuntimeError(f'Symlink points outside target sysroot: {source}')
            if self.args.dry_run:
                self.added.add(rel)
                return
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.is_symlink() and os.readlink(dest) == link:
                return
            if dest.exists() or dest.is_symlink():
                if dest.is_dir() and not dest.is_symlink():
                    raise RuntimeError(f'Cannot replace directory with symlink: {dest}')
                dest.unlink()
            dest.symlink_to(link)
            self.added.add(rel)
            return
        if not source.is_file():
            return
        machine = elf_machine(source)
        if machine is not None and machine != self.machine:
            raise RuntimeError(f'Wrong target architecture for {source}: e_machine={machine}')
        if self.args.dry_run:
            self.added.add(rel)
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.is_symlink():
                dest.unlink()
            shutil.copy2(source, dest)
            self.added.add(rel)
        if machine is not None:
            self.queue.append(source)

    def copy_tree(self, relative: str) -> int:
        source_dir = self.sysroot / relative
        if not source_dir.is_dir():
            return 0
        count = 0
        for root, dirs, files in os.walk(source_dir, followlinks=False):
            current = Path(root)
            dirs[:] = [d for d in dirs if not (current / d).is_symlink()]
            for name in files:
                source = current / name
                # Runtime QML does not need IDE metadata or generated development files.
                if source.suffix in ('.qmltypes', '.h', '.hpp', '.a', '.la', '.pc', '.cmake'):
                    continue
                self.record_file(source, source.relative_to(self.sysroot))
                count += 1
        return count

    def find_existing(self, soname: str, source: Path, runpaths: list[str]) -> bool:
        # Modules such as glibc gconv and PipeWire load siblings using RUNPATH.
        # A matching filename elsewhere in the image does not satisfy that link.
        tree = self.rootfs if in_tree(source, self.rootfs) else self.sysroot
        origin = '/' + source.relative_to(tree).parent.as_posix()
        directories = []
        for entries in runpaths:
            for entry in entries.split(':'):
                entry = entry.replace('${ORIGIN}', origin).replace('$ORIGIN', origin)
                if entry.startswith('/') and '$' not in entry:
                    directories.append(Path(os.path.normpath(entry).lstrip('/')))
        directories.extend(Path(directory) for directory in LIB_DIRS)
        for directory in directories:
            relative = directory / soname
            candidate = self.safe_destination(relative)
            if relative in self.added:
                return True
            try:
                if resolve_links(candidate, self.rootfs).is_file():
                    return True
            except RuntimeError:
                pass
        return False

    def add_lib(self, name: str) -> bool:
        source = self.library_index.get(name)
        if source is None:
            return False
        self.record_file(source, source.relative_to(self.sysroot))
        return True

    def add_elf_interpreter(self, source: Path):
        cp = subprocess.run([self.readelf, '-l', str(source)], text=True, capture_output=True)
        if cp.returncode:
            raise RuntimeError(f'Unable to inspect interpreter: {source}')
        match = re.search(r'Requesting program interpreter: ([^\]]+)\]', cp.stdout)
        if not match:
            return
        interp = match.group(1)
        if not interp.startswith('/') or '..' in Path(interp).parts:
            raise RuntimeError(f'Unexpected ELF interpreter: {interp}')
        target = self.rootfs / interp.lstrip('/')
        if target.exists():
            return
        candidate = self.sysroot / interp.lstrip('/')
        if candidate.exists() or candidate.is_symlink():
            self.record_file(candidate, candidate.relative_to(self.sysroot))
        else:
            self.missing.add((str(source), f'ELF interpreter {interp}'))

    def run(self):
        # Stage all target Qt shared libraries, plus all OpenWave shared APIs.
        for name, source in sorted(self.library_index.items()):
            if name.startswith('libQt6') and DYNAMIC_NAME.search(name):
                self.record_file(source, source.relative_to(self.sysroot))
                self.seeds['qt'] += 1
            elif name.startswith(('libopenwave-', 'libOpenWave', 'libWaterRouge')) and DYNAMIC_NAME.search(name):
                self.record_file(source, source.relative_to(self.sysroot))
                self.seeds['openwave'] += 1
        if not self.seeds['qt']:
            raise RuntimeError('No target Qt6 libraries installed in sysroot; build target Qt first')
        if not self.seeds['openwave']:
            raise RuntimeError('No libopenwave-*.so libraries installed in target sysroot; run ./build-openwave sdk first')
        for folder in QT_PLUGIN_DIRS:
            self.seeds['qt_plugins'] += self.copy_tree(folder)
        for folder in QML_DIRS:
            self.seeds['qml_modules'] += self.copy_tree(folder)
        for folder in GRAPHICS_DIRS:
            self.seeds['graphics'] += self.copy_tree(folder)
        for folder in FONT_DIRS:
            self.seeds['fonts'] += self.copy_tree(folder)
        for folder in COMPOSE_DIRS:
            self.copy_tree(folder)
        if not self.seeds['qt_plugins']:
            raise RuntimeError('No Qt plugin files installed in target sysroot')
        module_root = self.sysroot / 'usr/qml/OpenWave'
        for module in ('Fonts', 'Theme', 'Controls', 'Application'):
            module_path = module_root / module
            if not (module_path / 'qmldir').is_file():
                raise RuntimeError(f'Missing OpenWave.{module} QML module in target SDK: {module_path}')
            if not any(module_path.glob('*.so')):
                raise RuntimeError(f'Missing OpenWave.{module} QML plugin library: {module_path}')
        # Actual Ocean startup explicitly selects EGLFS + eglfs_kms.
        platform_plugins = [self.sysroot / d / 'platforms/libqeglfs.so' for d in QT_PLUGIN_DIRS]
        if not self.args.skip_eglfs_check and not any(p.exists() for p in platform_plugins):
            raise RuntimeError('Ocean requires Qt EGLFS, but libqeglfs.so is missing in target Qt plugins')
        # Check both pre-existing rootfs ELFs and the staged target libraries/plugins.
        for path in self.rootfs.rglob('*'):
            if not path.is_symlink() and path.is_file() and elf_machine(path) is not None:
                self.queue.append(path)
        while self.queue:
            source = self.queue.popleft()
            identity = str(source)
            if identity in self.seen:
                continue
            self.seen.add(identity)
            sonames, paths = parse_dynamic(source, self.readelf)
            for soname in sonames:
                if self.find_existing(soname, source, paths):
                    continue
                if not self.add_lib(soname):
                    self.missing.add((str(source), soname))
            # Don't repeat the interpreter check for ET_DYN libraries.
            if elf_machine(source) is not None:
                self.add_elf_interpreter(source)
            for rpath in paths:
                if '/mnt/' in rpath or '/out/' in rpath or '/toolchain/' in rpath:
                    self.rpaths.add((str(source), rpath))
        for name, count in self.seeds.items():
            print(f'{name}: {count} source entries selected')
        print(f'Files installed/planned: {len(self.added)}; ELF objects analyzed: {len(self.seen)}')
        if self.rpaths:
            print('ERROR: build-host RPATH/RUNPATH discovered in staged target libraries:', file=sys.stderr)
            for filename, rpath in sorted(self.rpaths)[:25]:
                print(f'  {filename}: {rpath}', file=sys.stderr)
        if self.rpaths:
            return 1
        if self.missing:
            print('ERROR: Unresolved target ELF dependencies:', file=sys.stderr)
            for filename, soname in sorted(self.missing)[:100]:
                print(f'  {soname} required by {filename}', file=sys.stderr)
            return 1
        print('DRY RUN: no files written' if self.args.dry_run else
              f'Target Qt/OpenWave runtime staged into {self.rootfs}; no image assembled.')
        return 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, default=Path.cwd())
    p.add_argument('--sdk', type=Path)
    p.add_argument('--sysroot', type=Path)
    p.add_argument('--rootfs', type=Path)
    p.add_argument('--target', default='x86_64-unknown-openwave')
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--skip-eglfs-check', action='store_true',
                   help='For non-Ocean test roots only; Ocean uses EGLFS/KMS')
    args = p.parse_args()
    try:
        return Stager(args).run()
    except (RuntimeError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
