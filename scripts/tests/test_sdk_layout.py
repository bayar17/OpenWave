"""Migration checks: preserve installed tools and reject destructive collisions."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from openwave_sdk_layout import merge_existing


class SharedPrefixTests(unittest.TestCase):
    def fixture(self, sdk):
        for name in ('bin', 'lib', 'include', 'share', 'libexec'):
            (sdk / 'clang' / name).mkdir(parents=True)
            (sdk / name).symlink_to('clang/' + name)
        files = {
            'clang/bin/clang': 'modified compiler',
            'clang/lib/libLLVM.so': 'modified compiler library',
            'clang/libexec/ccc-analyzer': 'analyzer',
            'qt/host/bin/qmake6': 'host qmake',
            'qt/host/libexec/moc': 'host moc',
            'qt/host/include/QtCore/qobject.h': 'host header',
            'qt/target/x86_64-unknown-openwave/lib/libQt6Core.so': 'target Qt',
            'openwave-api/include/OpenWave/Client.h': 'public client',
            'openwave-api/lib/x86_64-unknown-openwave/libopenwave-prompts.so': 'client library',
            'openwave-api/cmake/old.cmake': 'old export',
            'host-tools/usr/bin/patchelf': 'host packager',
        }
        for name, contents in files.items():
            file = sdk / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(contents)
        target = sdk / 'sysroot/x86_64-unknown-openwave/usr/lib'
        target.mkdir(parents=True)
        original = sdk / 'qt/target/x86_64-unknown-openwave/lib/libQt6Core.so'
        (target / original.name).symlink_to(original)

    def test_tools_and_clients_preserved_in_shared_prefix(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            self.fixture(sdk)
            inode = (sdk / 'clang/bin/clang').stat().st_ino
            self.assertTrue(merge_existing(sdk))
            self.assertEqual((sdk / 'bin/clang').stat().st_ino, inode)
            self.assertEqual((sdk / 'bin/moc').read_text(), 'host moc')
            self.assertEqual((sdk / 'bin/qmake-host6').read_text(), 'host qmake')
            self.assertEqual((sdk / 'bin/ccc-analyzer').read_text(), 'analyzer')
            self.assertEqual((sdk / 'bin/patchelf').read_text(), 'host packager')
            self.assertEqual((sdk / 'include/OpenWave/Client.h').read_text(), 'public client')
            self.assertEqual((sdk / 'sysroot/x86_64-unknown-openwave/usr/lib64/libopenwave-prompts.so').read_text(), 'client library')
            qt = sdk / 'sysroot/x86_64-unknown-openwave/usr/lib/libQt6Core.so'
            self.assertFalse(qt.is_symlink())
            self.assertEqual(qt.read_text(), 'target Qt')
            self.assertTrue((sdk / 'libexec/moc').exists())
            for name in ('clang', 'qt', 'openwave-api', 'host-tools'):
                self.assertFalse((sdk / name).exists())
            self.assertFalse(merge_existing(sdk))

    def test_collision_rejected_before_moving_any_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            self.fixture(sdk)
            collision = sdk / 'qt/host/bin/clang'
            collision.write_text('different compiler')
            with self.assertRaises(RuntimeError):
                merge_existing(sdk)
            self.assertEqual((sdk / 'clang/bin/clang').read_text(), 'modified compiler')
            self.assertEqual(collision.read_text(), 'different compiler')
            self.assertTrue((sdk / 'bin').is_symlink())


if __name__ == '__main__':
    unittest.main()
