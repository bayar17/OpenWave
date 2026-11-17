#!/usr/bin/python3.14
"""OpenRC-only adapter to upstream Binder setup; never reinitialize Android."""
import os
from pathlib import Path
import stat
import sys

DEVICE_RULES = Path('/var/lib/waydroid/openwave-devices.conf')
DMA_HEAP = '/dev/dma_heap/system'


def media_heap(path=None):
    path = path or DMA_HEAP
    try:
        mode = os.stat(path).st_mode
    except FileNotFoundError:
        return None
    if not stat.S_ISCHR(mode):
        raise RuntimeError('DMA-BUF system heap is not a character device: ' + path)
    return path


def write_device_rules(paths, destination):
    lines = ['# Generated at boot by openwave-waydroid-binder.py; do not edit.\n']
    for path in paths:
        device = os.stat(path).st_rdev
        lines.append(f'lxc.cgroup2.devices.allow = c {os.major(device)}:{os.minor(device)} rwm\n')
    temporary = destination.with_name(destination.name + '.tmp')
    temporary.write_text(''.join(lines))
    temporary.chmod(0o644)
    os.replace(temporary, destination)


def main():
    if len(sys.argv) != 1 or os.getuid() or os.geteuid() or os.getgid() or os.getegid():
        print('Waydroid Binder preparation requires the root OpenRC identity and no arguments', file=sys.stderr)
        return 126
    with open('/proc/self/attr/current', 'rb') as stream:
        context = stream.read().rstrip(b'\0\n')
    if context != b'system_u:system_r:openwave_system_t:s0':
        print('Waydroid Binder preparation requires the OpenWave supervisor domain', file=sys.stderr)
        return 126
    os.environ.clear()
    os.environ.update(PATH='/usr/bin:/usr/sbin:/bin:/sbin', HOME='/var/lib/waydroid',
                      TMPDIR='/run/openwave/waydroid', LANG='C.UTF-8')
    sys.dont_write_bytecode = True
    sys.path.insert(0, '/usr/lib/waydroid')
    import tools
    # Reuse upstream's parser/default preparation, with a fixed, non-executed
    # status action. Only its Binder helper runs; no init/config/data reset.
    sys.argv = [sys.argv[0], '--details-to-stdout', 'status']
    args = tools.helpers.arguments()
    tools.prep_args(args)
    tools.tools_logging.init(args)
    args.vendor_type = 'MAINLINE'
    tools.helpers.drivers.setupBinderNodes(args)
    paths = []
    for attribute in ('BINDER_DRIVER', 'VNDBINDER_DRIVER', 'HWBINDER_DRIVER'):
        path = '/dev/' + getattr(args, attribute)
        paths.append(path)
        if not stat.S_ISCHR(os.stat(path).st_mode):
            raise RuntimeError('Upstream did not prepare a Binder character device: ' + path)
        label = os.getxattr(path, 'security.selinux').rstrip(b'\0')
        if label != b'system_u:object_r:openwave_binderfs_t:s0':
            raise RuntimeError('Unexpected OpenWave Binder label: ' + path)
    heap = media_heap()
    if heap:
        paths.append(heap)
    write_device_rules(paths, DEVICE_RULES)
    print('Upstream Binder nodes prepared with dedicated OpenWave labels'
          + ('; DMA-BUF system heap allowed' if heap else '; no DMA-BUF system heap'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
