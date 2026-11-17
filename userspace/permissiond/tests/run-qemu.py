#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'out/permission-validation'
SDK = Path(os.environ.get('OPENWAVE_SDK', ROOT / 'toolchain'))
TRIPLE = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
SYSROOT = SDK / 'sysroot' / TRIPLE
BUILD = ROOT / 'out' / ('userspace-' + TRIPLE)
TESTS = OUT / 'tests'


def run(args, log):
    with (OUT / log).open('w') as output:
        subprocess.run([str(x) for x in args], cwd=ROOT, env=os.environ.copy(), stdout=output, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    image = args.image.resolve()
    if image.exists():
        parser.error('The test image must not already exist')
    OUT.mkdir(parents=True, exist_ok=True)
    run([ROOT / 'build-openwave', 'kernel'], 'kernel.log')
    run([ROOT / 'build-openwave', 'userspace'], 'build.log')
    run(['cmake', '-S', ROOT / 'userspace/permissiond/tests', '-B', TESTS, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + str(SDK / ('cmake/openwave-' + TRIPLE.split('-')[0] + '.cmake'))], 'configure-tests.log')
    run(['cmake', '--build', TESTS, '--parallel', '3'], 'build-tests.log')
    run([SYSROOT / 'lib64/ld-linux-x86-64.so.2', '--library-path',
         ':'.join(str(SYSROOT / x) for x in ('usr/lib64', 'usr/lib', 'lib64')),
         TESTS / 'permission-policy-tests'], 'policy-tests.log')
    try:
        run(['cmake', '-S', ROOT / 'userspace', '-B', BUILD, '-DOPENWAVE_PERMISSION_TESTS=ON'], 'configure-shell-test.log')
        run(['cmake', '--build', BUILD, '--target', 'appOceanLauncher', '--parallel', '3'], 'build-shell-test.log')
        shutil.copy2(BUILD / 'OceanLauncher/ocean-launcher', OUT / 'qa-shell')
    finally:
        run(['cmake', '-S', ROOT / 'userspace', '-B', BUILD, '-DOPENWAVE_PERMISSION_TESTS=OFF'], 'restore-shell-config.log')
        run(['cmake', '--build', BUILD, '--target', 'appOceanLauncher', '--parallel', '3'], 'restore-shell-build.log')
    run([SDK / 'bin/clang', '--target=' + TRIPLE, '--sysroot=' + str(SYSROOT),
         ROOT / 'userspace/permissiond/tests/init.c', '-o', OUT / 'qa-init'], 'build-init.log')
    for suffix in ('a', 'b'):
        (OUT / ('fixture-' + suffix + '.json')).write_text(json.dumps({
            'schemaVersion': 1, 'id': 'org.openwave.qa.permission-' + suffix,
            'name': 'Permission Fixture ' + suffix, 'version': '1',
            'executable': 'bin/fixture', 'runtime': 'openwave-qt',
            'sandbox': {'enabled': True, 'profile': 'standard', 'requires': ['selinux', 'bubblewrap', 'seccomp']}, 'permissions': ['files.read', 'camera'],
            'arguments': ['--fixture-' + suffix]}))
    (OUT / 'selected.txt').write_text('selected-file')
    (OUT / 'audio.conf').write_text('context.objects = [\n'
        ' { factory = adapter args = { factory.name = support.null-audio-sink node.name = qa-playback media.class = Audio/Sink audio.position = [ FL FR ] } }\n'
        ' { factory = adapter args = { factory.name = support.null-audio-sink node.name = qa-capture media.class = Audio/Source audio.position = [ FL FR ] } }\n]\n')
    run([ROOT / 'build-openwave', 'image', ROOT / 'rootfs', image], 'image.log')
    commands = ['mkdir /Data/Users/root/Documents', 'sif /Data/Users/root/Documents mode 040755',
                'mkdir /etc/pipewire', 'mkdir /etc/pipewire/pipewire.conf.d']
    for source, target, mode, replace in [
        (ROOT / 'rootfs/init', '/init.openwave', '0100755', False),
        (OUT / 'qa-init', '/init', '0100755', True),
        (OUT / 'qa-shell', '/System/Applications/com.openwave.shell/bin/ocean-launcher', '0100755', True),
        (TESTS / 'permission-runtime-tests', '/System/Applications/com.openwave.settings/bin/ocean-settings', '0100755', True),
        (TESTS / 'permission-runtime-tests', '/usr/libexec/permission-runtime-tests', '0100755', False),
        (OUT / 'audio.conf', '/etc/pipewire/pipewire.conf.d/90-permission-qa.conf', '0100644', False),
        (OUT / 'selected.txt', '/Data/Users/root/Documents/permission-test.txt', '0100644', False)]:
        if replace:
            commands.append('rm ' + target)
        commands += ['write "' + str(source) + '" ' + target, 'sif ' + target + ' mode ' + mode,
                     'sif ' + target + ' uid 0', 'sif ' + target + ' gid 0']
    for suffix in ('a', 'b'):
        bundle = '/Applications/org.openwave.qa.permission-' + suffix
        commands += ['mkdir ' + bundle, 'sif ' + bundle + ' mode 040755',
                     'mkdir ' + bundle + '/bin', 'sif ' + bundle + '/bin mode 040755',
                     'write "' + str(TESTS / 'permission-runtime-tests') + '" ' + bundle + '/bin/fixture',
                     'sif ' + bundle + '/bin/fixture mode 0100755',
                     'write "' + str(OUT / ('fixture-' + suffix + '.json')) + '" ' + bundle + '/manifest.json',
                     'sif ' + bundle + '/manifest.json mode 0100644',
                     'sif ' + bundle + '/manifest.json uid 0', 'sif ' + bundle + '/manifest.json gid 0',
                     'sif ' + bundle + '/bin/fixture uid 0', 'sif ' + bundle + '/bin/fixture gid 0']
    (OUT / 'inject.txt').write_text('\n'.join(commands) + '\n')
    run(['debugfs', '-w', '-f', OUT / 'inject.txt', image], 'inject.log')
    if 'File not found' in (OUT / 'inject.log').read_text():
        raise RuntimeError('Test fixture injection failed; inspect inject.log')
    run([ROOT / 'run-openwave', '--image', image, '--usb-wifi', 'none', '--usb-bluetooth', 'none', '--audio-backend', 'none',
         '--vnc', '--vnc-host', '127.0.0.1', '--vnc-port', os.environ.get('PERMISSION_TEST_VNC_PORT', '5915'), 'run'], 'qemu.log')
    log = (OUT / 'qemu.log').read_text(errors='replace')
    if 'PERMISSION_QA_RESULT failures=0' not in log or 'PERMISSION_QA_TIMEOUT' in log:
        raise RuntimeError('Permission QEMU validation failed; inspect ' + str(OUT / 'qemu.log'))
    print('Permission QEMU validation passed: ' + str(OUT / 'qemu.log'))


if __name__ == '__main__':
    main()
