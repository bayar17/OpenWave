#!/usr/bin/env python3
"""Remove unsupported AppArmor assignments from generated LXC templates only."""
import argparse
from pathlib import Path
import re

APPARMOR = re.compile(rb'^\s*lxc\.(?:aa_profile|apparmor\.profile)\s*=.*(?:\n|$)', re.M)


def prepare(package):
    for name in ('config_1', 'config_3'):
        path = package / 'usr/lib/waydroid/data/configs' / name
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f'Missing or symlinked LXC template: {path}')
        content = path.read_bytes()
        updated = APPARMOR.sub(b'', content)
        if content != updated:
            path.write_bytes(updated)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    prepare(parser.parse_args().package)
