"""Verify the real pinned package and rejection of implementation tampering."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'userspace/waydroid'))
from prepare_apparmor_free import prepare
from verify_upstream_package import verify


class PackageTests(unittest.TestCase):
    def test_pristine_package_and_tampered_python(self):
        with tempfile.TemporaryDirectory() as tmp:
            package = Path(tmp)
            subprocess.run(['make', '-s', '-C', str(ROOT / 'third_party/waydroid'),
                            'install', 'DESTDIR=' + str(package), 'USE_SYSTEMD=0',
                            'USE_DBUS_ACTIVATION=0', 'USE_NFTABLES=0'],
                           check=True, stdout=subprocess.DEVNULL)
            prepare(package)
            record = verify(ROOT / 'third_party/waydroid', package, True)
            self.assertGreater(record['verified_files'], 50)
            prepare(package)  # Idempotent, limited template transformation.
            self.assertEqual(verify(ROOT / 'third_party/waydroid', package, True), record)
            shadow = package / 'usr/lib/waydroid/logging.py'
            shadow.write_text('# unexpected module can shadow the standard library\n')
            with self.assertRaisesRegex(RuntimeError, 'Unexpected'):
                verify(ROOT / 'third_party/waydroid', package, True)
            shadow.unlink()
            implementation = package / 'usr/lib/waydroid/tools/actions/container_manager.py'
            original = implementation.read_bytes()
            implementation.write_bytes(original + b'\n# changed\n')
            with self.assertRaisesRegex(RuntimeError, 'mismatch'):
                verify(ROOT / 'third_party/waydroid', package, True)
            implementation.write_bytes(original)
            config = package / 'usr/lib/waydroid/data/configs/config_3'
            config.write_bytes(config.read_bytes() + b'\nlxc.cap.drop =\n')
            with self.assertRaisesRegex(RuntimeError, 'mismatch'):
                verify(ROOT / 'third_party/waydroid', package, True)
