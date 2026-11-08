#!/usr/bin/env python3
"""Build host policy tools separately from target libraries, then stage policy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

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
    'power': ('powerd', '/System/Services/powerd'),
    'connectivity': ('connectivityd', '/System/Services/connectivityd'),
    'audio': ('audiod', '/System/Services/audiod'),
    'shell': ('shell', '/System/Applications/com.openwave.shell/bin/ocean-launcher'),
    'settings': ('settings', '/System/Applications/com.openwave.settings/bin/ocean-settings'),
    'files': ('files', '/System/Applications/com.openwave.files/bin/ocean-files'),
    'dbus': ('dbus-daemon', '/usr/bin/dbus-daemon'),
    'udev': ('udevd', '/usr/sbin/udevd'),
    'networkmanager': ('NetworkManager', '/usr/sbin/NetworkManager'),
    'iwd': ('iwd', '/usr/libexec/iwd'),
    'bluetooth': ('bluetoothd', '/usr/libexec/bluetooth/bluetoothd'),
    'pipewire': ('pipewire', '/usr/bin/pipewire'),
    'wireplumber': ('wireplumber', '/usr/bin/wireplumber'),
    'avahi': ('avahi-daemon', '/usr/sbin/avahi-daemon'),
    # OpenWave PID 1 service entrypoint execs the pinned Waydroid Python
    # manager without replacing its lifecycle implementation.
    'waydroid_manager': ('waydroidd', '/System/Services/waydroidd'),
    # Retained only for legacy isolated QA; it is not installed in the runtime.
    'waydroid_launcher': ('waydroidd-launcher', '/System/Services/waydroidd-launcher'),
    # D-Bus activates this short-lived root helper for one fixed operation.
    'waydroid_helper': ('waydroid-privd', '/System/Services/waydroid-privd'),
}

# Each source is listed deliberately. Ordinary OpenWave services retain their
# historical system/appd activation paths. Waydroid principals have narrower
# activation paths and must never inherit those common transitions.
TRANSITION_SOURCES = {
    'appd': ('openwave_system_t', 'openwave_appd_t'),
    'promptd': ('openwave_system_t', 'openwave_appd_t'),
    'permissiond': ('openwave_system_t', 'openwave_appd_t'),
    'portal': ('openwave_system_t', 'openwave_appd_t'),
    'xdg_portal': ('openwave_system_t', 'openwave_appd_t'),
    'wallpaper': ('openwave_system_t', 'openwave_appd_t'),
    'time': ('openwave_system_t', 'openwave_appd_t'),
    'power': ('openwave_system_t',),
    'connectivity': ('openwave_system_t', 'openwave_appd_t'),
    'audio': ('openwave_system_t', 'openwave_appd_t'),
    'shell': ('openwave_system_t', 'openwave_appd_t'),
    'settings': ('openwave_system_t', 'openwave_appd_t'),
    'files': ('openwave_system_t', 'openwave_appd_t'),
    'dbus': ('openwave_system_t', 'openwave_appd_t'),
    'udev': ('openwave_system_t',),
    # OpenRC alone activates these native infrastructure services.
    'networkmanager': ('openwave_system_t',),
    'iwd': ('openwave_system_t',),
    'bluetooth': ('openwave_system_t',),
    'pipewire': ('openwave_system_t',),
    'wireplumber': ('openwave_system_t',),
    'avahi': ('openwave_system_t',),
    'waydroid_manager': ('openwave_system_t', 'openwave_dbus_t'),
    'waydroid_launcher': ('openwave_dbus_t',),
    'waydroid_helper': ('openwave_dbus_t', 'openwave_system_t'),
}


def render_services_cil():
    lines = []
    contexts = (SOURCE / 'file_contexts').read_text()
    if set(TRANSITION_SOURCES) != set(DOMAINS):
        raise ValueError('every generated service domain needs explicit transition sources')
    for name, (_, path) in DOMAINS.items():
        domain, entry = f'openwave_{name}_t', f'openwave_{name}_exec_t'
        lines += [f'(type {domain})', f'(type {entry})']
        lines.extend(f'(typetransition {source} {entry} process {domain})'
                     for source in TRANSITION_SOURCES[name])
        if not name.startswith('waydroid_'):
            lines += [f'(allow {domain} {entry} (file (read open getattr map execute entrypoint execute_no_trans)))']
            for source in TRANSITION_SOURCES[name]:
                lines += [f'(allow {source} {entry} (file (read open getattr map execute)))',
                          f'(allow {source} {domain} (process (transition)))']
        escaped = path.replace('.', r'\.')
        contexts += f'{escaped} system_u:object_r:{entry}:s0\n'
    contexts += '/usr/bin/udevadm system_u:object_r:openwave_udev_exec_t:s0\n'
    identity_context = ('/System/Services/openwave-register-service-identity '
                        'system_u:object_r:openwave_service_identity_exec_t:s0')
    if identity_context not in contexts.splitlines():
        contexts += identity_context + '\n'
    return '\n'.join(lines) + '\n', contexts


def policy_inputs():
    paths = [Path(__file__).resolve(), SOURCE / 'compile-policy.c',
             SOURCE / 'kernel-policy.c', SOURCE / 'openwave.cil',
             SOURCE / 'file_contexts', SOURCE / 'config',
             SOURCE / 'tests/inspect_qa_policy.c',
             SOURCE / 'tests/test_file_contexts.py']
    paths.extend((ROOT / 'linux/security/selinux/include').glob('*.h'))
    sepol = ROOT / 'third_party/libsepol-3.11'
    paths.extend(path for base in (sepol / 'src', sepol / 'include', sepol / 'cil')
                 for path in base.rglob('*') if path.is_file()
                 and (path.suffix in ('.c', '.h', '.inc', '.in', '.mk')
                      or path.name in ('Makefile', 'Kbuild')))
    paths.extend(path for path in sepol.iterdir()
                 if path.is_file() and path.name in ('VERSION', 'Makefile'))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(set(paths))}


def write_if_changed(path, content):
    if path.is_file() and path.read_text() == content:
        return
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(content)
    temporary.replace(path)


def reject_context_sidecars(stage):
    # libselinux also loads local, homedir, substitution, and compiled context
    # companions. This pipeline publishes and hashes only the base text file.
    directory = stage / 'etc/selinux/openwave/contexts/files'
    companions = sorted(path.name for path in directory.glob('file_contexts*')
                        if path.name != 'file_contexts')
    if companions:
        raise SystemExit('Unsupported file_contexts companions: ' + ', '.join(companions))


def prepare_stage_etc(stage):
    etc = stage / 'etc'
    for relative in ('etc', 'etc/selinux', 'etc/selinux/openwave',
                     'etc/selinux/openwave/policy', 'etc/selinux/openwave/contexts',
                     'etc/selinux/openwave/contexts/files'):
        directory = stage / relative
        if directory.is_symlink():
            raise SystemExit(f'Refusing symlink in SELinux policy staging path: {directory}')
        directory.mkdir(parents=True, exist_ok=True)
        if not directory.is_dir():
            raise SystemExit(f'Invalid SELinux policy staging directory: {directory}')
        # This stage is overlaid onto the runtime tree with cp -a. Set every
        # trusted policy parent explicitly instead of leaking the host umask.
        directory.chmod(0o755)
    reject_context_sidecars(stage)
    for relative in ('etc/selinux/config', 'etc/selinux/openwave/policy/policy.35',
                     'etc/selinux/openwave/contexts/files/file_contexts',
                     'etc/selinux/openwave/build-manifest.json'):
        if (stage / relative).is_symlink():
            raise SystemExit(f'Refusing symlinked SELinux artifact: {stage / relative}')
    return etc


def build(sdk, triple, stage, output):
    for name in ('libselinux.so.1', 'libsepol.so.2'):
        if not (sdk / 'sysroot' / triple / 'usr/lib64' / name).is_file():
            raise SystemExit('Missing target SELinux libraries: run ./build-openwave selinux first')
    sepol = ROOT / 'third_party/libsepol-3.11'
    if not (sepol / 'src/Makefile').is_file():
        raise SystemExit('Missing SELinux sources: run ./build-openwave selinux first')
    output.mkdir(parents=True, exist_ok=True)
    # Never let a previous successful manifest make partially rebuilt output
    # appear current after a later compiler or policy step fails.
    (output / 'build-manifest.json').unlink(missing_ok=True)
    native = output / 'host-libsepol'
    if native.exists():
        shutil.rmtree(native)
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
    with kernel.with_suffix('.cil.tmp').open('w') as stream:
        subprocess.run([str(generator)], stdout=stream, check=True)
    kernel.with_suffix('.cil.tmp').replace(kernel)
    domains = output / 'services.cil'
    services_cil, contexts = render_services_cil()
    write_if_changed(domains, services_cil)
    policy = output / 'policy.35'
    temporary = output / 'policy.tmp'
    subprocess.run([str(compiler), str(temporary), str(kernel), str(domains),
                    str(SOURCE / 'openwave.cil')], check=True)
    temporary.replace(policy)
    inspector = output / 'inspect-qa-policy'
    subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(native / 'include'),
                    str(SOURCE / 'tests/inspect_qa_policy.c'), str(native / 'src/libsepol.a'),
                    '-o', str(inspector)], env=env, check=True)
    subprocess.run([str(inspector), str(policy)], check=True)
    # The installed map is the static source PLUS generated service
    # entrypoints. Validate its actual libselinux precedence, not just the
    # source regexes, before issuing a fresh policy/build manifest.
    with tempfile.TemporaryDirectory(prefix='context-check-', dir=output) as work:
        effective = Path(work) / 'file_contexts'
        effective.write_text(contexts)
        check_env = {**env, 'OPENWAVE_FILE_CONTEXTS': str(effective)}
        subprocess.run([sys.executable, str(SOURCE / 'tests/test_file_contexts.py')],
                       cwd=ROOT, env=check_env, check=True)
    manifest = {
        'schema': 1,
        'target': triple,
        'inputs': policy_inputs(),
        'services_cil_sha256': hashlib.sha256(domains.read_bytes()).hexdigest(),
        'kernel_cil_sha256': hashlib.sha256(kernel.read_bytes()).hexdigest(),
        'policy_sha256': hashlib.sha256(policy.read_bytes()).hexdigest(),
        'contexts_sha256': hashlib.sha256(contexts.encode()).hexdigest(),
        'config_sha256': hashlib.sha256((SOURCE / 'config').read_bytes()).hexdigest(),
    }
    write_if_changed(output / 'build-manifest.json', json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    etc = prepare_stage_etc(stage)
    destination = etc / 'selinux/openwave'
    (destination / 'policy').mkdir(parents=True, exist_ok=True)
    (destination / 'contexts/files').mkdir(parents=True, exist_ok=True)
    shutil.copy2(policy, destination / 'policy/policy.35')
    write_if_changed(destination / 'contexts/files/file_contexts', contexts)
    shutil.copy2(SOURCE / 'config', destination.parent / 'config')
    for path in (destination / 'policy/policy.35', destination / 'contexts/files/file_contexts',
                 destination.parent / 'config'):
        path.chmod(0o644)
    shutil.copy2(output / 'build-manifest.json', destination / 'build-manifest.json')
    (destination / 'build-manifest.json').chmod(0o644)
    print(f'OpenWave SELinux policy compiled and staged for {triple}: {destination}')


def verify(stage, triple):
    reject_context_sidecars(stage)
    destination = stage / 'etc/selinux/openwave'
    manifest = json.loads((destination / 'build-manifest.json').read_text())
    if manifest['target'] != triple or manifest['inputs'] != policy_inputs():
        raise SystemExit('Stale SELinux policy inputs: rebuild policy before packing')
    for name, relative in (('policy', 'policy/policy.35'),
                           ('contexts', 'contexts/files/file_contexts'), ('config', '../config')):
        if hashlib.sha256((destination / relative).read_bytes()).hexdigest() != manifest[name + '_sha256']:
            raise SystemExit(f'Stale or modified staged SELinux {name}')
    print('Verified current SELinux sources, policy, contexts, and configuration')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    if args.verify_only:
        verify(args.stage.resolve(), args.target)
    else:
        if args.output is None:
            parser.error('--output is required for a build')
        build(args.sdk.resolve(), args.target, args.stage.resolve(), args.output.resolve())
