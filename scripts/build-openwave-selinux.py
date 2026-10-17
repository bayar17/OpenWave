#!/usr/bin/env python3
"""Build host policy tools separately from target libraries, then stage policy."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'userspace/security/selinux'
DOMAINS = {
    'appd': ('appd', '/System/Services/appd'),
    'promptd': ('promptd', '/System/Services/promptd'),
    'permissiond': ('permissiond', '/System/Services/permissiond'),
    'portal': ('openwave-portal', '/System/Services/openwave-portal'),
    'xdg_portal': ('xdg-desktop-portal-openwave', '/System/Services/xdg-desktop-portal-openwave'),
    'wallpaper': ('wallpaperagentd', '/System/Services/wallpaperagentd'),
    'time': ('timed', '/System/Services/timed'),
    'connectivity': ('connectivityd', '/System/Services/connectivityd'),
    'audio': ('audiod', '/System/Services/audiod'),
    'shell': ('shell', '/System/Applications/com.openwave.shell/bin/ocean-launcher'),
    'settings': ('settings', '/System/Applications/com.openwave.settings/bin/ocean-settings'),
    'files': ('files', '/System/Applications/com.openwave.files/bin/ocean-files'),
}


def build(sdk, triple, stage, output):
    for name in ('libselinux.so.1', 'libsepol.so.2'):
        if not (sdk / 'sysroot' / triple / 'usr/lib64' / name).is_file():
            raise SystemExit('Missing target SELinux libraries: run ./build-openwave selinux first')
    sepol = ROOT / 'third_party/libsepol-3.11'
    if not (sepol / 'src/Makefile').is_file():
        raise SystemExit('Missing SELinux sources: run ./build-openwave selinux first')
    output.mkdir(parents=True, exist_ok=True)
    native = output / 'host-libsepol'
    if not native.exists():
        shutil.copytree(sepol, native)
    env = os.environ.copy()
    for key in ('CC', 'CXX', 'AR', 'LD', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                'CPATH', 'C_INCLUDE_PATH', 'LIBRARY_PATH', 'MAKEFLAGS', 'MFLAGS'):
        env.pop(key, None)
    cc = os.environ.get('OPENWAVE_HOST_CC', '/usr/bin/cc')
    subprocess.run(['make', '-C', str(native / 'src'), '-j', os.environ.get('OPENWAVE_BUILD_JOBS', '4'),
                    f'CC={cc}', 'AR=/usr/bin/ar', 'RANLIB=/usr/bin/ranlib', 'CFLAGS=-O2 -fPIC',
                    'CPPFLAGS=', 'LDFLAGS=', 'DISABLE_SHARED=y'], env=env, check=True)
    compiler = output / 'compile-policy'
    subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(native / 'include'),
                    '-I', str(native / 'cil/include'), str(SOURCE / 'compile-policy.c'),
                    str(native / 'src/libsepol.a'), '-o', str(compiler)], env=env, check=True)
    generator = output / 'kernel-policy'
    subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-I',
                    str(ROOT / 'linux/security/selinux/include'), str(SOURCE / 'kernel-policy.c'),
                    '-o', str(generator)], env=env, check=True)
    kernel = output / 'kernel.cil'
    with kernel.open('w') as stream:
        subprocess.run([str(generator)], stdout=stream, check=True)
    domains = output / 'services.cil'
    lines = []
    contexts = (SOURCE / 'file_contexts').read_text()
    for name, (_, path) in DOMAINS.items():
        domain, entry = f'openwave_{name}_t', f'openwave_{name}_exec_t'
        lines += [f'(type {domain})', f'(type {entry})',
                  f'(typetransition openwave_system_t {entry} process {domain})',
                  f'(typetransition openwave_appd_t {entry} process {domain})']
        escaped = path.replace('.', r'\.')
        contexts += f'{escaped} system_u:object_r:{entry}:s0\n'
    domains.write_text('\n'.join(lines) + '\n')
    policy = output / 'policy.35'
    temporary = output / 'policy.tmp'
    subprocess.run([str(compiler), str(temporary), str(kernel), str(domains),
                    str(SOURCE / 'openwave.cil')], check=True)
    temporary.replace(policy)
    destination = stage / 'etc/selinux/openwave'
    (destination / 'policy').mkdir(parents=True, exist_ok=True)
    (destination / 'contexts/files').mkdir(parents=True, exist_ok=True)
    shutil.copy2(policy, destination / 'policy/policy.35')
    (destination / 'contexts/files/file_contexts').write_text(contexts)
    shutil.copy2(SOURCE / 'config', destination.parent / 'config')
    for path in (destination / 'policy/policy.35', destination / 'contexts/files/file_contexts',
                 destination.parent / 'config'):
        path.chmod(0o644)
    print(f'OpenWave SELinux policy compiled and staged for {triple}: {destination}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.sdk.resolve(), args.target, args.stage.resolve(), args.output.resolve())
