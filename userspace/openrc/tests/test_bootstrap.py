"""Compile and exercise the production bootstrap sequence with host syscall shims."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class BootstrapRegressionTests(unittest.TestCase):
    def test_mount_order_fail_closed_dirs_and_exec_handoff(self):
        with tempfile.TemporaryDirectory(prefix="openwave-bootstrap-") as temp:
            binary = Path(temp) / "bootstrap-sequence"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DOPENWAVE_BOOTSTRAP_NO_MAIN", "-I", str(ROOT / "userspace/openrc"),
                str(ROOT / "userspace/openrc/bootstrap.c"),
                str(ROOT / "userspace/openrc/tests/bootstrap_sequence.c"),
                "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("bootstrap regression: PASS", result.stdout)

    def test_selinux_loader_requires_verified_selinuxfs_and_enforcing_state(self):
        source = (ROOT / "userspace/security/selinux/OpenWaveSELinux.c").read_text()
        bootstrap = (ROOT / "userspace/openrc/bootstrap.c").read_text()
        self.assertIn('"/sys/fs/selinux"', source)
        self.assertIn("SELINUX_MAGIC", source)
        self.assertIn("security_setenforce(mode)", source)
        self.assertIn("actual_mode = security_getenforce()", source)
        self.assertIn("actual_mode != mode", source)
        self.assertNotIn('mount("securityfs"', source)
        self.assertIn('"selinuxfs", "/sys/fs/selinux", "selinuxfs"', bootstrap)


if __name__ == "__main__":
    unittest.main()
