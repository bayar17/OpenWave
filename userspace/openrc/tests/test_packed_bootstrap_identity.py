#!/usr/bin/env python3
"""Exercise packed /init comparison against a disposable ext4 fixture."""

from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
VERIFIER = ROOT / "userspace/openrc/verify-packed-bootstrap.py"


@unittest.skipUnless(shutil.which("debugfs") and shutil.which("mke2fs"),
                     "ext4 tools are required for the packed-init fixture")
class PackedBootstrapIdentityTests(unittest.TestCase):
    def test_exact_match_is_recorded_and_mismatch_rejected(self):
        with tempfile.TemporaryDirectory(prefix="openwave-init-identity-") as temporary:
            root = Path(temporary)
            built = root / "built-init"
            image = root / "fixture.img"
            record = root / "identity.json"
            built.write_bytes(b"fresh bootstrap fixture\n")
            with image.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            subprocess.run(["mke2fs", "-q", "-t", "ext4", "-F", str(image)], check=True)
            subprocess.run(["debugfs", "-w", "-R", f"write {built} /init", str(image)],
                           check=True, capture_output=True, text=True)

            match = subprocess.run(["python3", str(VERIFIER), "--built", str(built),
                                    "--image", str(image), "--record", str(record)],
                                   check=True, capture_output=True, text=True)
            self.assertIn("packed /init identity PASS", match.stdout)
            result = json.loads(record.read_text())
            self.assertTrue(result["match"])
            self.assertEqual(result["built_sha256"], result["packed_sha256"])

            built.write_bytes(b"a different bootstrap\n")
            mismatch = subprocess.run(["python3", str(VERIFIER), "--built", str(built),
                                       "--image", str(image)], capture_output=True, text=True)
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn("does not match", mismatch.stderr)


if __name__ == "__main__":
    unittest.main()
