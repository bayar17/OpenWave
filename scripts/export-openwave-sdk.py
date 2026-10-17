#!/usr/bin/env python3
"""Export modified Clang, matching Qt and OpenWave APIs into one toolchain prefix."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
from openwave_sdk_layout import configure_tools, merge_existing
from openwave_sdk_bootstrap import (export_compiler, export_runtimes, export_host_qt,
                                   export_host_utilities, qt_version, run,
                                   copy_installation as copy)

ROOT = Path(__file__).resolve().parents[1]
TRIPLE = 'x86_64-unknown-openwave'


def relocate_cmake(prefix, replacements):
    for file in prefix.rglob('*.cmake'):
        if file.is_symlink():
            continue
        text = file.read_text()
        for old, new in replacements.items():
            text = text.replace(str(old), '${CMAKE_CURRENT_LIST_DIR}/' + os.path.relpath(new, file.parent))
        file.write_text(text)


def profile(sdk, triple):
    processor = triple.split('-')[0]
    cmake = sdk / 'cmake'
    cmake.mkdir(exist_ok=True)
    template = (ROOT / 'cross/sdk-toolchain.cmake.in').read_text()
    (cmake / f'openwave-{processor}.cmake').write_text(template.replace('@TRIPLE@', triple).replace('@PROCESSOR@', processor))
    copy(ROOT / 'cross/generate-meson.py', cmake / 'generate-meson.py')
    subprocess.run(['python3', str(cmake / 'generate-meson.py'), '--target', triple], check=True)
    qt_toolchain = sdk / 'sysroot' / triple / 'usr/lib/cmake/Qt6/qt.toolchain.cmake'
    relative = '${CMAKE_CURRENT_LIST_DIR}/' + os.path.relpath(cmake / f'openwave-{processor}.cmake', qt_toolchain.parent)
    content = re.sub(r'set\(__qt_initially_configured_toolchain_file "[^"]*"\)',
                     f'set(__qt_initially_configured_toolchain_file "{relative}")', qt_toolchain.read_text())
    qt_toolchain.write_text(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', default=TRIPLE, choices=(TRIPLE, 'aarch64-unknown-openwave'))
    parser.add_argument('--source', type=Path, default=ROOT / 'out/legacy-fakeroot')
    parser.add_argument('--host-source', type=Path, help='Existing host Qt installation; defaults to the shared SDK')
    parser.add_argument('--compiler-source', type=Path, help='Existing modified LLVM installation')
    parser.add_argument('--compiler-build', type=Path, default=ROOT / 'out/llvm-build', help='Built LLVM tree to install without rebuilding')
    parser.add_argument('--host-build', type=Path, default=ROOT / 'out/sdk-host-qt', help='Native Qt recovery build cache')
    parser.add_argument('--qt-source', type=Path, default=ROOT / 'third_party/qt6', help='Matching Qt sources, used only if host tools are missing')
    parser.add_argument('--host-dependencies', type=Path, default=ROOT / 'out/sdk-host-dependencies/usr', help='Optional native dependency prefix for host Qt recovery')
    parser.add_argument('--jobs', type=int, default=4, help='Parallel jobs for host Qt/API builds (default: 4)')
    parser.add_argument('--merge-existing', action='store_true', help='Merge former vendor folders without copying or rebuilding installed binaries')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    run('bash', ROOT / 'build-openwave', 'llvm-patch')
    sdk = ROOT / 'toolchain'
    sdk.mkdir(exist_ok=True)
    if args.merge_existing:
        merge_existing(sdk)
    else:
        if (sdk / 'clang').exists() or (sdk / 'qt').exists():
            raise SystemExit('First migrate installed tools with --merge-existing')
        source = args.source.resolve()
        sysroot = sdk / 'sysroot' / args.target
        if source == sysroot.resolve():
            raise SystemExit('Source must be a separate target dependency installation')
        with (source / 'usr/lib/libQt6Core.so.6').open('rb') as stream:
            header = stream.read(20)
        expected = 62 if args.target.startswith('x86_64-') else 183
        if len(header) != 20 or header[:4] != b'\x7fELF' or struct.unpack('<H', header[18:20])[0] != expected:
            raise SystemExit('Source Qt architecture does not match the SDK target')
        host = (args.host_source or sdk).resolve()
        version = qt_version(source / 'usr')
        if args.host_source and qt_version(host) != version:
            raise SystemExit(f'Host Qt must match target Qt {version}: {host}')
        export_compiler(sdk, args.compiler_build, args.compiler_source)
        export_runtimes(sdk, ROOT, args.compiler_build, args.target)
        export_host_qt(sdk, ROOT, version, args.host_build, args.qt_source, args.jobs,
                       args.host_source, args.host_dependencies)
        export_host_utilities(sdk, ROOT, version, args.qt_source)
        for name in ('lib', 'lib64', 'usr'):
            copy(source / name, sysroot / name)
        shutil.rmtree(sysroot / 'usr/sbin', ignore_errors=True)
        for name in ('audiod', 'connectivityd', 'timed', 'wallpaperagentd', 'promptd', 'openwave-portal'):
            (sysroot / 'usr/libexec' / name).unlink(missing_ok=True)
        for file in (sysroot / 'usr/bin').glob('*'):
            if file.name.startswith(('ocean-', 'openwave-')):
                file.unlink()
        relocate_cmake(sdk / 'lib/cmake', {ROOT / 'qt-host': sdk, host: sdk})
        qt = sysroot / 'usr'
        relocate_cmake(qt / 'lib/cmake', {source / 'usr': qt, ROOT / 'fakeroot/usr': qt, ROOT / 'qt-host': sdk, host: sdk})
        for file in (qt / 'mkspecs').rglob('*.pri'):
            file.write_text(file.read_text().replace(str(source), '$$[QT_SYSROOT]').replace(str(ROOT / 'fakeroot'), '$$[QT_SYSROOT]'))
    if not (sdk / 'bin/clang').is_file():
        raise SystemExit('Missing modified Clang installation in toolchain/bin')
    configure_tools(sdk, args.target)
    for sysroot in (sdk / 'sysroot').iterdir():
        if (sysroot / 'usr/lib/cmake/Qt6').is_dir():
            profile(sdk, sysroot.name)
    copy(ROOT / 'userspace/sdk/README.md', sdk / 'README.md')
    env = os.environ.copy()
    env.update(OPENWAVE_SDK=str(sdk), OPENWAVE_TARGET_TRIPLE=args.target,
               OPENWAVE_BUILD_JOBS=str(args.jobs))
    run('bash', ROOT / 'build-openwave', 'sdk', env=env)
    print(f'Complete Clang, Qt and OpenWave API SDK exported: {sdk}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'SDK export failed: {error}') from error
