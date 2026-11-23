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

    def test_property_bind_uses_isolated_android_copy_never_the_host_file(self):
        # Stage 2B pinned a read-only bind of the manager's file. Stage 7
        # established (waydroid-preinit source and stage5a AVCs) that Android
        # rewrites /vendor/waydroid.prop, and the MS_REMOUNT the old code used
        # targeted the pre-bind inode (EINVAL in stage7a). The objective is
        # unchanged: Android must never modify the manager's file or the host
        # mount it lives on.
        source = SOURCE.read_text()
        bind = source[source.index("static int bind_property(void)"):
                      source.index("static int unbind_property(void)")]
        unbind = source[source.index("static int unbind_property(void)"):
                        source.index("/*\n * Stage 2C:")]
        copy = source[source.index("static int make_property_copy("):
                      source.index("static int is_property_copy(")]
        # The host file is validated and only read; the bind source is a copy
        # on a private tmpfs labeled for Android, never on a host filesystem.
        self.assertIn("make_property_copy(source, &root, &copy)", bind)
        # The detached tmpfs descriptor stays open until the copy is attached.
        self.assertLess(bind.index("bind_fd_onto(copy, target,"), bind.index("close(root)"))
        self.assertIn("*root_out = root;", copy)
        self.assertNotIn("    saved = errno;\n    close(root);\n    if (copy == -1)", copy)
        self.assertIn("bind_fd_onto(copy, target,", bind)
        self.assertNotIn("bind_fd_onto(source", bind)
        self.assertIn('fsopen("tmpfs", FSOPEN_CLOEXEC)', copy)
        self.assertIn('fsconfig(fs, FSCONFIG_SET_STRING, "context", PROP_CONTEXT, 0)', copy)
        self.assertIn('#define PROP_CONTEXT "system_u:object_r:openwave_android_prop_t:s0"', source)
        self.assertIn('"size", PROP_FS_SIZE', copy)
        self.assertIn('"nr_inodes", "4"', copy)
        self.assertIn("MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC", copy)
        self.assertIn("O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644", copy)
        self.assertNotIn("move_mount", copy)  # the tmpfs itself is never attached
        self.assertNotIn("WAYDROID_ROOT", copy)
        self.assertIn("st.st_size > PROP_MAX_BYTES", source)
        self.assertIn("#define PROP_MAX_BYTES 65536", source)
        # Writable for Android, but never setuid, device or executable.
        self.assertIn("MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC", bind)
        self.assertNotIn("MOUNT_ATTR_RDONLY", bind)
        # Unbind only a mount of exactly the helper's copy: a single root-owned
        # file on tmpfs carrying the Android property label.
        self.assertIn("!is_mounted || !is_property_copy(target)", unbind)
        self.assertIn("(unsigned long)fs.f_type != TMPFS_MAGIC", source)
        self.assertIn('getxattr(path, "security.selinux", label', source)
        self.assertIn("unmount_pinned(&target, parent, PROP_DEST)", unbind)

    def test_binds_never_remount_or_touch_the_source_mount(self):
        # No MS_REMOUNT anywhere: a bind remount is checked against, and can
        # change, the source superblock (host root). Binds are detached
        # non-recursive clones configured before they are attached.
        source = SOURCE.read_text()
        self.assertNotIn("MS_REMOUNT", source)
        self.assertNotIn("MS_BIND", source)
        self.assertNotIn("AT_RECURSIVE", source)
        self.assertNotIn("MNT_DETACH", source)
        self.assertIn("open_tree(source, \"\", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH)",
                      source)
        self.assertIn("mount_setattr(tree, \"\", AT_EMPTY_PATH, &attr, sizeof(attr))", source)
        self.assertIn("MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH", source)

    def test_every_unmount_is_pinned_strict_and_never_lazy(self):
        # Stage 2A-2D pinned umount2(<fd of the mount root>, 0). That
        # descriptor holds a reference to the mount, so the kernel refused
        # every helper unmount with EBUSY (stage7g teardown). All unmounts now
        # share one path: validate through the descriptor, record the mount
        # ID, close it, re-open the fixed name beneath the pinned parent,
        # refuse if another mount is on top, unmount without following.
        source = SOURCE.read_text()
        self.assertEqual(source.count("umount2("), 2)  # comment + the one call
        self.assertIn("return umount2(path, UMOUNT_NOFOLLOW);", source)
        pinned = source[source.index("static int unmount_pinned("):
                        source.index("static int mount_image(")]
        self.assertIn("if (now != expected) {", pinned)
        self.assertIn("errno = ESTALE;", pinned)
        self.assertIn("openat(parent, name, O_PATH | O_NOFOLLOW | O_CLOEXEC)", pinned)
        self.assertIn('"/proc/self/fd/%d/%s", parent, name', pinned)
        self.assertIn('!strcmp(name, "/..")', pinned)
        for forbidden in ("MNT_DETACH", "MNT_FORCE", "MNT_EXPIRE"):
            self.assertNotIn(forbidden, source)
        self.assertEqual(source.count("unmount_pinned(&target, parent,"), 4)

    def test_vendor_image_mount_is_explicitly_labeled(self):
        source = SOURCE.read_text()
        self.assertIn('#define VENDOR_CONTEXT_OPTION '
                      '"context=system_u:object_r:openwave_android_system_t:s0"', source)
        self.assertIn("return kind == &VENDOR_IMAGE ? VENDOR_CONTEXT_OPTION : NULL;", source)
        self.assertIn("MS_RDONLY | MS_NOSUID | MS_NODEV, image_mount_data(kind));", source)

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
        self.assertIn("unmount_pinned(&target, parent, kind->image->mountpoint)", source)

    def test_fixed_host_directory_binds_require_confined_guest(self):
        for kind in ("egl32", "egl64", "odm", "vendor-odm"):
            for operation in ("bind", "unbind"):
                with self.subTest(kind=kind, operation=operation):
                    result = self.run_helper(f"--{operation}-{kind}", test=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn("host directory bound readonly", result.stdout)
                    self.assertNotIn("host directory unbound", result.stdout)

    def test_host_directory_binds_refuse_arbitrary_paths(self):
        for args in (("--bind-egl32", "/etc"),
                     ("--bind-egl64", "--rw"),
                     ("--bind-odm", "../../"),
                     ("--bind-vendor-odm", "/"),
                     ("--unbind-egl32", "/etc"),
                     ("--unbind-vendor-odm", "--lazy"),
                     ("--bind-unknown",),
                     ("--unbind-unknown",)):
            with self.subTest(args=args):
                result = self.run_helper(*args, test=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("refused", result.stderr)

    def test_host_directory_binds_have_exact_allowlisted_paths(self):
        source = SOURCE.read_text()
        for expected in (
            '"egl32", "/vendor/lib/egl", VENDOR_MOUNT "/lib/egl"',
            '"egl64", "/vendor/lib64/egl", VENDOR_MOUNT "/lib64/egl"',
            '"odm", "/odm", SYSTEM_MOUNT "/odm_extra"',
            '"vendor-odm", "/vendor/odm", SYSTEM_MOUNT "/odm_extra"',
            "open_fixed_path(path, O_PATH | O_DIRECTORY)",
        ):
            self.assertIn(expected, source)
        self.assertIn("source_st.st_ino != target_st.st_ino", source)

    def test_host_directory_bind_is_readonly_and_cleanup_is_strict(self):
        # Same objective as Stage 2D: host directories appear read-only,
        # nosuid and nodev in Android. The attributes are now set on the
        # detached clone before attachment instead of by a remount afterwards.
        source = SOURCE.read_text()
        host = source[source.index("static int host_dir_bind_op("):
                      source.index("static int dispatch_host_dir_bind(")]
        self.assertIn("bind_fd_onto(source, target, MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID |", host)
        self.assertIn("MOUNT_ATTR_NODEV", host)
        self.assertIn("target_is_separate_mount(target, parent, &separate)", host)
        self.assertIn("unmount_pinned(&target, parent, kind->target)", host)
        self.assertNotIn("MNT_DETACH", source)

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
