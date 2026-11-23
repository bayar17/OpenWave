#!/usr/bin/python3.14 -I
"""Install OpenWave's Android home screen (TINH) as a privileged system app.

Run by the OpenRC `waydroid` service before the manager starts. TINH is
Android's home in OpenWave: an empty screen that, when Android would show
home (Back on an app's first screen), moves the still-running app's task back
to the front, and closes Android's running apps when Ocean closes the Android
card. Seeing other apps' tasks (REAL_GET_TASKS) and closing them
(FORCE_STOP_PACKAGES) are granted only to privileged apps: an APK under
/system/priv-app that a privapp-permissions file lists.

The Android images are not modified. Upstream Waydroid's own overlay
directory (/var/lib/waydroid/overlay, a read-only lower layer over the system
image, mounted by the fixed-operation mount helper) carries exactly two
files. Idempotent; nothing else in the overlay is touched.
"""
import os
import sys

OVERLAY = '/var/lib/waydroid/overlay'
SOURCE = '/usr/share/openwave-waydroid/tinh.apk'
MAX_APK = 16 * 1024 * 1024
APK = ('system', 'priv-app', 'TINH', 'TINH.apk')
PERMISSIONS = ('system', 'etc', 'permissions', 'privapp-permissions-com.openwave.tinh.xml')
# REORDER_TASKS is a normal permission and needs no entry.
PERMISSIONS_XML = b'''<?xml version="1.0" encoding="utf-8"?>
<permissions>
    <privapp-permissions package="com.openwave.tinh">
        <permission name="android.permission.REAL_GET_TASKS"/>
        <permission name="android.permission.FORCE_STOP_PACKAGES"/>
        <permission name="android.permission.WRITE_SECURE_SETTINGS"/>
        <permission name="android.permission.DELETE_PACKAGES"/>
    </privapp-permissions>
</permissions>
'''
DIRECTORY = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC


def _directory(parent, name):
    """Open (creating if needed) a root-owned, non-symlink child directory."""
    try:
        os.mkdir(name, 0o755, dir_fd=parent)
    except FileExistsError:
        pass
    fd = os.open(name, DIRECTORY, dir_fd=parent)
    status = os.fstat(fd)
    if status.st_uid != os.geteuid() or status.st_mode & 0o022:
        os.close(fd)
        raise PermissionError(f'unexpected owner or mode on overlay directory {name!r}')
    return fd


def place(overlay, path, content):
    """Make overlay/path hold content (mode 0644); True if it was written."""
    *directories, name = path
    fd = os.open(overlay, DIRECTORY)
    try:
        for directory in directories:
            child = _directory(fd, directory)
            os.close(fd)
            fd = child
        try:
            current = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
        except FileNotFoundError:
            current = None
        if current is not None:
            try:
                with os.fdopen(os.dup(current), 'rb') as stream:
                    same = stream.read(len(content) + 1) == content
                same = same and os.fstat(current).st_mode & 0o777 == 0o644
            finally:
                os.close(current)
            if same:
                return False
        temporary = name + '.openwave-new'
        try:
            os.unlink(temporary, dir_fd=fd)
        except FileNotFoundError:
            pass
        out = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                      0o644, dir_fd=fd)
        try:
            os.write(out, content)
            os.fchmod(out, 0o644)
            os.fsync(out)
        finally:
            os.close(out)
        os.rename(temporary, name, src_dir_fd=fd, dst_dir_fd=fd)
        return True
    finally:
        os.close(fd)


def read_apk(source=SOURCE):
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    with os.fdopen(fd, 'rb') as stream:
        data = stream.read(MAX_APK + 1)
    # An APK is a ZIP archive; anything else is not installed.
    if not 0 < len(data) <= MAX_APK or data[:4] != b'PK\x03\x04':
        raise ValueError('home APK is missing, too large or not an APK')
    return data


def install(overlay=OVERLAY, source=SOURCE):
    changed = place(overlay, APK, read_apk(source))
    return place(overlay, PERMISSIONS, PERMISSIONS_XML) or changed


def main():
    if len(sys.argv) != 1 or os.getuid() or os.geteuid():
        print('install-home requires root and no arguments', file=sys.stderr)
        return 126
    with open('/proc/self/attr/current', 'rb') as stream:
        if stream.read().rstrip(b'\0\n') != b'system_u:system_r:openwave_system_t:s0':
            print('install-home requires the OpenRC supervisor domain', file=sys.stderr)
            return 126
    try:
        changed = install()
    except (OSError, ValueError) as error:
        print(f'install-home: {error}', file=sys.stderr)
        return 1
    print('Android home (TINH) privileged overlay ' + ('installed' if changed else 'already current'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
