"""Recovery safeguards for missing SDKs and host/target Qt separation."""
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from openwave_sdk_bootstrap import (copy_installation, export_host_qt,
                                   export_runtimes, native_environment, qt_version)


class RecoveryTests(unittest.TestCase):
    def test_reexport_updates_files_and_preserves_installed_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, sdk = root / 'source', root / 'sdk'
            (source / 'lib').mkdir(parents=True)
            (source / 'lib/library.so.1').write_text('original')
            (source / 'lib/library.so').symlink_to('library.so.1')
            (source / 'lib64').symlink_to('lib')
            (source / 'missing-manual').symlink_to('manual.1')
            copy_installation(source, sdk)
            (sdk / 'public-api').write_text('retain installed API')
            (source / 'lib/library.so.1').write_text('updated')
            copy_installation(source, sdk)
            self.assertEqual((sdk / 'lib/library.so').read_text(), 'updated')
            self.assertEqual((sdk / 'lib64').readlink(), Path('lib'))
            self.assertEqual((sdk / 'missing-manual').readlink(), Path('manual.1'))
            self.assertEqual((sdk / 'public-api').read_text(), 'retain installed API')

    def host(self, prefix, version='6.11.2'):
        metadata = prefix / 'lib/cmake/Qt6'
        metadata.mkdir(parents=True)
        (metadata / 'Qt6ConfigVersion.cmake').write_text('include(Qt6ConfigVersionImpl.cmake)')
        (metadata / 'Qt6ConfigVersionImpl.cmake').write_text(f'set(PACKAGE_VERSION "{version}")')
        (prefix / 'bin').mkdir()
        for name in ('qmake6', 'qtpaths6'):
            (prefix / 'bin' / name).write_bytes(b'\x7fELF native tool')
        (prefix / 'bin/qmake').symlink_to('qmake6')
        (prefix / 'bin/qtpaths').symlink_to('qtpaths6')
        (prefix / 'libexec').mkdir()
        for name in ('moc', 'rcc', 'qsb', 'qmlcachegen'):
            (prefix / 'libexec' / name).write_bytes(b'\x7fELF native tool')

    def test_fresh_import_keeps_all_tools_in_bin_and_excludes_foreign_sysroot(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, sdk = root / 'native', root / 'toolchain'
            self.host(source)
            (source / 'sysroot').mkdir()
            (source / 'sysroot/foreign-target-library').write_text('must not be imported')
            sdk.mkdir()
            export_host_qt(sdk, root, '6.11.2', root / 'build', root / 'qt', 1, source)
            self.assertEqual(qt_version(sdk), '6.11.2')
            self.assertEqual((sdk / 'bin/qmake-host').resolve(), sdk / 'bin/qmake-host6')
            self.assertEqual((sdk / 'bin/qtpaths-host').resolve(), sdk / 'bin/qtpaths-host6')
            self.assertEqual((sdk / 'bin/moc').read_bytes(), b'\x7fELF native tool')
            self.assertTrue((sdk / 'libexec').is_symlink())
            self.assertFalse((sdk / 'sysroot').exists())

    def test_older_host_qt_rejected_before_any_sdk_files_are_copied(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.host(root / 'native', '6.10.2')
            sdk = root / 'toolchain'
            sdk.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'must match target Qt 6.11.2'):
                export_host_qt(sdk, root, '6.11.2', root / 'build', root / 'qt', 1, root / 'native')
            self.assertEqual(list(sdk.iterdir()), [])

    def test_host_recovery_does_not_inherit_target_build_environment(self):
        with patch.dict(os.environ, {'CC': 'target-clang', 'CXXFLAGS': '--sysroot=/target',
                                     'CMAKE_TOOLCHAIN_FILE': 'target.cmake',
                                     'PKG_CONFIG_SYSROOT_DIR': '/target',
                                     'PATH': '/tmp/sdk/bin:/usr/bin'}):
            env = native_environment(Path('/tmp/sdk'))
            self.assertEqual(env['PATH'], '/usr/bin')
            for name in ('CC', 'CXXFLAGS', 'CMAKE_TOOLCHAIN_FILE', 'PKG_CONFIG_SYSROOT_DIR'):
                self.assertNotIn(name, env)

    def test_openwave_startup_objects_and_builtins_survive_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            triple = 'x86_64-unknown-openwave'
            resource = root / 'toolchain/lib/clang/23'
            built = root / 'llvm-build/lib/clang/23/lib' / triple
            built.mkdir(parents=True)
            names = ('clang_rt.crtbegin.o', 'clang_rt.crtend.o', 'libclang_rt.builtins.a')
            for name in names:
                (built / name).write_bytes(b'original target runtime')
            with patch('openwave_sdk_bootstrap.subprocess.check_output', return_value=str(resource)):
                export_runtimes(root / 'toolchain', root, root / 'llvm-build', triple)
            for name in names:
                self.assertEqual((resource / 'lib' / triple / name).read_bytes(), b'original target runtime')


if __name__ == '__main__':
    unittest.main()
