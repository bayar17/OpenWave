#!/usr/bin/env python3
"""Conservative, idempotent OpenWave Waydroid ioctl policy patch for image 60.

Runs on the Ubuntu development host. Does NOT execute any guest binaries,
change SELinux enforcing mode, or edit images. Makes a dated sibling backup.
"""
from pathlib import Path
import argparse
import datetime
import re
import sys

BLOCK = '''\n;; WAYDROID_SIOCGIFINDEX_60_BEGIN\n;; iproute2 uses SIOCGIFINDEX to look up a network interface index.\n;; Image 59 audit: openwave_waydroid_net_t self:unix_dgram_socket ioctlcmd=0x8933.\n;; Keep this limited to the Waydroid network helper and that request number.\n(allow openwave_waydroid_net_t self (unix_dgram_socket (ioctl)))\n(allowx openwave_waydroid_net_t self (ioctl unix_dgram_socket (0x8933)))\n;; WAYDROID_SIOCGIFINDEX_60_END\n'''

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--root', type=Path, default=Path('/mnt/aosp/ThunderOS'))
p.add_argument('--apply', action='store_true', help='write a backed-up source change')
a = p.parse_args()
root = a.root.resolve()
policy = root / 'userspace/security/selinux/openwave.cil'
if not policy.is_file():
    sys.exit(f'ERROR: missing {policy}; no changes made')
s = policy.read_text()
if 'WAYDROID_SIOCGIFINDEX_60_BEGIN' in s:
    print('Already patched; no changes made')
    sys.exit(0)
if 'openwave_waydroid_net_t' not in s:
    sys.exit('ERROR: network SELinux domain not found; no changes made')
# A pre-existing xperm whitelist may need more careful reconciliation.
# Avoid blindly appending a possibly redundant or contradictory permission.
existing_x = [line.strip() for line in s.splitlines()
              if ('allowx' in line or 'permissionx' in line) and
              ('openwave_waydroid_net_t' in line or 'unix_dgram_socket' in line)]
if existing_x:
    print('REVIEW REQUIRED: existing extended permissions detected:')
    print('\n'.join(existing_x[:30]))
    sys.exit('No changes made. Review xperm statements before applying.')
print('Target:', policy)
print('Proposed addition:', BLOCK)
if not a.apply:
    print('DRY RUN ONLY: rerun with --apply to back up and append policy.')
    sys.exit(0)
backup = policy.with_name(policy.name + '.before-waydroid-ioctl-60.' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.write_text(s)
policy.write_text(s.rstrip() + '\n' + BLOCK)
print('Applied. Backup:', backup)
print('Next: ./build-openwave selinux && python3 -m unittest discover -s userspace/security/selinux/tests -p "test*.py"')
