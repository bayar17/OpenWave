from pathlib import Path
import importlib.util
import os
import pwd
import stat
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]


def load():
    spec = importlib.util.spec_from_file_location('share_storage', ROOT / 'userspace/waydroid/share_storage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ShareStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.share = load()

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.base = Path(directory.name)
        self.users = self.base / 'Users'
        (self.users / 'ocean').mkdir(parents=True, mode=0o700)
        self.source = self.base / 'waydroid' / 'media' / '0'
        self.mounts, self.unmounts, self.labelled = [], [], []
        entry = pwd.struct_passwd(('ocean', 'x', os.getuid(), os.getgid(), '', '/', '/bin/false'))

        def label(target):
            self.labelled.append(os.readlink(target) if isinstance(target, str) else target)

        self.patches = [
            patch.object(self.share.pwd, 'getpwnam', return_value=entry),
            patch.object(self.share, '_mount', side_effect=lambda s, d: self.mounts.append(
                (s, d, os.readlink(s), os.readlink(d)))),
            patch.object(self.share, '_unmount', side_effect=self.unmounts.append),
            patch.object(self.share, '_label', side_effect=label),
        ]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)

    def mount(self, **kwargs):
        return self.share.mount_share(str(self.source), users=str(self.users), users_owner=os.getuid(),
                                      media_group=os.getgid(), **kwargs)

    def test_waits_until_android_has_created_its_folder(self):
        with self.assertRaises(FileNotFoundError):
            self.mount()
        self.assertEqual(self.mounts, [])

    def test_androids_folder_is_mounted_on_androiddata(self):
        self.source.mkdir(parents=True)
        self.mount()
        info = (self.users / 'ocean' / 'AndroidData').stat()
        self.assertEqual(stat.S_IMODE(info.st_mode), 0o2770)
        self.assertEqual(len(self.mounts), 1)
        source, target, source_path, target_path = self.mounts[0]
        self.assertTrue(source.startswith('/proc/self/fd/') and target.startswith('/proc/self/fd/'))
        self.assertEqual(source_path, str(self.source))
        self.assertEqual(target_path, str(self.users / 'ocean' / 'AndroidData'))

    def test_nothing_of_the_users_own_data_is_mounted_or_changed(self):
        self.source.mkdir(parents=True)
        (self.users / 'ocean' / 'Pictures').mkdir()
        (self.users / 'ocean' / 'Pictures' / 'photo.png').write_text('x')
        self.mount()
        self.assertEqual(len(self.mounts), 1)
        self.assertEqual(sorted(p.name for p in (self.users / 'ocean').iterdir()), ['AndroidData', 'Pictures'])
        self.assertTrue(all('Pictures' not in str(x) for x in self.labelled))

    def test_tree_is_relabelled_without_following_links_or_hard_links(self):
        (self.source / 'Download' / 'sub').mkdir(parents=True)
        (self.source / 'Download' / 'a.txt').write_text('a')
        (self.source / 'Download' / 'sub' / 'b.txt').write_text('b')
        outside = self.base / 'secret'
        outside.write_text('s')
        os.link(outside, self.source / 'Download' / 'hardlink')
        (self.source / 'Download' / 'symlink').symlink_to(outside)
        os.mkfifo(self.source / 'Download' / 'fifo')
        count = self.mount()
        self.assertEqual(count, 5)

    def test_already_mounted_is_not_mounted_again(self):
        self.source.mkdir(parents=True)
        with patch.object(self.share, 'same_inode', return_value=True):
            self.mount()
        self.assertEqual(self.mounts, [])

    def test_planted_symlinks_are_refused(self):
        self.source.mkdir(parents=True)
        target = self.base / 'elsewhere'
        target.mkdir()
        (self.users / 'ocean' / 'AndroidData').symlink_to(target)
        with self.assertRaises(OSError):
            self.mount()
        self.assertEqual(self.mounts, [])
        self.assertEqual(stat.S_IMODE(target.stat().st_mode) & 0o2000, 0)

    def test_a_symlinked_source_or_home_is_refused(self):
        real = self.base / 'real'
        real.mkdir()
        (self.base / 'waydroid').mkdir()
        (self.base / 'waydroid' / 'media').symlink_to(real)
        with self.assertRaises(OSError):
            self.mount()
        outer = self.base / 'Other'
        outer.mkdir()
        (outer / 'ocean').symlink_to(self.users)
        self.source.parent.parent.joinpath('media').unlink()
        self.source.mkdir(parents=True)
        with self.assertRaises(OSError):
            self.share.mount_share(str(self.source), users=str(outer), users_owner=os.getuid())
        self.assertEqual(self.mounts, [])

    def test_group_writable_home_is_refused(self):
        self.source.mkdir(parents=True)
        (self.users / 'ocean').chmod(0o770)
        with self.assertRaises(RuntimeError):
            self.mount()

    def test_unmount_only_when_androiddata_shows_androids_folder(self):
        self.source.mkdir(parents=True)
        kwargs = dict(users=str(self.users), users_owner=os.getuid(), media_group=os.getgid())
        self.assertFalse(self.share.unmount_share(str(self.source), **kwargs))
        self.assertEqual(self.unmounts, [])
        with patch.object(self.share, 'same_inode', return_value=True):
            self.assertTrue(self.share.unmount_share(str(self.source), **kwargs))
        self.assertEqual(len(self.unmounts), 1)

    def test_refuses_to_run_unprivileged_or_with_unknown_arguments(self):
        with patch.object(self.share.sys, 'argv', ['x']):
            self.assertEqual(self.share.main(), 126)
        with patch.object(self.share.sys, 'argv', ['x', 'mount-everything']):
            self.assertEqual(self.share.main(), 126)


if __name__ == '__main__':
    unittest.main()
