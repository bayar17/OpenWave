"""Recover real compiler/host Qt installations for the shared SDK exporter."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from openwave_sdk_layout import move_tree


def copy_installation(source, destination):
    """Overlay an installed tree while replacing, rather than following, links."""
    if source.is_symlink():
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.unlink(missing_ok=True)
        destination.symlink_to(source.readlink())
    elif source.is_dir():
        if destination.is_symlink():
            destination.unlink()
        destination.mkdir(parents=True, exist_ok=True)
        for child in source.iterdir():
            copy_installation(child, destination / child.name)
        shutil.copystat(source, destination)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink():
            destination.unlink()
        shutil.copy2(source, destination)


def run(*command, **kwargs):
    print('+ ' + ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def qt_version(prefix):
    config = prefix / 'lib/cmake/Qt6/Qt6ConfigVersion.cmake'
    implementation = config.with_name('Qt6ConfigVersionImpl.cmake')
    if implementation.is_file():
        config = implementation
    if not config.is_file():
        raise RuntimeError(f'Missing Qt installation metadata: {config}')
    match = re.search(r'set\(PACKAGE_VERSION "([^"]+)"\)', config.read_text())
    if not match:
        raise RuntimeError(f'Cannot determine Qt version: {config}')
    return match[1]


def export_compiler(sdk, build, source=None):
    if source:
        if not (source / 'bin/clang').is_file():
            raise RuntimeError(f'Missing modified Clang installation: {source}')
        if source.resolve() != sdk.resolve():
            for name in ('bin', 'lib', 'libexec', 'include', 'share'):
                if (source / name).is_dir():
                    copy_installation(source / name, sdk / name)
    elif not (sdk / 'bin/clang').is_file():
        if not (build / 'bin/clang').is_file() or not (build / 'cmake_install.cmake').is_file():
            raise RuntimeError(f'Missing existing LLVM build: {build}; provide --compiler-source')
        # Install the modified compiler verbatim. Never invoke the LLVM build.
        run('cmake', '--install', build, '--prefix', sdk)
    run(sdk / 'bin/clang', '--version')


def export_runtimes(sdk, root, build, triple):
    resource = Path(subprocess.check_output([str(sdk / 'bin/clang'), '-print-resource-dir'], text=True).strip())
    # LLVM's install rules omit the separately staged OpenWave CRT/builtins.
    # Preserve these from the existing build's resource directory as well.
    staged = build / 'lib/clang' / resource.name / 'lib'
    if staged.is_dir():
        copy_installation(staged, resource / 'lib')
    arch = triple.split('-')[0]
    archive = resource / f'lib/linux/libclang_rt.builtins-{arch}.a'
    if not archive.is_file():
        source = root / 'out/compiler-rt-builtins/lib/linux'
        if (source / archive.name).is_file():
            archive.parent.mkdir(parents=True, exist_ok=True)
            for file in source.glob('*.a'):
                shutil.copy2(file, archive.parent / file.name)
    for name in ('clang_rt.crtbegin.o', 'clang_rt.crtend.o', 'libclang_rt.builtins.a'):
        if not (resource / 'lib' / triple / name).is_file():
            raise RuntimeError(f'Missing {triple} compiler runtime {name}; restore it in the LLVM resource directory')


def native_environment(sdk):
    env = os.environ.copy()
    # The host Qt build must never inherit target compilers, sysroot or packages.
    for name in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                 'CMAKE_TOOLCHAIN_FILE', 'CMAKE_PREFIX_PATH', 'QT_HOST_PATH',
                 'PKG_CONFIG_SYSROOT_DIR', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_PATH',
                 'QMAKESPEC', 'QMAKEPATH', 'QTDIR', 'LD_LIBRARY_PATH'):
        env.pop(name, None)
    env['PATH'] = os.pathsep.join(path for path in env['PATH'].split(os.pathsep)
                                   if Path(path).resolve() != (sdk / 'bin').resolve())
    return env


def export_host_qt(sdk, root, version, build, qt_source, jobs, source=None, dependencies=None):
    if source:
        if qt_version(source) != version:
            raise RuntimeError(f'Host Qt must match target Qt {version}: {source}')
        if source.resolve() != sdk.resolve():
            # Import only the host installation, never its embedded sysroots/API.
            for name in ('bin', 'lib', 'libexec', 'include', 'share', 'plugins', 'qml', 'mkspecs', 'doc', 'translations'):
                if (source / name).is_dir():
                    copy_installation(source / name, sdk / name)
    elif not all((sdk / 'bin' / name).is_file() for name in
                 ('qmake-host6', 'qtpaths-host6', 'moc', 'rcc', 'qsb', 'qmlcachegen')):
        conf = qt_source / 'qtbase/.cmake.conf'
        if not conf.is_file() or f'QT_REPO_MODULE_VERSION "{version}"' not in conf.read_text():
            raise RuntimeError(f'Matching host Qt {version} sources required; provide --host-source or --qt-source')
        env = native_environment(sdk)
        extra = []
        if dependencies and dependencies.is_dir():
            extra.append(f'-DCMAKE_PREFIX_PATH={dependencies}')
            # Recheck a formerly incomplete local PNG installation rather than
            # retaining a non-PIC static archive selected before shared aliases
            # were restored. Qt Gui requires a shared or PIC PNG library.
            extra.extend(('-U', 'PNG_LIBRARY_*'))
            env['PKG_CONFIG_PATH'] = os.pathsep.join(str(path) for path in
                dependencies.glob('lib*/**/pkgconfig'))
        run('cmake', '-S', qt_source, '-B', build, '-G', 'Ninja',
            '-C', root / 'cross/qt-host-sdk.cmake', '-DCMAKE_BUILD_TYPE=Release',
            f'-DCMAKE_INSTALL_PREFIX={sdk}', '-DCMAKE_C_COMPILER=/usr/bin/cc',
            '-DCMAKE_CXX_COMPILER=/usr/bin/c++', '-DQT_BUILD_TESTS=OFF',
            '-DQT_BUILD_EXAMPLES=OFF', '-DQT_BUILD_DOCS=OFF', '-DBUILD_WITH_PCH=OFF',
            '-DQT_BUILD_SUBMODULES=qtbase;qtshadertools;qtdeclarative;qtsvg;qtimageformats', *extra, env=env)
        run('cmake', '--build', build, '--parallel', jobs, env=env)
        run('cmake', '--install', build, '--prefix', sdk, env=env)
    if qt_version(sdk) != version:
        raise RuntimeError(f'Exported host Qt does not match target Qt {version}')
    for name in ('qmake', 'qmake6', 'qtpaths', 'qtpaths6'):
        original = sdk / 'bin' / name
        renamed = sdk / 'bin' / (name.replace('6', '') + '-host' + ('6' if name.endswith('6') else ''))
        # Existing target wrappers are not native tools; retain qmake-host6.
        if original.is_file() and not original.is_symlink() and original.read_bytes()[:4] == b'\x7fELF':
            original.replace(renamed)
        elif original.is_symlink() and not renamed.exists():
            # qmake/qtpaths aliases refer to the newly renamed native executable.
            target = original.readlink().name
            renamed.symlink_to(target.replace('6', '') + '-host' + ('6' if target.endswith('6') else ''))
    libexec = sdk / 'libexec'
    if libexec.is_dir() and not libexec.is_symlink():
        move_tree(libexec, sdk / 'bin')
    if not libexec.exists():
        libexec.symlink_to('bin')
    for name in ('qmake-host6', 'qtpaths-host6', 'moc', 'rcc', 'qsb', 'qmlcachegen'):
        if not (sdk / 'bin' / name).is_file():
            raise RuntimeError(f'Missing host Qt tool: {name}')


def export_host_utilities(sdk, root, version, qt_source):
    patchelf = sdk / 'bin/patchelf'
    if not patchelf.is_file():
        system = shutil.which('patchelf')
        if system:
            shutil.copy2(system, patchelf)
        else:
            packages = sorted((root / 'out/host-packages').glob('patchelf_*_amd64.deb'))
            if not packages:
                raise RuntimeError('patchelf is required for relocatable host utilities')
            with tempfile.TemporaryDirectory(prefix='openwave-host-tools-') as temporary:
                run('dpkg-deb', '-x', packages[-1], temporary)
                shutil.copy2(Path(temporary) / 'usr/bin/patchelf', patchelf)
                docs = Path(temporary) / 'usr/share/doc/patchelf'
                shutil.copytree(docs, sdk / 'share/doc/patchelf', dirs_exist_ok=True)
    scanner = sdk / 'bin/qtwaylandscanner'
    if not scanner.is_file():
        source = root / 'out/qtwaylandscanner-host/qtwaylandscanner'
        if not source.is_file():
            cpp = qt_source / 'qtbase/src/tools/qtwaylandscanner/qtwaylandscanner.cpp'
            if not cpp.is_file():
                raise RuntimeError('Missing qtwaylandscanner and its matching source')
            source.parent.mkdir(parents=True, exist_ok=True)
            run('/usr/bin/c++', '-std=c++17', '-O2', '-fPIC', cpp,
                f'-I{sdk / "include"}', f'-I{sdk / "include/QtCore"}',
                f'-L{sdk / "lib"}', '-lQt6Core', '-o', source,
                env=native_environment(sdk))
        shutil.copy2(source, scanner)
    run(patchelf, '--set-rpath', '$ORIGIN/../lib', scanner)
    # This real tool was built separately from the original host Qt. Supply its
    # generated-style import package without referencing the repository build.
    package = sdk / 'lib/cmake/Qt6WaylandScannerTools'
    if not (package / 'Qt6WaylandScannerToolsConfig.cmake').is_file():
        package.mkdir(parents=True, exist_ok=True)
        macros = qt_source / 'qtbase/src/tools/qtwaylandscanner'
        for name in ('Qt6WaylandClientMacros.cmake', 'Qt6WaylandCompositorMacros.cmake'):
            shutil.copy2(macros / name, package / name)
        (package / 'Qt6WaylandScannerToolsConfig.cmake').write_text('''
if(NOT TARGET Qt6::qtwaylandscanner)
    add_executable(Qt6::qtwaylandscanner IMPORTED)
    set_target_properties(Qt6::qtwaylandscanner PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../../bin/qtwaylandscanner")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/Qt6WaylandClientMacros.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Qt6WaylandCompositorMacros.cmake")
''')
        run('cmake', f'-DOPENWAVE_QT_VERSION={version}', f'-DOPENWAVE_PACKAGE_DIR={package}',
            '-P', root / 'cross/qt-scanner-version.cmake')
