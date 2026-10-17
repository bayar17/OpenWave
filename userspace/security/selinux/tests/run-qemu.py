#!/usr/bin/env python3
import argparse
import os
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / 'out/selinux-validation/runtime'


def run(args, log):
    with (OUT / log).open('w') as stream:
        subprocess.run([str(arg) for arg in args], cwd=ROOT, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description='Test SELinux in a new disposable OpenWave image')
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    image = args.image.resolve()
    if image.exists():
        parser.error('The test image must not already exist')
    OUT.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('OPENWAVE_SDK', ROOT / 'toolchain'))
    triple = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
    sysroot = sdk / 'sysroot' / triple
    runtime = Path(os.environ.get('OPENWAVE_ROOTFS', ROOT / 'rootfs'))
    run([ROOT / 'build-openwave', 'kernel'], 'kernel.log')
    run([ROOT / 'build-openwave', 'userspace'], 'build.log')
    source = ROOT / 'userspace/security/selinux/tests'
    for name in ('runtime', 'init'):
        run([sdk / 'bin/clang', '--target=' + triple, '--sysroot=' + str(sysroot),
             '-O2', '-Wall', '-Wextra', '-Werror', source / (name + '.c'), '-lselinux',
             '-o', OUT / name], 'compile-' + name + '.log')
    run([ROOT / 'build-openwave', 'image', runtime, image], 'image.log')
    commands = ['write "' + str(runtime / 'init') + '" /init.openwave', 'rm /init',
                'write "' + str(OUT / 'init') + '" /init', 'sif /init mode 0100755',
                'write "' + str(OUT / 'runtime') + '" /usr/libexec/selinux-qa',
                'sif /usr/libexec/selinux-qa mode 0100755',
                'sif /usr/libexec/selinux-qa uid 0', 'sif /usr/libexec/selinux-qa gid 0']
    (OUT / 'inject.txt').write_text('\n'.join(commands) + '\n')
    run(['debugfs', '-w', '-f', OUT / 'inject.txt', image], 'inject.log')
    if 'File not found' in (OUT / 'inject.log').read_text():
        raise RuntimeError('Test fixture injection failed')
    run([ROOT / 'run-openwave', '--image', image, '--usb-wifi', 'none', '--usb-bluetooth', 'none',
         '--audio-backend', 'none', '--vnc', '--vnc-host', '127.0.0.1', '--vnc-port',
         os.environ.get('SELINUX_QA_VNC_PORT', '5917'), 'run'], 'qemu.log')
    log = (OUT / 'qemu.log').read_text(errors='replace')
    # Kernel console records can interrupt a userspace line on the serial port.
    log = re.sub(r'\[\s*\d+\.\d+\][^\n]*\n', '', log)
    if 'SELINUX_QA_EXIT=0' not in log or 'SELINUX_QA FAIL' in log:
        raise RuntimeError('SELinux runtime tests failed; inspect ' + str(OUT / 'qemu.log'))
    print('SELinux QEMU validation passed: ' + str(OUT / 'qemu.log'))


if __name__ == '__main__':
    main()
