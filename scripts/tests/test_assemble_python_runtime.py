"""Keep explicitly supported runtime entry points in the assembled image."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = spec_from_file_location('assemble_openwave_rootfs', ROOT / 'scripts/assemble-openwave-rootfs.py')
ASSEMBLER = module_from_spec(SPEC)
sys.modules[SPEC.name] = ASSEMBLER
SPEC.loader.exec_module(ASSEMBLER)
SELINUX_SPEC = spec_from_file_location('build_openwave_selinux', ROOT / 'scripts/build-openwave-selinux.py')
SELINUX_BUILDER = module_from_spec(SELINUX_SPEC)
sys.modules[SELINUX_SPEC.name] = SELINUX_BUILDER
SELINUX_SPEC.loader.exec_module(SELINUX_BUILDER)


class PythonRuntimeAssemblyTests(unittest.TestCase):
    def test_waydroid_python_launcher_repairs_stale_sdk_link(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            (tree / 'usr/bin').mkdir(parents=True)
            (tree / 'usr/bin/waydroid').write_text('#!/usr/bin/env python3\n')
            executable = tree / 'usr/bin/python3.14'
            executable.write_bytes(b'\x7fELF' + bytes(60))
            executable.chmod(0o755)
            (tree / 'bin').symlink_to('usr/bin')
            (tree / 'usr/bin/busybox').write_bytes(b'\x7fELF' + bytes(60))
            standard = tree / 'usr/lib64/python3.14/encodings'
            standard.mkdir(parents=True)
            (standard / '__init__.py').write_text('')
            (tree / 'usr/bin/python3').symlink_to('python3.12')
            ASSEMBLER.normalize_python_launcher(tree)
            self.assertEqual((tree / 'usr/bin/python3').readlink(), Path('python3.14'))
            self.assertEqual((tree / 'usr/bin/env').readlink(), Path('busybox'))
            ASSEMBLER.normalize_python_launcher(tree)
            executable.unlink()
            with self.assertRaisesRegex(RuntimeError, 'executable'):
                ASSEMBLER.normalize_python_launcher(tree)

    def test_trusted_appd_metadata_modes_are_independent_of_host_umask(self):
        observed = []
        original_umask = os.umask(0)
        os.umask(original_umask)
        try:
            for mask in (0o022, 0o002):
                with tempfile.TemporaryDirectory(prefix='openwave-trusted-config-') as tmp:
                    tree = Path(tmp) / 'rootfs'
                    os.umask(mask)
                    for relative in (
                        'etc/openwave', 'etc/dbus-1', 'etc/selinux/openwave/policy',
                        'etc/selinux/openwave/contexts/files',
                    ):
                        (tree / relative).mkdir(parents=True, exist_ok=True)
                    for relative in (
                        'etc/openwave/appd.conf', 'etc/dbus-1/system.conf',
                        'etc/selinux/config', 'etc/selinux/openwave/policy/policy.35',
                        'etc/selinux/openwave/contexts/files/file_contexts',
                    ):
                        (tree / relative).write_text('test config\n')
                    for relative in (
                        'etc', 'etc/openwave', 'etc/dbus-1', 'etc/selinux',
                        'etc/selinux/openwave', 'etc/selinux/openwave/policy',
                        'etc/selinux/openwave/contexts', 'etc/selinux/openwave/contexts/files',
                    ):
                        (tree / relative).chmod(0o775)
                    for relative in (
                        'etc/openwave/appd.conf', 'etc/dbus-1/system.conf',
                        'etc/selinux/config', 'etc/selinux/openwave/policy/policy.35',
                        'etc/selinux/openwave/contexts/files/file_contexts',
                    ):
                        (tree / relative).chmod(0o666)
                    ASSEMBLER.normalize_trusted_metadata(tree)
                    observed.append(tuple((tree / relative).stat().st_mode & 0o7777
                                          for relative in (
                                              'etc', 'etc/openwave', 'etc/dbus-1',
                                              'etc/selinux', 'etc/selinux/openwave',
                                              'etc/selinux/openwave/policy',
                                              'etc/selinux/openwave/contexts',
                                              'etc/selinux/openwave/contexts/files',
                                              'etc/openwave/appd.conf', 'etc/dbus-1/system.conf',
                                              'etc/selinux/config',
                                              'etc/selinux/openwave/policy/policy.35',
                                              'etc/selinux/openwave/contexts/files/file_contexts',
                                          )))
        finally:
            os.umask(original_umask)
        self.assertEqual(observed, [(
            0o755, 0o755, 0o755, 0o755, 0o755, 0o755, 0o755, 0o755,
            0o644, 0o644, 0o644, 0o644, 0o644,
        )] * 2)

    def test_trusted_appd_metadata_rejects_symlinked_components(self):
        with tempfile.TemporaryDirectory(prefix='openwave-trusted-config-link-') as tmp:
            tree = Path(tmp) / 'rootfs'
            (tree / 'etc').mkdir(parents=True)
            outside = Path(tmp) / 'outside'
            outside.mkdir()
            (tree / 'etc/openwave').symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(RuntimeError, 'Symlink'):
                ASSEMBLER.normalize_trusted_metadata(tree)

    def test_selinux_overlay_etc_mode_is_independent_of_host_umask(self):
        original_umask = os.umask(0)
        os.umask(original_umask)
        try:
            for mask in (0o022, 0o002):
                with tempfile.TemporaryDirectory(prefix='openwave-policy-stage-') as tmp:
                    os.umask(mask)
                    etc = SELINUX_BUILDER.prepare_stage_etc(Path(tmp) / 'stage')
                    for relative in (
                        'etc', 'etc/selinux', 'etc/selinux/openwave',
                        'etc/selinux/openwave/policy', 'etc/selinux/openwave/contexts',
                        'etc/selinux/openwave/contexts/files',
                    ):
                        self.assertEqual((Path(tmp) / 'stage' / relative).stat().st_mode & 0o7777, 0o755)
        finally:
            os.umask(original_umask)

    def test_python_launchers_are_runtime_files(self):
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/bin/python3')))
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/bin/python3.14')))
        self.assertFalse(ASSEMBLER.runtime_path(Path('usr/bin/python3.14-config')))

    def test_standard_library_and_extension_modules_are_runtime_files(self):
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/lib/python3.14/json/__init__.py')))
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/lib64/python3.14/lib-dynload/_ssl.cpython-314-x86_64-linux-gnu.so')))
        self.assertFalse(ASSEMBLER.runtime_path(Path('usr/include/python3.14/Python.h')))

    def test_lxc_tools_and_container_init_are_runtime_files(self):
        for name in ('lxc-start', 'lxc-stop', 'lxc-info', 'lxc-attach',
                     'lxc-checkconfig', 'lxc-create', 'lxc-execute'):
            with self.subTest(name=name):
                self.assertTrue(ASSEMBLER.runtime_path(Path('usr/bin') / name))
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/sbin/init.lxc')))
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/share/lxc/config/default.conf')))

    def test_upstream_waydroid_and_network_tools_are_runtime_files(self):
        self.assertTrue(ASSEMBLER.runtime_path(Path('usr/bin/waydroid')))
        for name in ('dnsmasq', 'iptables-apply', 'xtables-legacy-multi',
                     'iptables', 'iptables-legacy', 'iptables-restore',
                     'iptables-save', 'ip6tables', 'ip6tables-legacy',
                     'ip6tables-restore', 'ip6tables-save'):
            with self.subTest(name=name):
                self.assertTrue(ASSEMBLER.runtime_path(Path('usr/sbin') / name))
        self.assertTrue(ASSEMBLER.runtime_path(Path('sbin/ip')))
        self.assertFalse(ASSEMBLER.runtime_path(Path(
            'usr/share/dbus-1/system-services/id.waydro.Container.service')))
        self.assertTrue(ASSEMBLER.runtime_path(Path(
            'usr/share/icons/hicolor/512x512/apps/waydroid.png')))
        self.assertFalse(ASSEMBLER.runtime_path(Path(
            'usr/lib/waydroid/tools/services/openwave_android_provider.py')))
        self.assertFalse(ASSEMBLER.runtime_path(Path(
            'usr/lib/waydroid/tools/services/__pycache__/user_manager.cpython-314.pyc')))

    def test_active_selinux_policy_comes_from_the_rootfs_base(self):
        policy = Path('etc/selinux/openwave/policy/policy.35')
        config = Path('etc/selinux/config')
        self.assertTrue(ASSEMBLER.source_file_allowed(0, policy))
        self.assertTrue(ASSEMBLER.source_file_allowed(0, config))
        self.assertFalse(ASSEMBLER.source_file_allowed(1, policy))
        self.assertFalse(ASSEMBLER.source_file_allowed(2, config))
        self.assertTrue(ASSEMBLER.source_file_allowed(2, Path('usr/lib64/libselinux.so.1')))


if __name__ == '__main__':
    unittest.main()
