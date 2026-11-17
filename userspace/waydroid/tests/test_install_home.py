"""TINH's privileged overlay install: exact files, idempotent, no symlinks."""
from pathlib import Path
import importlib.util
import os
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'install_home.py'
spec = importlib.util.spec_from_file_location('install_home', SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InstallHomeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.overlay = self.root / 'overlay'
        self.overlay.mkdir(mode=0o755)
        self.apk = self.root / 'tinh.apk'
        self.apk.write_bytes(b'PK\x03\x04 apk')

    def tearDown(self):
        self.temporary.cleanup()

    def test_installs_exactly_the_apk_and_its_permission_file(self):
        self.assertTrue(module.install(str(self.overlay), str(self.apk)))
        files = sorted(str(p.relative_to(self.overlay)) for p in self.overlay.rglob('*') if p.is_file())
        self.assertEqual(files, ['system/etc/permissions/privapp-permissions-com.openwave.tinh.xml',
                                 'system/priv-app/TINH/TINH.apk'])
        self.assertEqual((self.overlay / 'system/priv-app/TINH/TINH.apk').read_bytes(), b'PK\x03\x04 apk')
        for path in self.overlay.rglob('*'):
            self.assertEqual(path.stat().st_mode & 0o777, 0o755 if path.is_dir() else 0o644, path)
        self.assertFalse(module.install(str(self.overlay), str(self.apk)))   # idempotent
        self.apk.write_bytes(b'PK\x03\x04 newer')
        self.assertTrue(module.install(str(self.overlay), str(self.apk)))    # updated in place

    def test_only_the_three_needed_permissions_are_granted(self):
        import re
        xml = module.PERMISSIONS_XML.decode()
        self.assertEqual(sorted(re.findall(r'<permission name="([^"]+)"/>', xml)),
                         ['android.permission.FORCE_STOP_PACKAGES', 'android.permission.REAL_GET_TASKS',
                          'android.permission.WRITE_SECURE_SETTINGS'])
        self.assertEqual(xml.count('package='), 1)
        self.assertIn('package="com.openwave.tinh"', xml)

    def test_symlinked_overlay_directory_is_refused(self):
        elsewhere = self.root / 'elsewhere'
        elsewhere.mkdir()
        os.symlink(elsewhere, self.overlay / 'system')
        with self.assertRaises(OSError):
            module.install(str(self.overlay), str(self.apk))
        self.assertEqual(list(elsewhere.iterdir()), [])

    def test_symlinked_target_file_is_replaced_not_followed(self):
        victim = self.root / 'victim'
        victim.write_bytes(b'keep')
        target = self.overlay / 'system/priv-app/TINH'
        target.mkdir(parents=True, mode=0o755)
        os.symlink(victim, target / 'TINH.apk')
        with self.assertRaises(OSError):
            module.install(str(self.overlay), str(self.apk))
        self.assertEqual(victim.read_bytes(), b'keep')

    def test_writable_overlay_directory_is_refused(self):
        (self.overlay / 'system').mkdir(mode=0o777)
        os.chmod(self.overlay / 'system', 0o777)
        with self.assertRaises(PermissionError):
            module.install(str(self.overlay), str(self.apk))

    def test_non_apk_is_refused(self):
        self.apk.write_bytes(b'not an apk')
        with self.assertRaises(ValueError):
            module.install(str(self.overlay), str(self.apk))


if __name__ == '__main__':
    unittest.main()
