"""The mount/umount dispatcher accepts upstream's exact command lines only."""
from pathlib import Path
import importlib.util
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'mount_dispatch.py'
spec = importlib.util.spec_from_file_location('mount_dispatch', SOURCE)
dispatch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dispatch)
translate, Refused = dispatch.translate, dispatch.Refused

R = '/var/lib/waydroid/rootfs'
V = R + '/vendor'
# Exactly as pinned Waydroid 1.6.3 issued them in the accepted enforcing run
# (out/boot-recovery/waydroid-stage5n-enforcing-evidence, manager log).
UPSTREAM = {
    ('mount', '-o', 'ro', '/usr/share/waydroid-extra/images/system.img', R):
        ['--mount-system', 'preinstalled'],
    ('mount', '-t', 'overlay', '-o',
     'ro,lowerdir=/var/lib/waydroid/overlay:/var/lib/waydroid/rootfs,'
     'upperdir=/var/lib/waydroid/overlay_rw/system,'
     'workdir=/var/lib/waydroid/overlay_work/system,xino=off', 'overlay', R):
        ['--overlay-system'],
    ('mount', '-o', 'ro', '/usr/share/waydroid-extra/images/vendor.img', V):
        ['--mount-vendor', 'preinstalled'],
    ('mount', '-t', 'overlay', '-o',
     'ro,lowerdir=/var/lib/waydroid/overlay/vendor:/var/lib/waydroid/rootfs/vendor,'
     'upperdir=/var/lib/waydroid/overlay_rw/vendor,'
     'workdir=/var/lib/waydroid/overlay_work/vendor,xino=off', 'overlay', V):
        ['--overlay-vendor'],
    ('mount', '-o', 'bind', '/var/lib/waydroid/waydroid.prop', V + '/waydroid.prop'):
        ['--bind-prop'],
    ('mount', '-o', 'ro', '/var/lib/waydroid/images/system.img', R):
        ['--mount-system', 'persistent'],
}
MOUNTINFO = '\n'.join((
    '33 20 0:28 / /var/lib/waydroid/rootfs ro,relatime - erofs /dev/loop1 ro',
    '37 33 0:29 / /var/lib/waydroid/rootfs ro,relatime - overlay overlay ro,lowerdir=...',
    '38 37 7:0 / /var/lib/waydroid/rootfs/vendor ro,relatime - ext4 /dev/loop0 ro,seclabel',
    '42 38 0:32 / /var/lib/waydroid/rootfs/vendor ro,relatime - overlay overlay ro',
    '43 42 0:45 /waydroid.prop /var/lib/waydroid/rootfs/vendor/waydroid.prop '
    'rw,nosuid,nodev,noexec,relatime - tmpfs tmpfs '
    'rw,context=system_u:object_r:openwave_android_prop_t:s0,size=128k,nr_inodes=4',
))


def without_top(mountinfo, point):
    lines = mountinfo.splitlines()
    for index in range(len(lines) - 1, -1, -1):
        if lines[index].split()[4] == point:
            del lines[index]
            break
    return '\n'.join(lines)


