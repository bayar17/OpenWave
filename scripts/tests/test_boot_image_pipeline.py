"""Exercise ext4 labels and guest symlinks using actual small filesystem images."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('boot', ROOT / 'scripts/prepare-openwave-boot.py')
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)
policy_spec = importlib.util.spec_from_file_location('policy', ROOT / 'scripts/build-openwave-selinux.py')
policy_builder = importlib.util.module_from_spec(policy_spec)
policy_spec.loader.exec_module(policy_builder)


class ImageTests(unittest.TestCase):
    def test_image_ownership_preserves_explicit_service_owner(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            root = temp / 'root'; root.mkdir()
            (root / 'etc').mkdir()
            (root / 'etc/passwd').write_text('root:x:0:0::/:/bin/sh\nocean:x:1000:1000::/Data/Users/ocean:/bin/sh\n')
            (root / 'etc/passwd').chmod(0o644)
            (root / 'Data/Users/ocean').mkdir(parents=True)
            (root / 'service-data').write_text('owned by service')
            (root / 'system-file').write_text('system')
            boot.prepare_layout(root)
            image = temp / 'owners.img'
            env = os.environ.copy()
            env.update(OPENWAVE_IMAGE_BUILDER_UID=str(os.getuid()),
                       OPENWAVE_IMAGE_BUILDER_GID=str(os.getgid()))
            subprocess.run(['truncate', '-s', '16M', str(image)], check=True)
            subprocess.run(['fakeroot', '--', 'bash', '-ec',
                'chown 42:43 "$1/service-data"\npython3 "$3" "$1"\nmke2fs -q -t ext4 -d "$1" "$2"',
                '_', str(root), str(image), str(ROOT / 'scripts/image-owners.py')],
                env=env, check=True, capture_output=True)
            for path, uid, gid in (('/system-file', 0, 0), ('/service-data', 42, 43),
                                   ('/Data/Users/ocean', 1000, 1000)):
                result = subprocess.run(['debugfs', '-R', 'stat ' + path, str(image)],
                                        capture_output=True, text=True, check=True)
                self.assertRegex(result.stdout, rf'User:\s+{uid}\s+Group:\s+{gid}\s')
            self.assertEqual((root / 'service-data').stat().st_uid, os.getuid())

    def test_account_migration_is_idempotent_and_preserves_database(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'etc').mkdir()
            passwd = root / 'etc/passwd'
            content = b'root:x:0:0::/:/bin/sh\nocean:x:1000:1000::/Data/Users/ocean:/bin/sh\n'
            passwd.write_bytes(content); passwd.chmod(0o644)
            boot.prepare_layout(root)
            backing = root / boot.ACCOUNT_DIRECTORY / 'passwd'
            self.assertEqual(os.readlink(passwd), boot.PASSWD_LINK)
            self.assertEqual(passwd.read_bytes(), content)
            self.assertEqual(backing.stat().st_mode & 0o7777, 0o644)
            inode = backing.stat().st_ino
            boot.prepare_layout(root)
            self.assertEqual(backing.stat().st_ino, inode)
            self.assertEqual(backing.read_bytes(), content)

    def test_account_migration_rejects_conflicts_and_unsafe_links_without_loss(self):
        for scenario in ('foreign_link', 'dangling', 'backing_link', 'conflict', 'writable_directory'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / 'etc').mkdir()
                passwd = root / 'etc/passwd'
                passwd.write_text('root:x:0:0::/:/bin/sh\n'); passwd.chmod(0o644)
                backing = root / boot.ACCOUNT_DIRECTORY / 'passwd'
                backing.parent.mkdir(parents=True)
                backing.parent.chmod(0o755)
                external = root / 'preserve'; external.write_text('sentinel')
                if scenario in ('foreign_link', 'dangling'):
                    passwd.unlink()
                    passwd.symlink_to(str(external) if scenario == 'foreign_link' else boot.PASSWD_LINK)
                elif scenario == 'backing_link':
                    backing.symlink_to(external)
                elif scenario == 'conflict':
                    backing.write_text('different:x:42:43::/:/bin/false\n'); backing.chmod(0o644)
                else:
                    backing.parent.chmod(0o777)
                previous = os.readlink(passwd) if passwd.is_symlink() else passwd.read_bytes()
                with self.assertRaises(RuntimeError):
                    boot.prepare_layout(root)
                self.assertEqual(os.readlink(passwd) if passwd.is_symlink() else passwd.read_bytes(), previous)
                self.assertEqual(external.read_text(), 'sentinel')
                self.assertFalse((root / 'var/log').exists())

    def test_layout_rejects_host_parent_symlink_before_mutation(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            root = temp / 'root'; root.mkdir()
            external = temp / 'external'; external.mkdir()
            (external / 'sentinel').write_text('preserve')
            (root / 'etc').symlink_to(external)
            with self.assertRaisesRegex(RuntimeError, 'directory symlink'):
                boot.prepare_layout(root)
            self.assertFalse((external / 'mtab').is_symlink())
            self.assertEqual(sorted(p.name for p in external.iterdir()), ['sentinel'])
            self.assertFalse((root / 'var').exists())

    def test_layout_rejects_symlinked_runtime_root(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            external = temp / 'external'; external.mkdir()
            root = temp / 'root'; root.symlink_to(external)
            with self.assertRaisesRegex(RuntimeError, 'symlinked runtime root'):
                boot.prepare_layout(root)
            self.assertEqual(list(external.iterdir()), [])

    def test_guest_absolute_links_and_cycle(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'target').write_text('guest')
            (root / 'link').symlink_to('/target')
            self.assertEqual(boot.guest_path(root, '/link').read_text(), 'guest')
            (root / 'cycle').symlink_to('/cycle')
            with self.assertRaisesRegex(RuntimeError, 'loop'):
                boot.guest_path(root, '/cycle')

    def test_label_roundtrip_and_stale_policy_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            root = temp / 'root'; root.mkdir()
            policy = root / 'etc/selinux/openwave/policy/policy.35'
            policy.parent.mkdir(parents=True); policy.write_bytes(b'policy fixture')
            contexts = root / 'etc/selinux/openwave/contexts/files/file_contexts'
            contexts.parent.mkdir(parents=True)
            contexts.write_text('/.* system_u:object_r:openwave_runtime_t:s0\n')
            (root / 'init').write_text('init')
            link = root / 'etc/runlevels/default/appd'
            link.parent.mkdir(parents=True); link.symlink_to('/etc/init.d/appd')
            service = root / 'etc/init.d/appd'; service.parent.mkdir()
            service.write_text('service')
            image = temp / 'raw.img'
            subprocess.run(['truncate', '-s', '16M', str(image)], check=True)
            subprocess.run(['mke2fs', '-q', '-t', 'ext4', '-d', str(root), str(image)], check=True)
            args = ['python3', str(ROOT / 'scripts/openwave-label-ext4.py'),
                    '--rootfs', str(root), '--image', str(image), '--output', str(temp / 'labeled.img')]
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for suffix in ('local', 'homedirs', 'subs', 'subs_dist', 'bin'):
                companion = contexts.with_name('file_contexts.' + suffix)
                companion.write_text('/init system_u:object_r:unexpected_t:s0\n')
                args[-1] = str(temp / ('sidecar-' + suffix + '.img'))
                result = subprocess.run(args, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('companions', result.stderr)
                self.assertFalse(Path(args[-1]).exists())
                with self.assertRaisesRegex(SystemExit, 'companions'):
                    policy_builder.prepare_stage_etc(root)
                with self.assertRaisesRegex(SystemExit, 'companions'):
                    policy_builder.verify(root, 'x86_64-unknown-openwave')
                companion.unlink()
            args[-1] = str(temp / 'labeled.img')
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('overwrite', result.stderr)
            policy.write_bytes(b'new policy')
            args[-1] = str(temp / 'stale.img')
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('disagree', result.stderr)
            self.assertFalse((temp / 'stale.img').exists())


if __name__ == '__main__':
    unittest.main()
