#!/usr/bin/python3.14
import os
import pwd
import stat
import sys
import time

ACCOUNT = 'ocean'
USERS = '/Data/Users'
NAME = 'AndroidData'
SOURCE = '/var/lib/waydroid-user/data/media/0'
ANDROID_MEDIA_RW = 1023
LABEL = b'system_u:object_r:openwave_android_share_t:s0\0'
WAIT_SECONDS = 900
MAX_DEPTH = 64
MS_BIND = 4096
O_DIR = os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC


def _mount(source, target):
    import ctypes
    if ctypes.CDLL(None, use_errno=True).mount(source.encode(), target.encode(), None, MS_BIND, None):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), target)


def _unmount(target):
    import ctypes
    if ctypes.CDLL(None, use_errno=True).umount2(target.encode(), 0):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), target)


def open_path(path, flags=os.O_PATH):
    descriptor = os.open('/', os.O_PATH | O_DIR)
    try:
        for part in [p for p in path.split('/') if p]:
            following = os.open(part, os.O_PATH | O_DIR, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = following
        if flags != os.O_PATH:
            reopened = os.open('.', flags | O_DIR, dir_fd=descriptor)
            os.close(descriptor)
            descriptor = reopened
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def ensure_share(users=USERS, account=ACCOUNT, name=NAME, users_owner=0, media_group=ANDROID_MEDIA_RW):
    uid = pwd.getpwnam(account).pw_uid
    users_fd = os.open(users, os.O_PATH | O_DIR)
    try:
        if os.fstat(users_fd).st_uid != users_owner:
            raise RuntimeError(users + ' has an unexpected owner')
        home = os.open(account, os.O_PATH | O_DIR, dir_fd=users_fd)
    finally:
        os.close(users_fd)
    try:
        info = os.fstat(home)
        if info.st_uid != uid or info.st_mode & 0o022:
            raise RuntimeError(f'{account} has unexpected ownership or permissions')
        try:
            os.mkdir(name, 0o2770, dir_fd=home)
            created = True
        except FileExistsError:
            created = False
        point = os.open(name, os.O_RDONLY | O_DIR, dir_fd=home)
    finally:
        os.close(home)
    try:
        if created:
            os.fchown(point, uid, media_group)
            os.fchmod(point, 0o2770)
        if os.fstat(point).st_mode & 0o002:
            raise RuntimeError(name + ' is writable by everyone')
        return point
    except BaseException:
        os.close(point)
        raise


def relabel_tree(parent, name, depth=0):
    if depth > MAX_DEPTH:
        return 0
    info = os.stat(name, dir_fd=parent, follow_symlinks=False)
    if stat.S_ISDIR(info.st_mode):
        descriptor = os.open(name, os.O_RDONLY | O_DIR, dir_fd=parent)
        try:
            opened = os.fstat(descriptor)
            if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino):
                return 0
            _label(descriptor)
            count = 1
            for child in os.listdir(descriptor):
                try:
                    count += relabel_tree(descriptor, child, depth + 1)
                except OSError:
                    continue
            return count
        finally:
            os.close(descriptor)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        return 0
    descriptor = os.open(name, os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino) or opened.st_nlink != 1:
            return 0
        _label(f'/proc/self/fd/{descriptor}')
        return 1
    finally:
        os.close(descriptor)


def _label(target):
    if os.getxattr(target, 'security.selinux').rstrip(b'\0') != LABEL.rstrip(b'\0'):
        os.setxattr(target, 'security.selinux', LABEL)


def same_inode(first, second):
    one, two = os.fstat(first), os.fstat(second)
    return (one.st_dev, one.st_ino) == (two.st_dev, two.st_ino)


def mount_share(source=SOURCE, **share):
    source_fd = open_path(source)
    try:
        if not stat.S_ISDIR(os.fstat(source_fd).st_mode):
            raise RuntimeError(source + ' is not a directory')
        point = ensure_share(**share)
        try:
            parent, _, leaf = source.rpartition('/')
            parent_fd = open_path(parent)
            try:
                count = relabel_tree(parent_fd, leaf)
            finally:
                os.close(parent_fd)
            if not same_inode(point, source_fd):
                _mount(f'/proc/self/fd/{source_fd}', f'/proc/self/fd/{point}')
            return count
        finally:
            os.close(point)
    finally:
        os.close(source_fd)


def unmount_share(source=SOURCE, **share):
    source_fd = open_path(source)
    try:
        point = ensure_share(**share)
        try:
            if not same_inode(point, source_fd):
                return False
            _unmount(f'/proc/self/fd/{point}')
            return True
        finally:
            os.close(point)
    finally:
        os.close(source_fd)


def main():
    if sys.argv[1:] not in ([], ['unmount']) or os.getuid() or os.geteuid() or os.getgid() or os.getegid():
        print('openwave-waydroid-share: needs the root OpenRC identity; argument: unmount (optional)',
              file=sys.stderr)
        return 126
    with open('/proc/self/attr/current', 'rb') as stream:
        if stream.read().rstrip(b'\0\n') != b'system_u:system_r:openwave_system_t:s0':
            print('openwave-waydroid-share: needs the OpenWave supervisor domain', file=sys.stderr)
            return 126
    sys.dont_write_bytecode = True
    try:
        if sys.argv[1:] == ['unmount']:
            print('openwave-waydroid-share: ' + ('unmounted' if unmount_share() else 'nothing mounted'))
            return 0
        os.close(ensure_share())
        deadline = time.monotonic() + WAIT_SECONDS
        while True:
            try:
                count = mount_share()
                break
            except FileNotFoundError:
                if time.monotonic() > deadline:
                    print("openwave-waydroid-share: Android's storage folder never appeared", file=sys.stderr)
                    return 1
                time.sleep(2)
        print(f'openwave-waydroid-share: {NAME} shows Android storage ({count} entries labelled)')
        return 0
    except (OSError, KeyError, RuntimeError) as error:
        print('openwave-waydroid-share: failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
