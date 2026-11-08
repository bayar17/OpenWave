#!/usr/bin/env python3
"""Build the real broker against the target SDK, then run isolated IPC tests.

Only this temporary executable enables PORTAL_TESTING; deployed brokers do not.
"""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'
with tempfile.TemporaryDirectory(prefix='openwave-portal-contract-') as temporary:
    directory = Path(temporary)
    binary = directory / 'portal'
    includes = [ROOT / 'toolchain/include', ROOT / 'userspace/security', ROOT / 'userspace/xml']
    includes += [SDK / p for p in ('usr/include/glib-2.0', 'usr/lib64/glib-2.0/include',
                                  'usr/include/gio-unix-2.0', 'usr/include/libxml2')]
    subprocess.run([str(ROOT / 'toolchain/bin/clang'), '--target=x86_64-unknown-openwave',
        '--sysroot=' + str(SDK), '-D_GNU_SOURCE', '-Wno-macro-redefined', '-DPORTAL_TESTING',
        '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
        *['-I' + str(p) for p in includes],
        '-DOPENWAVE_DBUS_INTERFACE_DIR="' + str(ROOT / 'userspace/dbus/interfaces') + '"',
        *[str(ROOT / p) for p in ('userspace/openwave-portal/main.c',
                                 'userspace/security/OpenWaveIdentity.c', 'userspace/xml/OpenWaveXml.c')],
        '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-lxml2', '-lselinux', '-o', str(binary)], check=True)
    wrapper = directory / 'run-portal'
    wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([
        str(SDK / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
        ':'.join(str(SDK / p) for p in ('usr/lib64', 'usr/lib', 'lib64')), str(binary)]) + ' "$@"\n')
    wrapper.chmod(0o755)
    subprocess.run(['dbus-run-session', '--', '/usr/bin/python3',
                    str(ROOT / 'userspace/openwave-portal/tests/portal_contract.py'), str(wrapper)], check=True)
