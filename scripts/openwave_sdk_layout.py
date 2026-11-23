"""Shared-prefix layout for the installed OpenWave compiler, Qt and APIs."""
import hashlib
import os
from pathlib import Path
import re
import shutil


def identical(a, b):
    if a.is_symlink() or b.is_symlink():
        return a.is_symlink() and b.is_symlink() and a.readlink() == b.readlink()
    if not a.is_file() or not b.is_file():
        return False
    if a.samefile(b):
        return True
    return a.stat().st_size == b.stat().st_size and hashlib.sha256(a.read_bytes()).digest() == hashlib.sha256(b.read_bytes()).digest()


def move_tree(source, destination):
    """Merge a preflighted tree, preserving installed files and symlinks."""
    if source.is_dir() and not source.is_symlink():
        destination.mkdir(parents=True, exist_ok=True)
        for child in list(source.iterdir()):
            move_tree(child, destination / child.name)
        source.rmdir()
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() or destination.is_symlink():
            if not identical(source, destination):
                raise RuntimeError(f"Conflicting SDK files: {source} and {destination}")
            source.unlink()
        else:
            source.rename(destination)


def merge_existing(sdk):
    """Migrate the former vendor prefixes into one SDK prefix without rebuilding."""
    compiler, host = sdk / 'clang', sdk / 'qt/host'
    if not compiler.exists() and not host.exists():
        return False
    if not compiler.is_dir() or not host.is_dir():
        raise RuntimeError('Expected both installed Clang and host Qt before migration')
    groups = []
    for name in ('bin', 'lib', 'include', 'share'):
        if (sdk / name).is_symlink() and (sdk / name).readlink() != Path('clang') / name:
            raise RuntimeError(f'Unexpected SDK alias: {sdk / name}')
        groups.append((compiler / name, sdk / name))
    groups.append((compiler / 'libexec', sdk / 'bin'))
    for child in host.iterdir():
        if child.name == 'bin':
            for tool in child.iterdir():
                name = tool.name
                if name in ('qmake', 'qmake6', 'qtpaths', 'qtpaths6'):
                    name = name.replace('6', '') + '-host' + ('6' if name.endswith('6') else '')
                groups.append((tool, sdk / 'bin' / name))
        else:
            groups.append((child, sdk / ('bin' if child.name == 'libexec' else child.name)))
    targets = list((sdk / 'qt/target').iterdir())
    for target in targets:
        groups.append((target, sdk / 'sysroot' / target.name / 'usr'))
    api = sdk / 'openwave-api'
    groups.append((api / 'include', sdk / 'include'))
    for target in (api / 'lib').iterdir():
        groups.append((target, sdk / 'sysroot' / target.name / 'usr/lib64'))
    packaging = sdk / 'host-tools/usr'
    for child in packaging.iterdir():
        groups.append((child, sdk / child.name))
    # Validate all collisions before moving anything. Parent aliases currently
    # resolve to the original installed files and are removed only after this.
    destinations = {}
    for source, destination in groups:
        leaves = list(source.rglob('*')) if source.is_dir() and not source.is_symlink() else [source]
        for leaf in leaves:
            if leaf.is_dir() and not leaf.is_symlink():
                continue
            dest = destination / leaf.relative_to(source) if leaf != source else destination
            previous = destinations.get(dest)
            if previous and not identical(previous, leaf):
                raise RuntimeError(f'SDK filename collision: {previous} and {leaf}')
            if (dest.exists() or dest.is_symlink()) and not identical(leaf, dest):
                # Directory links in the sysroot point to the same Qt prefix;
                # leaf symlinks may use a different path to that identical file.
                if not (dest.exists() and leaf.exists() and dest.samefile(leaf)):
                    raise RuntimeError(f'Existing SDK file would be overwritten: {dest}')
            destinations[dest] = leaf
    mapping = {host: sdk, compiler: sdk}
    mapping.update({target: sdk / 'sysroot' / target.name / 'usr' for target in targets})
    # Repair the few cross-prefix references already made relative by export.
    pattern = re.compile(r'\$\{CMAKE_CURRENT_LIST_DIR\}(/[^\s";()]+)')
    for prefix in (host, *targets):
        for file in prefix.rglob('*.cmake'):
            if file.is_symlink():
                continue
            new_file = mapping[prefix] / file.relative_to(prefix)
            def rewrite(match):
                suffix = match.group(1)
                if '${' in suffix:
                    return match.group(0)
                old_path = Path(os.path.normpath(str(file.parent) + suffix))
                for old, new in mapping.items():
                    if old_path.is_relative_to(old):
                        new_path = new / old_path.relative_to(old)
                        return '${CMAKE_CURRENT_LIST_DIR}/' + os.path.relpath(new_path, new_file.parent)
                return match.group(0)
            original = file.read_text()
            updated = pattern.sub(rewrite, original)
            if updated != original:
                file.write_text(updated)
    for name in ('bin', 'lib', 'include', 'share', 'libexec'):
        path = sdk / name
        if path.is_symlink():
            path.unlink()
    # Materialize target Qt in the sysroot by moving its installed files there.
    for target in targets:
        for path in list((sdk / 'sysroot' / target.name).rglob('*')):
            if path.is_symlink() and path.resolve().is_relative_to(target):
                path.unlink()
    for source, destination in groups:
        move_tree(source, destination)
    compiler.rmdir()
    (host / 'bin').rmdir()
    host.rmdir()
    (sdk / 'qt/target').rmdir()
    (sdk / 'qt').rmdir()
    # Former exports are regenerated by the API build for the common prefix.
    shutil.rmtree(api)
    shutil.rmtree(sdk / 'host-tools')
    (sdk / 'libexec').symlink_to('bin')
    return True


