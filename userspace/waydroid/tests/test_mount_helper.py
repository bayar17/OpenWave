"""Offline tests for the fixed-op Waydroid mount helper.

No test performs a mount, unmount, loop allocation or privilege escalation.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "waydroid_mount.c"


class MountHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("No host C compiler")
        cls.tmp = tempfile.TemporaryDirectory(prefix="waydroid-mount-qa-")
        cls.work = Path(cls.tmp.name)
        cls.production = cls.work / "production-mount"
        cls.test_binary = cls.work / "qa-mount"
        common = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2"]
        subprocess.run([*common, str(SOURCE), "-o", str(cls.production)], check=True)
        subprocess.run([*common, "-DOPENWAVE_MOUNT_TESTING=1",
                        str(SOURCE), "-o", str(cls.test_binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tmp"):
            cls.tmp.cleanup()

    def run_helper(self, *args, test=True):
        return subprocess.run([str(self.test_binary if test else self.production), *args],
                              capture_output=True, text=True, timeout=5)

    def image(self, kind, valid=True):
        path = self.work / ("fixture-" + kind + ("-valid" if valid else "-invalid"))
        raw = bytearray(4096)
        if valid:
            if kind == "system":
                raw[1024:1028] = bytes.fromhex("e2e1f5e0")
            else:
                raw[1080:1082] = bytes.fromhex("53ef")
        path.write_bytes(raw)
        return path

    def test_approved_image_headers(self):
        for kind in ("system", "vendor"):
            with self.subTest(kind=kind):
                result = self.run_helper("--inspect-image", kind, str(self.image(kind)))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("signature OK", result.stdout)

    def test_mismatched_image_type_rejected(self):
        result = self.run_helper("--inspect-image", "vendor", str(self.image("system")))
        self.assertNotEqual(result.returncode, 0)

    def test_invalid_and_short_images_rejected(self):
        for kind in ("system", "vendor"):
            result = self.run_helper("--inspect-image", kind, str(self.image(kind, False)))
            self.assertNotEqual(result.returncode, 0)
        short = self.work / "short.img"
        short.write_bytes(b"Hello")
        self.assertNotEqual(self.run_helper("--inspect-image", "system", str(short)).returncode, 0)

    def test_symlinked_image_rejected(self):
        link = self.work / "link.img"
        link.symlink_to(self.image("system"))
        self.assertNotEqual(self.run_helper("--inspect-image", "system", str(link)).returncode, 0)

    def test_production_binary_has_no_arbitrary_path_mode(self):
        result = self.run_helper("--inspect-image", "system", str(self.image("system")),
                                 test=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refused", result.stderr)

    def test_mount_commands_require_selinux_domain(self):
        for operation in ("--mount-system", "--mount-vendor"):
            with self.subTest(operation=operation):
                result = self.run_helper(operation, "preinstalled", test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("mounted readonly", result.stdout)
        for operation in ("--unmount-system", "--unmount-vendor"):
            with self.subTest(operation=operation):
                result = self.run_helper(operation, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("unmounted", result.stdout)

    def test_fixed_property_bind_requires_confined_guest(self):
        # Neither operation is allowed on an ordinary host, even as root.
        for operation in ("--bind-prop", "--unbind-prop"):
            with self.subTest(operation=operation):
                result = self.run_helper(operation, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("bound readonly", result.stdout)
                self.assertNotIn("unbound", result.stdout)

    def test_property_bind_rejects_all_caller_supplied_paths(self):
        for args in (("--bind-prop", "/etc/passwd"),
                     ("--unbind-prop", "/"),
                     ("--bind-prop", "preinstalled"),
                     ("--bind-prop", "--force")):
            with self.subTest(args=args):
                result = self.run_helper(*args, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refused", result.stderr)

    def test_property_binding_requires_readonly_remount_in_source(self):
        source = SOURCE.read_text()
        self.assertIn("MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV", source)
        self.assertIn("source_st.st_ino != target_st.st_ino", source)
        self.assertIn("st.st_size > 65536", source)

    def test_fixed_overlay_commands_reject_unconfined_execution(self):
        for operation in ("--overlay-system", "--overlay-vendor",
                          "--unoverlay-vendor", "--unoverlay-system"):
            with self.subTest(operation=operation):
                result = self.run_helper(operation, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("overlay mounted readonly", result.stdout)
                self.assertNotIn("overlay unmounted", result.stdout)

    def test_overlay_commands_reject_all_path_arguments(self):
        for args in (("--overlay-system", "/etc"),
                     ("--overlay-vendor", "/var/lib/waydroid"),
                     ("--unoverlay-system", "/"),
                     ("--unoverlay-vendor", "/proc"),
                     ("--overlay-system", "writable")):
            with self.subTest(args=args):
                result = self.run_helper(*args, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refused", result.stderr)

    def test_overlay_uses_fixed_readonly_and_fd_bound_paths(self):
        source = SOURCE.read_text()
        self.assertIn("OVERLAY_ROOT WAYDROID_ROOT", source)
        self.assertIn("OVERLAY_RW WAYDROID_ROOT", source)
        self.assertIn("OVERLAY_WORK WAYDROID_ROOT", source)
        self.assertIn("lowerdir=%s:%s,upperdir=%s,workdir=%s,xino=off", source)
        self.assertIn("MS_RDONLY | MS_NOSUID | MS_NODEV, options", source)
        self.assertIn("us.st_dev != ws.st_dev", source)
        self.assertIn("OVERLAYFS_SUPER_MAGIC", source)
        self.assertIn("target_id != child_id", source)

    def test_overlay_parent_unmount_checks_nested_children(self):
        source = SOURCE.read_text()
        self.assertIn("kind == &VENDOR_OVERLAY ? PROP_DEST : VENDOR_MOUNT", source)
        self.assertIn("detach nested vendor/property mount before parent overlay", source)
        self.assertIn("umount2(dest, 0)", source)

    def test_generic_mount_cli_is_rejected(self):
        for args in (("-o", "rw", "/dev/sda", "/"),
                     ("--mount-system", "../../etc"),
                     ("--mount-vendor", "garbage"),
                     ("--unmount-vendor", "/"),
                     ("--verify-system", "/tmp")):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_helper(*args, test=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
