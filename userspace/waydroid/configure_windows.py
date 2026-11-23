#!/usr/bin/python3.14 -I
"""Set the few Android boot properties OpenWave's Android window depends on.

Run by the OpenRC `waydroid` service before the manager starts, after
`waydroid init`. Android's hardware composer reads
persist.waydroid.multi_windows once, when it opens, so the property must be
in Android's boot properties; a runtime setprop has no effect (Stage 8 runs
8c/8d). Upstream's own mechanism is the [properties] section of
waydroid.cfg, which `waydroid init`/`upgrade` copy into waydroid_base.prop,
from which the manager generates each session's waydroid.prop. This adapter
sets exactly that one property in both files, idempotently, and touches
nothing else. Upstream sources are unchanged.
"""
import configparser
import os
import sys
from pathlib import Path

ROOT = Path('/var/lib/waydroid')
# OpenWave shows Android as one window (Waydroid's full-UI mode): Android's
# navigation bar with Back, no Android status bar (Ocean's replaces it).
# Per-application windows are off: in this image they need the host
# android_wlegl protocol or per-layer task names that SurfaceFlinger does not
# provide (Stage 8 runs 8k-8q). The composer reads these once, at boot,
# which is why they are boot properties rather than a runtime setprop.
#   multi_windows=false      one Android window
#   width/height empty       no size override: Android follows Ocean's size
#   setupwizard DISABLED     Google's first-run wizard (GApps image) would
#                            otherwise own Android's screen (runs 8k-8s)
#   lcd_density 320          phone layout: Android follows Ocean's size
#                            (774x1296 on the QEMU screen), and Waydroid's
#                            default density 180 makes that 688dp wide, a
#                            tablet (taskbar, right-aligned navigation). At
#                            320 it is 387dp, a phone (centred navigation bar).
PROPERTIES = (('persist.waydroid.multi_windows', 'false'),
              ('persist.waydroid.width', ''),
              ('persist.waydroid.height', ''),
              ('ro.setupwizard.mode', 'DISABLED'),
              ('ro.sf.lcd_density', '320'))


def _replace(path, text):
    temporary = path.with_name(path.name + '.openwave-new')
    with open(temporary, 'x') as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    os.chmod(temporary, path.stat().st_mode & 0o777)
    os.replace(temporary, path)


def configure_cfg(text):
    """waydroid.cfg text with the [properties] entries set; None if unchanged."""
    config = configparser.ConfigParser()
    config.read_string(text)
    if not config.has_section('waydroid'):
        raise ValueError('waydroid.cfg has no [waydroid] section; not initialized')
    if not config.has_section('properties'):
        config.add_section('properties')
    if all(config.get('properties', key, fallback=None) == value for key, value in PROPERTIES):
        return None
    for key, value in PROPERTIES:
        config.set('properties', key, value)
    from io import StringIO
    output = StringIO()
    config.write(output)
    return output.getvalue()


def configure_prop(text):
    """waydroid_base.prop text with each property exactly once; None if unchanged."""
    lines = text.splitlines()
    keys = tuple(key + '=' for key, _ in PROPERTIES)
    kept = [line for line in lines if not line.startswith(keys)]
    wanted = kept + [f'{key}={value}' for key, value in PROPERTIES]
    return None if lines == wanted else '\n'.join(wanted) + '\n'


def main():
    if len(sys.argv) != 1 or os.getuid() or os.geteuid():
        print('configure-windows requires root and no arguments', file=sys.stderr)
        return 126
    for name, transform in (('waydroid.cfg', configure_cfg), ('waydroid_base.prop', configure_prop)):
        path = ROOT / name
        if path.is_symlink() or not path.is_file():
            print(f'configure-windows: {path} is missing or not a regular file', file=sys.stderr)
            return 1
        updated = transform(path.read_text())
        if updated is not None:
            _replace(path, updated)
    print('Waydroid per-application windows requested: '
          + ', '.join(f'{key}={value}' for key, value in PROPERTIES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
