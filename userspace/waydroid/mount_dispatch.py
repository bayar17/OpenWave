#!/usr/bin/python3.14 -I
"""Route upstream Waydroid's mount/umount calls to the fixed-operation helper.

Installed as /usr/libexec/openwave-waydroid/mount and .../umount. The
Waydroid manager's fixed PATH (waydroidd.c) starts with that directory, so
upstream's tools.helpers.mount reaches this file instead of util-linux.

This program has no privilege of its own: it runs in the manager's domain,
which no longer holds CAP_SYS_ADMIN or any mount permission. It recognizes
the exact command lines that pinned Waydroid 1.6.3 issues on an OpenWave host
and executes /usr/libexec/openwave-waydroid/waydroid-mount with one fixed
operation, which SELinux runs in openwave_waydroid_mount_t. Every other
command line is refused, so a new upstream mount fails closed and visibly.
"""
import os
import re
import sys

HELPER = '/usr/libexec/openwave-waydroid/waydroid-mount'
ROOT = '/var/lib/waydroid'
ROOTFS = ROOT + '/rootfs'
VENDOR = ROOTFS + '/vendor'
STORES = {'/usr/share/waydroid-extra/images': 'preinstalled',
          ROOT + '/images': 'persistent'}
OVERLAY_OPTIONS = {
    ROOTFS: ('ro,lowerdir={0}/overlay:{1},upperdir={0}/overlay_rw/system,'
             'workdir={0}/overlay_work/system,xino=off').format(ROOT, ROOTFS),
    VENDOR: ('ro,lowerdir={0}/overlay/vendor:{1},upperdir={0}/overlay_rw/vendor,'
             'workdir={0}/overlay_work/vendor,xino=off').format(ROOT, VENDOR),
}
HOST_BINDS = {
    ('/vendor/lib/egl', VENDOR + '/lib/egl'): 'egl32',
    ('/vendor/lib64/egl', VENDOR + '/lib64/egl'): 'egl64',
    ('/odm', ROOTFS + '/odm_extra'): 'odm',
    ('/vendor/odm', ROOTFS + '/odm_extra'): 'vendor-odm',
}
# Unmounting: mountpoint -> {type of the topmost mount there: operation}
UNMOUNT = {
    ROOTFS: {'overlay': '--unoverlay-system', 'erofs': '--unmount-system'},
    VENDOR: {'overlay': '--unoverlay-vendor', 'ext4': '--unmount-vendor'},
    # The helper binds its Android copy of the property file, which lives on
    # a private tmpfs (never on a host filesystem).
    VENDOR + '/waydroid.prop': {'tmpfs': '--unbind-prop'},
}
_ESCAPE = re.compile(r'\\([0-7]{3})')


class Refused(Exception):
    pass


def _image(source, destination):
    directory, _, name = source.rpartition('/')
    store = STORES.get(directory)
    if store and (name, destination) == ('system.img', ROOTFS):
        return ['--mount-system', store]
    if store and (name, destination) == ('vendor.img', VENDOR):
        return ['--mount-vendor', store]
    raise Refused('not an approved Android image and mountpoint')


def top_mounts(mountinfo):
    """Mountpoint -> filesystem type of the most recently stacked mount."""
    top = {}
    for line in mountinfo.splitlines():
        fields, separator, tail = line.partition(' - ')
        fields = fields.split()
        if not separator or len(fields) < 5 or not tail.split():
            continue
        # The kernel octal-escapes space, tab, newline and backslash, so a
        # directory name can never forge a line or an approved mountpoint.
        point = _ESCAPE.sub(lambda m: chr(int(m.group(1), 8)), fields[4])
        top[point] = tail.split()[0]
    return top


def translate(name, argv, mountinfo, odm_is_mountpoint=False):
    """Return the helper's argument list for an exact upstream command line."""
    if name == 'mount':
        if len(argv) == 4 and argv[:2] == ['-o', 'ro']:
            return _image(argv[2], argv[3])
        if len(argv) == 6 and argv[:2] == ['-t', 'overlay'] and argv[2] == '-o' \
                and argv[4] == 'overlay' and OVERLAY_OPTIONS.get(argv[5]) == argv[3]:
            return ['--overlay-system' if argv[5] == ROOTFS else '--overlay-vendor']
        if len(argv) == 4 and argv[:2] == ['-o', 'bind']:
            source, destination = argv[2], argv[3]
            if (source, destination) == (ROOT + '/waydroid.prop', VENDOR + '/waydroid.prop'):
                return ['--bind-prop']
            if (source, destination) in HOST_BINDS:
                return ['--bind-' + HOST_BINDS[(source, destination)]]
        raise Refused('mount command line is not one of the approved Waydroid operations')
    if name == 'umount':
        if len(argv) != 1:
            raise Refused('only a plain umount of one approved mountpoint is supported')
        point = argv[0]
        if point in (VENDOR + '/lib/egl', VENDOR + '/lib64/egl'):
            return ['--unbind-' + ('egl32' if point.endswith('/lib/egl') else 'egl64')]
        if point == ROOTFS + '/odm_extra':
            return ['--unbind-' + ('odm' if odm_is_mountpoint else 'vendor-odm')]
        kinds = UNMOUNT.get(point)
        fstype = top_mounts(mountinfo).get(point)
        if not kinds or fstype not in kinds:
            raise Refused('not an approved Waydroid mountpoint, or unexpected filesystem')
        return [kinds[fstype]]
    raise Refused('unknown command name')


def main():
    name = os.path.basename(sys.argv[0])
    try:
        with open('/proc/self/mountinfo') as stream:
            mountinfo = stream.read()
        operation = translate(name, sys.argv[1:], mountinfo, os.path.ismount('/odm'))
    except (Refused, OSError) as error:
        print(f'openwave-waydroid {name}: refused {sys.argv[1:]}: {error}', file=sys.stderr)
        return 1
    os.execv(HELPER, [HELPER, *operation])


if __name__ == '__main__':
    sys.exit(main())
