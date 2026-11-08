#!/usr/bin/env python3
"""Rebuild the small early-boot binaries and policy into runtime staging.

Requires an already assembled OpenRC runtime and the target SDK. Compilation
never uses rootfs as a sysroot. Failed builds are not installed.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from openwave_runtime_loader import library_aliases, verify_core_loader
from openwave_graphics_runtime import verify_mesa_runtime


def guest_path(root, path):
    pending = path.lstrip('/').split('/')
    parts = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if parts:
                parts.pop()
            continue
        candidate = root.joinpath(*parts, part)
        if candidate.is_symlink():
            links += 1
            if links > 40:
                raise RuntimeError(f'Guest symlink loop: {path}')
            target = os.readlink(candidate)
            if target.startswith('/'):
                parts = []
            pending = target.split('/') + pending
        else:
            parts.append(part)
    return root.joinpath(*parts)


def validate(root):
    for name in ('bin/busybox', 'bin/sh', 'sbin/openrc-init', 'sbin/openrc-run',
                 'usr/bin/rc-status', 'usr/sbin/acpid', 'usr/bin/mount', 'usr/bin/umount'):
        path = guest_path(root, name)
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RuntimeError(f'Missing target prerequisite: /{name}; stage OpenRC first')
        with path.open('rb') as stream:
            header = stream.read(20)
        if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\x3e\x00':
            raise RuntimeError(f'Expected an x86_64 target ELF: /{name}')
    for path in (root / 'etc/runlevels').glob('*/*'):
        guest = '/' + path.relative_to(root).as_posix()
        if not path.is_symlink() or not guest_path(root, guest).is_file():
            raise RuntimeError(f'Invalid guest runlevel symlink: {guest}')


def runtime_root(path):
    # Preserve the supplied root boundary instead of accepting a link that
    # silently redirects all image preparation into another host directory.
    if path.is_symlink():
        raise RuntimeError(f'Refusing symlinked runtime root: {path}')
    root = path.resolve(strict=True)
    if not root.is_dir():
        raise RuntimeError(f'Runtime root is not a directory: {root}')
    return root


def require_directory_path(root, relative):
    current = root
    for component in Path(relative).parts:
        current /= component
        if current.is_symlink():
            raise RuntimeError(f'Unsafe directory symlink: {current}')
        if current.exists() and not current.is_dir():
            raise RuntimeError(f'Expected runtime directory: {current}')


ACCOUNT_DIRECTORY = 'var/lib/openwave-accounts'
PASSWD_LINK = '../var/lib/openwave-accounts/passwd'


def read_account_file(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 \
                or info.st_mode & 0o7133 or info.st_uid not in (0, os.getuid()) \
                or info.st_gid not in (0, os.getgid()):
            raise RuntimeError(f'Unsafe account file: {path}')
        contents = stream.read(1048577)
        if len(contents) > 1048576 or b'\0' in contents:
            raise RuntimeError(f'Invalid account database: {path}')
        return contents


def account_layout_plan(root):
    """Validate both sides before creating directories or replacing passwd."""
    require_directory_path(root, 'etc')
    require_directory_path(root, ACCOUNT_DIRECTORY)
    directory = root / ACCOUNT_DIRECTORY
    if directory.exists():
        info = directory.stat()
        if info.st_uid not in (0, os.getuid()) or info.st_gid not in (0, os.getgid()) \
                or info.st_mode & 0o7022:
            raise RuntimeError(f'Unsafe account directory metadata: {directory}')
    passwd, backing = root / 'etc/passwd', directory / 'passwd'
    backing_bytes = None
    if backing.is_symlink():
        raise RuntimeError(f'Refusing symlinked account backing: {backing}')
    if backing.exists():
        backing_bytes = read_account_file(backing)
    if passwd.is_symlink():
        if os.readlink(passwd) != PASSWD_LINK:
            raise RuntimeError(f'Unexpected passwd symlink: {passwd}')
        if backing_bytes is None:
            raise RuntimeError('Passwd symlink has no account backing')
        return backing_bytes
    if passwd.exists():
        source_bytes = read_account_file(passwd)
        if backing_bytes is not None and backing_bytes != source_bytes:
            raise RuntimeError('Conflicting passwd and account backing; refusing to replace either')
        return source_bytes
    if backing_bytes is None:
        raise RuntimeError('Missing passwd database; stage target accounts first')
    # Recover an interrupted migration whose backing was already published.
    return backing_bytes


def migrate_accounts(root, contents):
    directory = root / ACCOUNT_DIRECTORY
    backing, passwd = directory / 'passwd', root / 'etc/passwd'
    if not backing.exists():
        descriptor, temporary_name = tempfile.mkstemp(prefix='.passwd-', dir=directory)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, 'wb') as stream:
                stream.write(contents)
                stream.flush()
                os.fchmod(stream.fileno(), 0o644)
                os.fsync(stream.fileno())
            # Publish without replacing a concurrently created backing file.
            os.link(temporary, backing, follow_symlinks=False)
        finally:
            temporary.unlink()
    if backing.is_symlink() or read_account_file(backing) != contents:
        raise RuntimeError('Account backing changed during migration')
    backing.chmod(0o644)
    if passwd.is_symlink():
        if os.readlink(passwd) != PASSWD_LINK:
            raise RuntimeError('Passwd link changed during migration')
        return
    if not passwd.exists():
        passwd.symlink_to(PASSWD_LINK)
        return
    if read_account_file(passwd) != contents:
        raise RuntimeError('Passwd changed during migration; original preserved')
    temporary = passwd.with_name('.passwd-link-' + uuid.uuid4().hex)
    temporary.symlink_to(PASSWD_LINK)
    try:
        temporary.replace(passwd)
    finally:
        temporary.unlink(missing_ok=True)


def prepare_layout(root):
    root = runtime_root(root)
    # Check every parent before making any layout changes. Ordinary Path
    # operations follow parent links using host semantics, including absolute
    # links which the guest would resolve inside its own root.
    for relative in ('var/log', 'var/lib/misc', 'var/cache/rc',
                     'var/openwave/wallpaper', ACCOUNT_DIRECTORY, 'etc'):
        require_directory_path(root, relative)
    accounts = account_layout_plan(root)
    links = (('var/run', '/run'), ('var/lock', '/run/lock'),
             ('etc/mtab', '/proc/self/mounts'),
             ('etc/resolv.conf', '/run/NetworkManager/resolv.conf'))
    for relative, target in links:
        destination = root / relative
        if destination.is_symlink():
            if os.readlink(destination) != target:
                raise RuntimeError(f'Unexpected guest symlink: {relative}')
        elif destination.exists():
            if not destination.is_dir() or any(destination.iterdir()):
                raise RuntimeError(f'Refusing to replace {relative}')
    # Create persistent directories offline, avoiding boot-time writes to /etc
    # and the root directory. Only replace an empty legacy /var/run directory.
    for relative in ('var/log', 'var/lib/misc', 'var/cache/rc',
                     'var/openwave', 'var/openwave/wallpaper', ACCOUNT_DIRECTORY):
        destination = root / relative
        if guest_path(root, relative) != destination:
            raise RuntimeError(f'Unsafe directory: {relative}')
        destination.mkdir(parents=True, exist_ok=True)
        destination.chmod(0o755)
    for relative, target in links:
        destination = root / relative
        if destination.is_symlink():
            if os.readlink(destination) != target:
                raise RuntimeError(f'Unexpected guest symlink: {relative}')
        else:
            if destination.is_dir():
                destination.rmdir()  # Deliberately fails if data is present.
            elif destination.exists():
                raise RuntimeError(f'Refusing to replace {relative}')
            destination.symlink_to(target)
    migrate_accounts(root, accounts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=ROOT / 'toolchain')
    parser.add_argument('--target', default='x86_64-unknown-openwave')
    args = parser.parse_args()
    root, sdk = runtime_root(args.rootfs), args.sdk.resolve(strict=True)
    if args.target != 'x86_64-unknown-openwave':
        parser.error('boot image integration currently supports x86_64 only')
    validate(root)
    verify_mesa_runtime(root)
    prepare_layout(root)
    # Upstream's install defaults must not re-enable desktop/PC services.
    # These are links only; never remove a directory or service data here.
    for line in (ROOT / 'userspace/openrc/disabled-services.txt').read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        level, service = line.split()
        relative = f'etc/runlevels/{level}/{service}'
        require_directory_path(root, f'etc/runlevels/{level}')
        link = root / relative
        if link.is_symlink():
            link.unlink()
        elif link.exists():
            raise RuntimeError(f'Disabled service entry is not a runlevel link: {relative}')
    output = ROOT / 'out/boot-build' / args.target
    output.mkdir(parents=True, exist_ok=True)
    common = [str(sdk / 'bin/clang'), '--target=' + args.target,
              '--sysroot=' + str(sdk / 'sysroot' / args.target),
              '-O2', '-Wall', '-Wextra', '-Werror',
              '-I' + str(ROOT / 'userspace/security/selinux')]
    for source, name in (('bootstrap.c', 'init'), ('runtime-check.c', 'openwave-runtime-check'),
                         ('register-service-identity.c', 'openwave-register-service-identity')):
        support = []
        if source == 'bootstrap.c':
            support.append(str(ROOT / 'userspace/security/selinux/OpenWaveSELinux.c'))
        if source != 'register-service-identity.c':
            support.append('-lselinux')
        subprocess.run(common + [str(ROOT / 'userspace/openrc' / source)]
                       + support + ['-o', str(output / name)], check=True)
    subprocess.run(['python3', str(ROOT / 'scripts/build-openwave-selinux.py'),
                    '--sdk', str(sdk), '--target', args.target,
                    '--stage', str(output / 'stage'), '--output', str(output / 'policy')], check=True)
    # No partial installation on compile/policy failure.
    for source, relative in (
            (output / 'init', 'init'),
            (output / 'openwave-runtime-check', 'System/Services/openwave-runtime-check'),
            (output / 'openwave-register-service-identity', 'System/Services/openwave-register-service-identity'),
            (ROOT / 'userspace/openrc/kms.json', 'etc/openwave/kms.json'),
            (ROOT / 'userspace/config/NetworkManager.conf', 'etc/NetworkManager/NetworkManager.conf'),
            (ROOT / 'userspace/waydroid/zz-openwave-waydroid.conf',
             'etc/dbus-1/system.d/zz-openwave-waydroid.conf'),
            (ROOT / 'userspace/waydroid/prepare_binder.py',
             'usr/libexec/openwave-waydroid-binder.py'),
            (ROOT / 'userspace/openrc/openwave-wait-dbus',
             'System/Services/openwave-wait-dbus'),
            (ROOT / 'userspace/openrc/udev/70-openwave-drm.rules',
             'etc/udev/rules.d/70-openwave-drm.rules'),
            (ROOT / 'userspace/openrc/udev/70-openwave-input.rules',
             'etc/udev/rules.d/70-openwave-input.rules')) + tuple(
                (source, 'etc/init.d/' + source.name)
                for source in sorted((ROOT / 'userspace/openrc/etc/init.d').iterdir())
                if source.is_file()):
        destination = root / relative
        if guest_path(root, relative) != destination or destination.is_symlink():
            raise RuntimeError(f'Refusing symlinked boot destination: {destination}')
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + '.new')
        with temporary.open('xb') as stream:
            stream.write(source.read_bytes())
        temporary.chmod(0o644 if relative.endswith(('.rules', '.json', '.conf')) else 0o755)
        temporary.replace(destination)
    spec = importlib.util.spec_from_file_location('policy', ROOT / 'scripts/build-openwave-selinux.py')
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    policy.prepare_stage_etc(root)
    shutil.copytree(output / 'stage/etc/selinux', root / 'etc/selinux', dirs_exist_ok=True)
    policy.verify(root, args.target)
    for source, name in (('boot-report.start', 'openwave-boot-report.start'),
                         ('dns.start', 'openwave-dns.start'),
                         ('app-launch.start', 'zz-openwave-app-launch.start'),
                         ('portal.start', 'zz-openwave-portal.start'),
                         ('power.start', 'zz-openwave-power.start'),
                         ('keyboard.start', 'zz-openwave-keyboard.start'),
                         ('browser-actions.start', 'zz-openwave-browser-actions.start'),
                         ('waydroid.start', 'zz-openwave-waydroid.start'),
                         ('waydroid-mount-preflight.start', 'zzz-openwave-mount-preflight.start'),
                         ('wifi.start', 'zz-openwave-wifi.start'),
                         ('wifi-settings.start', 'zzz-openwave-wifi-settings.start'),
                         ('wifi-association.start', 'zzz-openwave-wifi-association.start')):
        report = root / 'etc/local.d' / name
        if guest_path(root, 'etc/local.d') != report.parent or report.is_symlink():
            raise RuntimeError('Unsafe boot report destination')
        report.parent.mkdir(exist_ok=True)
        shutil.copyfile(ROOT / 'userspace/openrc/qa' / source, report)
        report.chmod(0o755)
    aliases = library_aliases(root, args.target)
    count = verify_core_loader(root)
    print(f'Loader defaults: {len(aliases)} aliases added; {count} core ELF programs checked')


if __name__ == '__main__':
    main()