class DispatchTests(unittest.TestCase):
    def test_every_upstream_mount_maps_to_one_fixed_operation(self):
        for command, operation in UPSTREAM.items():
            with self.subTest(command=command):
                self.assertEqual(translate(command[0], list(command[1:]), MOUNTINFO), operation)

    def test_upstream_teardown_order_maps_each_layer(self):
        # umount_all() sorts mountpoints in reverse and unmounts each listed
        # entry, so a stacked path (erofs + overlay) is unmounted twice.
        state, seen = MOUNTINFO, []
        for point in sorted(dispatch.top_mounts(MOUNTINFO).keys() | {R, V}, reverse=True):
            while point in dispatch.top_mounts(state):
                seen.append(translate('umount', [point], state)[0])
                state = without_top(state, point)
        self.assertEqual(seen, ['--unbind-prop', '--unoverlay-vendor', '--unmount-vendor',
                                '--unoverlay-system', '--unmount-system'])

    def test_any_variation_is_refused(self):
        refused = (
            ('mount', ['-o', 'ro', '/tmp/system.img', R]),
            ('mount', ['-o', 'ro', '/usr/share/waydroid-extra/images/system.img', V]),
            ('mount', ['-o', 'ro', '/usr/share/waydroid-extra/images/../images/system.img', R]),
            ('mount', ['-o', 'rw', '/usr/share/waydroid-extra/images/system.img', R]),
            ('mount', ['-o', 'ro,remount', '/usr/share/waydroid-extra/images/system.img', R]),
            ('mount', ['/usr/share/waydroid-extra/images/system.img', R]),
            ('mount', ['-t', 'overlay', '-o', 'lowerdir=/,upperdir=/tmp,workdir=/tmp2',
                       'overlay', R]),
            ('mount', ['-t', 'overlay', '-o', UPSTREAM_OPTIONS_SYSTEM + ',nosuid', 'overlay', R]),
            ('mount', ['-o', 'bind', '/', R]),
            ('mount', ['-o', 'bind', '/etc/shadow', V + '/waydroid.prop']),
            ('mount', ['-o', 'bind', '/var/lib/waydroid/waydroid.prop', '/etc/passwd']),
            ('mount', ['-t', 'binder', 'binder', '/dev/binderfs']),
            ('mount', ['--bind', '/', '/mnt']),
            ('umount', ['-l', R]),
            ('umount', ['/']),
            ('umount', ['/var/lib/waydroid']),
            ('umount', ['-l', '/sys/fs/cgroup/schedtune']),
            ('umount', [R, V]),
            ('mount.ext4', ['-o', 'ro', '/usr/share/waydroid-extra/images/vendor.img', V]),
        )
        for name, argv in refused:
            with self.subTest(name=name, argv=argv):
                with self.assertRaises(Refused):
                    translate(name, argv, MOUNTINFO)

    def test_unmount_requires_the_expected_filesystem_on_top(self):
        foreign = MOUNTINFO + '\n50 33 0:40 / /var/lib/waydroid/rootfs rw - tmpfs tmpfs rw'
        with self.assertRaises(Refused):
            translate('umount', [R], foreign)
        with self.assertRaises(Refused):
            translate('umount', [R], '')

    def test_equivalent_path_spellings_are_refused(self):
        # Only the literal upstream spelling is accepted. Spellings that the
        # kernel would resolve to the same place (traversal, duplicate or
        # trailing slashes, a symlinked image store) never reach the helper,
        # and the helper itself never receives a path.
        image = '/usr/share/waydroid-extra/images/system.img'
        for source, target in (
                ('/usr/share/waydroid-extra/images/./system.img', R),
                ('//usr/share/waydroid-extra/images/system.img', R),
                ('/usr/share/waydroid-extra//images/system.img', R),
                ('/usr/share/waydroid-extra/images/system.img/', R),
                ('/var/lib/waydroid/../../usr/share/waydroid-extra/images/system.img', R),
                (image, R + '/'),
                (image, '/var/lib/waydroid/./rootfs'),
                (image, '/var/lib/waydroid/../waydroid/rootfs'),
                (image, '/var/lib//waydroid/rootfs')):
            with self.subTest(source=source, target=target):
                with self.assertRaises(Refused):
                    translate('mount', ['-o', 'ro', source, target], MOUNTINFO)
        for point in (R + '/', '/var/lib/waydroid/./rootfs', V + '/../vendor'):
            with self.subTest(point=point):
                with self.assertRaises(Refused):
                    translate('umount', [point], MOUNTINFO)

    def test_helper_receives_only_fixed_operation_tokens(self):
        allowed = {'--mount-system', '--mount-vendor', '--overlay-system', '--overlay-vendor',
                   '--bind-prop', '--unbind-prop', '--unoverlay-system', '--unoverlay-vendor',
                   '--unmount-system', '--unmount-vendor', 'preinstalled', 'persistent'}
        for command in UPSTREAM:
            for token in translate(command[0], list(command[1:]), MOUNTINFO):
                self.assertIn(token, allowed)

    def test_mountinfo_escapes_cannot_forge_an_approved_mountpoint(self):
        # A directory literally named "rootfs x" or "rootfs\\n..." is decoded,
        # not mistaken for the approved mountpoint.
        for line in (
                '60 20 0:40 / /var/lib/waydroid/rootfs\\040x rw - erofs /dev/loop9 ro',
                '61 20 0:41 / /var/lib/waydroid/rootfs\\012 rw - erofs /dev/loop9 ro',
                '62 20 0:42 / /var/lib/waydroid/rootfs\\134 rw - erofs /dev/loop9 ro'):
            with self.subTest(line=line):
                self.assertNotIn(R, dispatch.top_mounts(line))
                with self.assertRaises(Refused):
                    translate('umount', [R], line)
        self.assertIn(R + ' x', dispatch.top_mounts(
            '60 20 0:40 / /var/lib/waydroid/rootfs\\040x rw - erofs /dev/loop9 ro'))

    def test_property_unmount_requires_the_private_tmpfs_copy_on_top(self):
        prop = V + '/waydroid.prop'
        self.assertEqual(translate('umount', [prop], MOUNTINFO), ['--unbind-prop'])
        for fstype in ('overlay', 'ext4', 'proc'):
            with self.subTest(fstype=fstype):
                mountinfo = MOUNTINFO + f'\n70 42 0:50 / {prop} rw - {fstype} x rw'
                with self.assertRaises(Refused):
                    translate('umount', [prop], mountinfo)

    def test_dispatcher_has_no_privilege_or_environment_override(self):
        text = SOURCE.read_text()
        self.assertTrue(text.startswith('#!/usr/bin/python3.14 -I\n'))
        self.assertIn("HELPER = '/usr/libexec/openwave-waydroid/waydroid-mount'", text)
        self.assertNotIn('environ', text)
        self.assertNotIn('subprocess', text)
        self.assertNotIn('shell=True', text)


UPSTREAM_OPTIONS_SYSTEM = dispatch.OVERLAY_OPTIONS[R]

if __name__ == '__main__':
    unittest.main()
