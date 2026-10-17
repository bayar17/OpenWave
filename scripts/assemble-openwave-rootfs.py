#!/usr/bin/env python3
"""Assemble a runtime-only tree, checking every ELF dependency before publishing.

The SDK remains the source of development libraries. Existing rootfs state is
preserved; --base allows the one-time migration of the legacy installation.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TRIPLE = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
SDK = Path(os.environ.get('OPENWAVE_SDK', ROOT / 'toolchain'))
SYSROOT = SDK / 'sysroot' / TRIPLE
RPATH_FIXES = []
STAGE = ROOT / 'out/staging' / TRIPLE / 'userspace'
RUNTIME = Path(os.environ.get('OPENWAVE_ROOTFS', ROOT / 'rootfs')).resolve()
# Runtime entry points and helper programs, not development utilities.
BIN = { 'dbus-daemon', 'udevadm', 'pipewire', 'wireplumber',
       'wpctl', 'nm-online', 'getent', 'iconv', 'locale', 'gsettings', 'gio', 'gdbus', 'bwrap'}
SBIN = {'NetworkManager', 'udevd', 'udevadm', 'avahi-daemon', 'iconvconfig'}
SHARE = {'X11', 'alsa', 'alsa-card-profile', 'applications', 'avahi', 'dbus-1',
         'drirc.d', 'fontconfig', 'fonts', 'glib-2.0', 'gst-plugins-base',
         'gstreamer-1.0', 'icu', 'libdrm', 'licenses', 'locale', 'ocean-browser',
         'pipewire', 'pixmaps', 'spa-0.2', 'wireplumber', 'wpe-webkit-2.0',
         'xkeyboard-config-2', 'xml', 'zoneinfo', 'xdg-desktop-portal'}
QML = {'OpenWave/Fonts', 'OpenWave/Web', 'QML', 'QtQml', 'QtQml/Models', 'QtQml/WorkerScript', 'QtQuick',
       'QtQuick/Window', 'QtQuick/Controls', 'QtQuick/Controls/impl',
       'QtQuick/Controls/Basic', 'QtQuick/Controls/Basic/impl',
       'QtQuick/Controls/Fusion', 'QtQuick/Controls/Fusion/impl',
       'QtQuick/Templates', 'QtQuick/Effects', 'QtQuick/Layouts',
       'QtQuick/Shapes', 'QtQuick/Dialogs', 'QtQuick/Dialogs/quickimpl',
       'Qt/labs/folderlistmodel', 'QtWayland/Compositor', 'QtWayland/Compositor/XdgShell'}
PLUGINS = {'platforms/libqeglfs.so', 'platforms/libqwayland.so',
           'egldeviceintegrations/libqeglfs-kms-integration.so',
           'wayland-shell-integration/libxdg-shell.so',
           'wayland-graphics-integration-client/libqt-plugin-wayland-egl.so',
           'wayland-graphics-integration-server/libqt-wayland-compositor-wayland-egl.so',
           'wayland-graphics-integration-server/libqt-wayland-compositor-linux-dmabuf-unstable-v1.so',
           'platforminputcontexts/libcomposeplatforminputcontextplugin.so',
           'sqldrivers/libqsqlite.so', 'iconengines/libqsvgicon.so',
           'tls/libqopensslbackend.so'}
DEVEL = {'include', 'cmake', 'pkgconfig', 'objects-Release', 'metatypes',
         'mkspecs', 'modules', 'sbom', 'glib-2.0', 'icu', 'libpng', 'audit'}
DEVEL_EXT = {'.a', '.o', '.la', '.prl', '.pc', '.cmake', '.h', '.hpp', '.qmltypes'}


LEGACY_APPS = {'ocean-launcher', 'ocean-browser', 'ocean-files', 'ocean-settings', 'openwave-shell'}
SERVICES = {'appd', 'permissiond', 'promptd', 'openwave-portal', 'wallpaperagentd', 'timed', 'connectivityd', 'audiod', 'xdg-desktop-portal-openwave'}
BUNDLES = {'com.openwave.shell': 'ocean-launcher', 'com.openwave.ocean': 'ocean',
           'com.openwave.files': 'ocean-files', 'com.openwave.settings': 'ocean-settings',
           'com.openwave.photos': 'ocean-photos'}


def runtime_path(rel):
    p = rel.parts
    if len(p) >= 3 and p[:2] == ('usr', 'bin') and p[2] in LEGACY_APPS:
        return False
    if len(p) >= 3 and p[:2] == ('usr', 'libexec') and p[2] in SERVICES:
        return False
    if len(p) >= 3 and p[:3] == ('usr', 'share', 'ocean-browser'):
        return False
    if p[0] in ('System', 'Applications') and (rel.suffix in DEVEL_EXT or '.git' in p):
        return False
    if rel.as_posix() in {'usr/lib/fonts/SourceSansPro-ExtraLight.otf',
                          'usr/lib64/fonts/SourceSansPro-ExtraLight.otf'}:
        return False
    # User data and persistent service state are not development installations.
    # Preserve arbitrary names/extensions, including a user's source files.
    if p[0] != 'usr':
        return True
    if rel.suffix in DEVEL_EXT or '.git' in p or '__MACOSX' in p or rel.name == '.DS_Store':
        return False
    if len(p) < 3:
        return False
    kind, name = p[1:3]
    if kind == 'bin':
        return name in BIN
    if kind == 'sbin':
        return name in SBIN
    if kind == 'libexec':
        return not name.startswith(('qt-', 'qt_', 'nm-initrd-'))
    if kind == 'share':
        # ICU was configured with external data rather than embedding it in
        # libicudata. JavaScriptCore/Qt need this data, not ICU's build scripts.
        if name == 'icu':
            return rel.suffix == '.dat' or rel.name == 'LICENSE'
        return name in SHARE
    if kind == 'plugins':
        plugin = '/'.join(p[2:])
        return plugin in PLUGINS or (name == 'imageformats') or (name == 'generic' and rel.name.startswith('libqevdev'))
    if kind == 'qml':
        # Choose the nearest module boundary; do not recursively copy QtQuick's
        # unused child modules or optional Controls styles.
        if '/'.join(p[2:-1]) in QML:
            return True
        for count in range(len(p)-1, 2, -1):
            directory = '/'.join(p[2:count])
            if (SYSROOT / 'usr/qml' / directory / 'qmldir').exists():
                return directory in QML
        return False
    if kind in ('lib', 'lib64'):
        if name in DEVEL:
            return False
        if len(p) == 3:
            return name == 'fonts'  # top-level shared libraries added by ELF closure
        return True
    return False


def copy_file(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_dir() and not dst.is_symlink():
        shutil.rmtree(dst)
    elif dst.exists() or dst.is_symlink():
        dst.unlink()
    if src.is_symlink():
        target = os.readlink(src)
        # SDK Qt prefix symlinks must never escape into the runtime filesystem.
        if src.is_dir():
            dst.symlink_to(target)
        elif not target.startswith('/') and (src.parent / target).resolve().is_relative_to(SDK.resolve()):
            resolved = (src.parent / target).resolve()
            if not target.startswith('../') and (src.parent / target).parent == src.parent:
                dst.symlink_to(target)
            else:
                shutil.copy2(resolved, dst)
        else:
            dst.symlink_to(target)
    else:
        shutil.copy2(src, dst)


def files(prefix):
    # os.walk follows exported Qt directory links, but each path is still an
    # on-device /usr/... relative path, never the SDK's installation location.
    for directory, dirs, names in os.walk(prefix, followlinks=True):
        rel_directory = Path(directory).relative_to(prefix)
        if rel_directory.parts and rel_directory.parts[0] == 'usr':
            dirs[:] = [d for d in dirs if d not in ('.git', '__MACOSX')]
        for name in names:
            yield Path(directory) / name
        for name in list(dirs):
            path = Path(directory) / name
            if path.is_symlink() and (os.readlink(path).startswith('/') or path.resolve().is_relative_to(prefix.resolve())):
                dirs.remove(name)
                yield path


def elf(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


def needed(path):
    import struct
    with path.open('rb') as stream:
        header = stream.read(20)
    expected = 62 if TRIPLE.split('-')[0] == 'x86_64' else 183
    if header[4:6] != b'\x02\x01' or struct.unpack('<H', header[18:20])[0] != expected:
        raise RuntimeError(f'Wrong ELF architecture in {path}; expected {TRIPLE}')
    info = subprocess.check_output([str(SDK / 'bin/llvm-readelf'), '-d', '-l', str(path)], text=True)
    names = re.findall(r'\(NEEDED\).*?\[(.*?)\]', info)
    interpreter = re.search(r'Requesting program interpreter: (.*?)\]', info)
    if interpreter:
        names.append(Path(interpreter[1]).name)
    for rpath in re.findall(r'\((?:RPATH|RUNPATH)\).*?\[(.*?)\]', info):
        entries = rpath.split(':')
        valid = [entry for entry in entries if entry.startswith('$ORIGIN') or entry.startswith(('/usr/', '/lib'))]
        if valid != entries:
            patcher = SDK / 'bin/patchelf'
            if not patcher.is_file():
                found = shutil.which('patchelf')
                if not found:
                    raise RuntimeError(f'Install host patchelf to remove development RPATH in {path}: {rpath}')
                patcher = Path(found)
            subprocess.run([str(patcher), '--set-rpath', ':'.join(valid), str(path)], check=True)
            RPATH_FIXES.append({'file': str(path.relative_to(next(parent for parent in path.parents if parent.name.startswith('.rootfs-')))), 'before': rpath, 'after': ':'.join(valid)})
    return names


def assemble(base):
    build = Path(tempfile.mkdtemp(prefix='.rootfs-', dir=ROOT / 'out'))
    libraries = {}
    resources = {}
    library_priorities = {}
    try:
        for source_index, source in enumerate((base, SYSROOT, STAGE)):
            if not source.exists():
                continue
            for directory, dirs, names in os.walk(source):
                rel_dir = Path(directory).relative_to(source)
                if rel_dir.parts and rel_dir.parts[0] != 'usr':
                    dest_dir = build / rel_dir
                    dest_dir.mkdir(parents=True, exist_ok=True)
                    dest_dir.chmod(Path(directory).stat().st_mode & 0o7777)
            for src in files(source):
                rel = src.relative_to(source)
                # Keep empty mount/state directories and their private permissions.
                for ancestor in reversed(rel.parents):
                    if ancestor == Path('.'):
                        continue
                    origin = source / ancestor
                    target = build / ancestor
                    if target.is_dir() and origin.is_dir():
                        target.chmod(origin.stat().st_mode & 0o7777)
                # Library precedence is SDK over baseline, then current staging.
                if '.so' in src.name:
                    if rel.parent.as_posix() in ('lib', 'lib64', 'usr/lib', 'usr/lib64'):
                        # Prefer the target loader's lib64 search directory for
                        # duplicate sonames. Qt's unique libraries stay in /usr/lib.
                        priority = (source_index, {'lib64': 4, 'usr/lib64': 3, 'lib': 2, 'usr/lib': 1}[rel.parent.as_posix()])
                        if priority > library_priorities.get(src.name, (-1, -1)):
                            libraries[src.name] = (src, rel)
                            library_priorities[src.name] = priority
                    elif rel.parts[0] not in ('System', 'Applications'):
                        libraries.setdefault(src.name, (src, rel))
                if runtime_path(rel):
                    resources[rel] = src
        # A preserved directory symlink replaces any old materialized subtree.
        links = [rel for rel, src in resources.items() if src.is_symlink() and src.is_dir()]
        resources = {rel: src for rel, src in resources.items() if not any(link in rel.parents for link in links)}
        for rel, src in resources.items():
            copy_file(src, build / rel)
        # Restore directory permissions after parent creation, including private state.
        for source_index, source in enumerate((base, SYSROOT, STAGE)):
            if not source.exists():
                continue
            for directory, dirs, names in os.walk(source):
                rel = Path(directory).relative_to(source)
                dest = build / rel
                if dest.is_dir():
                    dest.chmod(Path(directory).stat().st_mode & 0o7777)
        # Shared SDK API implementations are runtime dependencies, not SDK headers.
        for src in (SYSROOT / 'usr/lib64').glob('libopenwave-*.so*'):
            libraries[src.name] = (src, Path('usr/lib64') / src.name)
        # Libraries loaded by name rather than DT_NEEDED: NSS, EGL/GL dispatch,
        # GStreamer, PipeWire, codecs, and WPE are seeded with their plugin trees.
        dynamic = ['libnss_files.so.2', 'libnss_dns.so.2', 'libnss_compat.so.2',
                   'libEGL.so.1', 'libGLESv2.so.2', 'libgbm.so.1']
        # Keep the SELinux runtime available to native clients before services
        # start linking it. Development headers/static archives stay in the SDK.
        selinux = ['libselinux.so.1', 'libsepol.so.2']
        if any((SYSROOT / 'usr/lib64' / name).exists() for name in selinux):
            if not all((SYSROOT / 'usr/lib64' / name).is_file() for name in selinux):
                raise RuntimeError('Incomplete SELinux SDK: run ./build-openwave selinux')
            dynamic.extend(selinux)
        queue = [p for p in files(build) if elf(p)]
        requested = set(dynamic)
        scanned = set()
        while queue or requested:
            for name in sorted(requested):
                if name not in libraries:
                    raise RuntimeError(f'Missing target runtime dependency: {name}')
                src, rel = libraries[name]
                dest = build / rel
                if dest.exists() or dest.is_symlink():
                    continue
                copy_file(src, dest)
                # Preserve soname chains but materialize their final file in rootfs.
                while dest.is_symlink():
                    target = os.readlink(dest)
                    rel = Path(target.lstrip('/')) if target.startswith('/') else rel.parent / target
                    dest = build / rel
                    if dest.exists():
                        break
                    origin = src.parent / target if not target.startswith('/') else SYSROOT / target.lstrip('/')
                    copy_file(origin, dest)
                    src = origin
                if elf(dest):
                    queue.append(dest)
            requested = set()
            while queue:
                path = queue.pop()
                if path in scanned:
                    continue
                scanned.add(path)
                dependencies = needed(path)
                private_lib = None
                rel = path.relative_to(build)
                if rel.parts[0] in ('System', 'Applications'):
                    bundle_depth = 3 if rel.parts[0] == 'System' else 2
                    private_lib = build.joinpath(*rel.parts[:bundle_depth], 'lib')
                requested.update(name for name in dependencies if not private_lib or not (private_lib / name).is_file())
        # Empty runtime mount points and the existing private data layout.
        for name in ('dev', 'proc', 'sys', 'run', 'tmp', 'var', 'Data', 'System', 'System/Applications', 'System/Services', 'Applications'):
            (build / name).mkdir(exist_ok=True)
        (build / 'tmp').chmod(0o1777)
        build.chmod(0o755)
        # appd trusts administrative metadata/executables only through roots
        # that cannot be written by a group. Build-host umasks are not policy.
        for name in ('usr', 'usr/bin', 'usr/libexec', 'usr/share',
                     'usr/share/applications', 'usr/share/pixmaps', 'etc/openwave',
                     'var', 'var/lib'):
            (build / name).chmod(0o755)
        for root in ('System', 'System/Applications', 'System/Services', 'Applications'):
            (build / root).chmod(0o755)
        for bundle_root in ('System/Applications', 'Applications'):
            for bundle in (build / bundle_root).iterdir():
                if not bundle.is_dir() or bundle.is_symlink():
                    raise RuntimeError(f'Invalid installed bundle directory: {bundle}')
                manifest_path = bundle / 'manifest.json'
                if manifest_path.stat().st_size > 65536:
                    raise RuntimeError(f'Oversized manifest: {manifest_path}')
                manifest = json.loads(manifest_path.read_text())
                appid = bundle.name
                executable = manifest.get('executable', '')
                if manifest.get('id') != appid or manifest.get('schemaVersion') != 1 or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*(?:\.[A-Za-z0-9][A-Za-z0-9_-]*){2,}', appid):
                    raise RuntimeError(f'Invalid installed bundle identity: {appid}')
                if not executable.startswith('bin/') or any(part in ('', '.', '..') for part in executable.split('/')) or not (bundle / executable).is_file():
                    raise RuntimeError(f'Invalid bundle executable: {appid}')
                if appid in BUNDLES and executable != f'bin/{BUNDLES[appid]}':
                    raise RuntimeError(f'Unexpected system bundle executable: {appid}')
                for directory in ('lib', 'plugins', 'qml', 'resources'):
                    (bundle / directory).mkdir(exist_ok=True)
                for directory, dirs, names in os.walk(bundle):
                    Path(directory).chmod(0o755)
                    for name in dirs + names:
                        path = Path(directory) / name
                        if path.is_symlink():
                            raise RuntimeError(f'Symlinks are not accepted in installed bundles: {path}')
                        if path.is_file():
                            path.chmod(0o755 if os.access(path, os.X_OK) else 0o644)
        required = ['init'] + [f'System/Applications/{appid}/bin/{exe}' for appid, exe in BUNDLES.items()]
        required += [f'System/Services/{name}' for name in SERVICES | {'openwave-launch'}]
        for rel in required:
            if not (build / rel).exists():
                raise RuntimeError(f'Missing runtime entry point: {rel}')
        # Fail closed if any development artifact or escaping SDK link slipped in.
        for path in files(build):
            if path.relative_to(build).parts[0] == 'usr' and path.suffix in DEVEL_EXT:
                raise RuntimeError(f'Development file in rootfs: {path}')
            if path.is_symlink() and ('toolchain' in os.readlink(path) or 'fakeroot' in os.readlink(path)):
                raise RuntimeError(f'SDK link in runtime: {path}')
        report = {'target': TRIPLE, 'elf_files': len(scanned),
                  'bytes': sum(p.stat().st_size for p in files(build) if not p.is_symlink()),
                  'rpath_fixes': RPATH_FIXES, 'qml_modules': sorted(QML), 'qt_plugins': sorted(PLUGINS)}
        (ROOT / 'out/rootfs-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
        backup = ROOT / 'out/rootfs-previous'
        if backup.exists():
            shutil.rmtree(backup)
        if RUNTIME.exists():
            RUNTIME.rename(backup)
        build.rename(RUNTIME)
        print(f'Runtime tree: {RUNTIME}, {report["bytes"] / 1048576:.1f} MiB; {len(scanned)} ELF files checked')
    except BaseException:
        shutil.rmtree(build)
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=RUNTIME)
    args = parser.parse_args()
    if not args.base.exists():
        raise SystemExit('Specify --base fakeroot for the first runtime migration')
    assemble(args.base.resolve())
