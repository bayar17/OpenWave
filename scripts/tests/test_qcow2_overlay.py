#!/usr/bin/env python3
"""Host-only QEMU block-layer test; never starts a guest."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class Qcow2OverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("qemu-img") or not shutil.which("qemu-io"):
            raise unittest.SkipTest("qemu-img and qemu-io are required")

    @staticmethod
    def sha256(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_overlay_uses_explicit_raw_backing_and_isolates_writes(self):
        with tempfile.TemporaryDirectory(prefix="openwave-qcow2-host-test-") as temporary:
            directory = Path(temporary)
            base = directory / "immutable-base.raw"
            overlay = directory / "disposable-overlay.qcow2"
            subprocess.run(["qemu-img", "create", "-q", "-f", "raw", str(base), "1M"],
                           check=True)
            subprocess.run(["qemu-io", "-f", "raw", "-c", "write -P 0x11 0 4096", str(base)],
                           check=True, capture_output=True, text=True)
            base_hash = self.sha256(base)
            subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "raw",
                            "-b", str(base), str(overlay)], check=True)

            chain = json.loads(subprocess.check_output(
                ["qemu-img", "info", "--output=json", "--backing-chain", str(overlay)],
                text=True))
            self.assertEqual(len(chain), 2)
            self.assertEqual(chain[0]["format"], "qcow2")
            self.assertEqual(chain[0]["backing-filename-format"], "raw")
            self.assertEqual(Path(chain[0]["full-backing-filename"]).resolve(), base.resolve())
            self.assertEqual(chain[1]["format"], "raw")

            subprocess.run(["qemu-io", "-f", "qcow2", "-c", "write -P 0x22 0 4096",
                            str(overlay)], check=True, capture_output=True, text=True)
            subprocess.run(["qemu-io", "-f", "qcow2", "-c", "read -P 0x22 0 4096",
                            str(overlay)], check=True, capture_output=True, text=True)
            subprocess.run(["qemu-io", "-f", "raw", "-c", "read -P 0x11 0 4096", str(base)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(self.sha256(base), base_hash)

            check = subprocess.run(["qemu-img", "check", str(overlay)],
                                   capture_output=True, text=True)
            self.assertEqual(check.returncode, 0, check.stderr or check.stdout)


if __name__ == "__main__":
    unittest.main()