def configure_tools(sdk, triple):
    """All development executables live in bin; target libraries stay in sysroot."""
    tools, qt = sdk / 'bin', sdk / 'sysroot' / triple / 'usr'
    (tools / 'qt.conf').write_text('[Paths]\nPrefix=..\nLibraryExecutables=bin\n')
    (tools / 'target_qt.conf').write_text(f'''[DevicePaths]
Prefix=/usr
[Paths]
Prefix=../sysroot/{triple}/usr
Headers=include
Libraries=lib
LibraryExecutables=libexec
Binaries=bin
Plugins=plugins
QmlImports=qml
ArchData=.
Data=.
HostPrefix=..
HostBinaries=bin
HostLibraries=lib
HostLibraryExecutables=bin
HostData=sysroot/{triple}/usr
Sysroot=../sysroot/{triple}
SysrootifyPrefix=false
TargetSpec=linux-clang
HostSpec=linux-g++
''')
    wrapper = '''#!/bin/sh
script_dir_path=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
export PATH="$script_dir_path:$PATH"
case " $* " in
    *" -query "*|*" -v "*|*" -version "*)
        exec "$script_dir_path/qmake-host6" -qtconf "$script_dir_path/target_qt.conf" "$@" ;;
esac
exec "$script_dir_path/qmake-host6" -qtconf "$script_dir_path/target_qt.conf" "$@" \
    "QMAKE_CC=$script_dir_path/clang" "QMAKE_CXX=$script_dir_path/clang++" \
    "QMAKE_LINK=$script_dir_path/clang++" "QMAKE_LINK_SHLIB=$script_dir_path/clang++" \
    "QMAKE_AR=$script_dir_path/llvm-ar cqs" \
    "QMAKE_CFLAGS+=--target=@TRIPLE@" "QMAKE_CXXFLAGS+=--target=@TRIPLE@" "QMAKE_LFLAGS+=--target=@TRIPLE@"
'''.replace('@TRIPLE@', triple)
    paths = '''#!/bin/sh
script_dir_path=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
exec "$script_dir_path/qtpaths-host6" -qtconf "$script_dir_path/target_qt.conf" "$@"
'''
    cmake = '''#!/bin/sh
script_dir_path=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
exec cmake -DCMAKE_TOOLCHAIN_FILE="$script_dir_path/../cmake/openwave-@ARCH@.cmake" "$@"
'''.replace('@ARCH@', triple.split('-')[0])
    for name, content in (('qmake', wrapper), ('qmake6', wrapper), ('qtpaths', paths), ('qtpaths6', paths), ('qt-cmake', cmake)):
        file = tools / name
        file.write_text(content)
        file.chmod(0o755)
        target_file = qt / 'bin' / name
        target_file.unlink(missing_ok=True)
        target_file.symlink_to(os.path.relpath(file, target_file.parent))
    config = qt / 'bin/target_qt.conf'
    config.unlink(missing_ok=True)
    config.symlink_to(os.path.relpath(tools / 'target_qt.conf', config.parent))
