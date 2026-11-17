#!/usr/bin/env python3
"""Apply OpenWave's two documented adjustments to generated LXC templates only.

1. Remove AppArmor assignments (OpenWave does not use AppArmor).
2. Place the Android payload in its own SELinux domain. LXC applies
   lxc.selinux.context when it executes the container's init, so Android's
   processes never share the LXC monitor's domain.
3. Restrict the container's device cgroup to the devices Android uses.
No upstream Python, networking script or Android file is changed.
"""
import argparse
from pathlib import Path
import re

APPARMOR = re.compile(rb'^\s*lxc\.(?:aa_profile|apparmor\.profile)\s*=.*(?:\n|$)', re.M)
ANDROID_CONTEXT = 'system_u:system_r:openwave_android_t:s0'
# config_3 is included for every LXC >= 3; OpenWave ships LXC 6.
DEVICE_RULES = '/var/lib/waydroid/openwave-devices.conf'
SELINUX = (b'\n# OpenWave: confine the Android payload separately from the LXC monitor.\n'
           b'lxc.selinux.context = ' + ANDROID_CONTEXT.encode() + b'\n'
           # Upstream sets no device cgroup and keeps CAP_MKNOD, so Android
           # could create a node for any host device (the root disk too) in its
           # own /dev. Only the devices Android uses are reachable.
           b'lxc.cgroup2.devices.deny = a\n'
           b'lxc.cgroup2.devices.allow = c 1:3 rwm\n'      # null
           b'lxc.cgroup2.devices.allow = c 1:5 rwm\n'      # zero
           b'lxc.cgroup2.devices.allow = c 1:7 rwm\n'      # full
           b'lxc.cgroup2.devices.allow = c 1:8 rwm\n'      # random
           b'lxc.cgroup2.devices.allow = c 1:9 rwm\n'      # urandom
           b'lxc.cgroup2.devices.allow = c 1:11 wm\n'      # kmsg: write only
           b'lxc.cgroup2.devices.allow = c 5:0 rwm\n'      # tty
           b'lxc.cgroup2.devices.allow = c 5:1 rwm\n'      # console
           b'lxc.cgroup2.devices.allow = c 5:2 rwm\n'      # ptmx
           b'lxc.cgroup2.devices.allow = c 136:* rwm\n'    # the container's ptys
           b'lxc.cgroup2.devices.allow = c 10:183 rwm\n'   # hw_random
           b'lxc.cgroup2.devices.allow = c 10:200 rwm\n'   # tun
           b'lxc.cgroup2.devices.allow = c 10:223 rwm\n'   # uinput
           b'lxc.cgroup2.devices.allow = c 10:229 rwm\n'   # fuse
           b'lxc.cgroup2.devices.allow = c 10:237 rwm\n'   # loop-control
           b'lxc.cgroup2.devices.allow = c 10:239 rwm\n'   # uhid
           b'lxc.cgroup2.devices.allow = b 7:* rwm\n'      # loop (APEX images)
           b'lxc.cgroup2.devices.allow = c 226:128 rwm\n'  # renderD128
           b'lxc.include = ' + DEVICE_RULES.encode() + b'\n')
TEMPLATES = ('config_1', 'config_3')


def transform(name, content):
    """Return the OpenWave form of an upstream template; idempotent."""
    updated = APPARMOR.sub(b'', content)
    if name == 'config_3' and not updated.endswith(SELINUX):
        if b'lxc.selinux.context' in updated:
            raise RuntimeError('Unexpected SELinux context in LXC template')
        updated = updated.rstrip(b'\n') + b'\n' + SELINUX
    return updated


def prepare(package):
    for name in TEMPLATES:
        path = package / 'usr/lib/waydroid/data/configs' / name
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f'Missing or symlinked LXC template: {path}')
        content = path.read_bytes()
        updated = transform(name, content)
        if content != updated:
            path.write_bytes(updated)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    prepare(parser.parse_args().package)
