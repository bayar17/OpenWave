#!/usr/bin/env python3
"""Read-only OpenWave pre-boot/rootfs readiness audit.

Checks the existing OpenRC validator and extends its coverage to static runtime
integrity. Does not install, change ownership, label, or create an image.

Usage: python3 openwave-readiness.py [--repo /path/to/ThunderOS] [--rootfs /path/to/rootfs]
"""
import argparse
from collections import Counter, defaultdict
import os
from pathlib import Path
import re
import subprocess
import sys

REQUIRED_COMMANDS = (
    '/init', '/sbin/openrc-init', '/sbin/openrc-run', '/sbin/rc-service',
    '/sbin/rc-update', '/sbin/openrc-shutdown', '/usr/bin/rc-status',
    '/bin/busybox', '/bin/sh', '/usr/bin/mount', '/usr/bin/umount',
    '/usr/sbin/acpid', '/usr/bin/dbus-daemon', '/usr/bin/udevadm',
    '/usr/bin/pipewire', '/usr/bin/wireplumber',
    '/System/Services/appd', '/System/Services/permissiond',
    '/System/Services/promptd', '/System/Services/audiod',
    '/System/Services/connectivityd', '/System/Services/timed',
    '/System/Services/wallpaperagentd', '/System/Services/openwave-portal',
    '/System/Services/xdg-desktop-portal-openwave',
    '/System/Services/openwave-runtime-check',
    '/System/Services/openwave-wait-dbus',
    '/System/Services/openwave-register-service-identity',
    '/System/Applications/com.openwave.shell/bin/ocean-launcher',
)
SELINUX_FILES = (
    '/etc/selinux/config',
    '/etc/selinux/openwave/policy/policy.35',
    '/etc/selinux/openwave/contexts/files/file_contexts',
)
DBUS_POLICY_FILES = (
    '/usr/share/dbus-1/system.d/org.openwave.Applications.conf',
    '/usr/share/dbus-1/system.d/org.openwave.Permissions.conf',
)
QML_MODULES = ('Application', 'Fonts', 'Theme', 'Controls')
LIB_DIRS = ('/lib64', '/usr/lib64', '/lib', '/usr/lib', '/usr/local/lib64', '/usr/local/lib')

class Audit:
    def __init__(self, repo, root):
        self.repo, self.root = repo, root
        self.counts = Counter()
        self.samples = defaultdict(list)

    def report(self, level, topic, message):
        self.counts[level] += 1
        print(f'[{level:4}] {topic}: {message}')

    def pass_(self, topic, message): self.report('PASS', topic, message)
    def fail(self, topic, message): self.report('FAIL', topic, message)
    def warn(self, topic, message): self.report('WARN', topic, message)

    def virtual(self, guest_path):
        """Resolve guest-absolute symlinks inside the rootfs, never on host."""
        if not guest_path.startswith('/'):
            raise ValueError(f'not guest-absolute: {guest_path}')
        pieces = guest_path.split('/')
        stack = []
        nlinks = 0
        while pieces:
            part = pieces.pop(0)
            if part in ('', '.'): continue
            if part == '..':
                if stack: stack.pop()
                continue
            candidate = self.root.joinpath(*stack, part)
            if candidate.is_symlink():
                nlinks += 1
                if nlinks > 40: raise RuntimeError('symlink loop')
                target = os.readlink(candidate)
                if target.startswith('/'):
                    stack = []
                pieces = target.split('/') + pieces
            else:
                stack.append(part)
        return self.root.joinpath(*stack)

    def exists(self, guest_path, *, executable=False):
        try:
            path = self.virtual(guest_path)
            return path.is_file() and (not executable or os.access(path, os.X_OK))
        except (OSError, RuntimeError, ValueError):
            return False

    def run_validator(self):
        tool = self.repo / 'userspace/openrc/validate_openrc.py'
        if not tool.is_file():
            self.fail('OpenRC validator', f'missing {tool}')
            return
        env = os.environ.copy()
        env['OPENWAVE_OPENRC_ROOT'] = str(self.root)
        result = subprocess.run([sys.executable, str(tool)], cwd=self.repo, env=env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            self.fail('OpenRC validator', result.stdout.strip()[-1400:] or f'exit {result.returncode}')
        else:
            self.pass_('OpenRC validator', result.stdout.strip().replace('\n', ' | '))

    def check_core(self):
        missing = [x for x in REQUIRED_COMMANDS if not self.exists(x, executable=True)]
        if missing:
            for path in missing: self.fail('Runtime commands', path)
        else:
            self.pass_('Runtime commands', f'{len(REQUIRED_COMMANDS)} required executables present')
        for name in ('sysinit', 'boot', 'default', 'shutdown'):
            d = self.root / 'etc/runlevels' / name
            if not d.is_dir():
                self.fail('Runlevels', f'no {name} directory')
                continue
            links = list(d.iterdir())
            broken = [p.name for p in links if p.is_symlink() and not self.exists('/etc/init.d/' + p.name)]
            if broken: self.fail('Runlevels', f'{name}: broken registrations {broken}')
            else: self.pass_('Runlevels', f'{name}: {sum(p.is_symlink() for p in links)} links checked')

    def check_security(self):
        for item in SELINUX_FILES:
            try:
                path = self.virtual(item)
                if not path.is_file() or path.is_symlink() or path.stat().st_size == 0:
                    self.fail('SELinux policy', f'missing/unsafe {item}')
                else: self.pass_('SELinux policy', item)
            except (OSError, RuntimeError): self.fail('SELinux policy', f'unreadable {item}')
        config = self.root / 'etc/selinux/config'
        if config.is_file():
            values = [x.strip() for x in config.read_text().splitlines() if x.strip().startswith('SELINUX=')]
            if values == ['SELINUX=enforcing']:
                self.pass_('SELinux mode', 'exactly SELINUX=enforcing')
            else:
                self.fail('SELinux mode', f'expected only SELINUX=enforcing, found {values}')
        context = self.root / 'etc/selinux/openwave/contexts/files/file_contexts'
        if context.is_file():
            content = context.read_text()
            for fragment in ('/System/Services/openwave-register-service-identity', '/System(/.*)?', '/dev/dri'):
                if fragment not in content: self.warn('SELinux file_contexts', f'missing pattern {fragment}')
        for path in ('/init', '/System/Services/appd', '/System/Services/permissiond'):
            if not self.exists(path): continue
            try:
                label = os.getxattr(self.virtual(path), 'security.selinux').decode('ascii', errors='replace')
                self.pass_('SELinux staging labels', f'{path}: {label}')
            except OSError:
                self.warn('SELinux staging labels', f'{path}: no readable SELinux xattr (verify labels in packed image/guest)')
        for item in DBUS_POLICY_FILES:
            if self.exists(item): self.pass_('D-Bus policy', item)
            else: self.fail('D-Bus policy', f'missing {item}')
        if self.exists('/etc/passwd'):
            passwd = self.virtual('/etc/passwd').read_text()
            if re.search(r'^ocean:[^:]*:1000:1000:', passwd, re.M): self.pass_('User identity', 'ocean UID/GID 1000')
            else: self.fail('User identity', 'ocean UID/GID 1000 absent')
        else: self.fail('User identity', '/etc/passwd missing')

    def plugin_audit(self):
        found = defaultdict(list)
        for dirpath, dirs, files in os.walk(self.root, followlinks=False):
            for name in files:
                if name in ('libqeglfs.so', 'libqeglfs-kms-integration.so',
                            'libpipewire-module-protocol-native.so',
                            'libwireplumber-module-default-nodes-api.so', 'wpctl', 'qmldir'):
                    found[name].append(str(Path(dirpath, name).relative_to(self.root)))
        for name in ('libqeglfs.so', 'libqeglfs-kms-integration.so',
                     'libpipewire-module-protocol-native.so'):
            if found[name]: self.pass_('Runtime modules', f'{name}: {found[name][0]}')
            else: self.fail('Runtime modules', f'{name} missing')
        if self.exists('/usr/bin/wpctl', executable=True): self.pass_('WirePlumber', 'wpctl executable present')
        else: self.warn('WirePlumber', 'wpctl missing (audio controls may fail)')
        for module in QML_MODULES:
            expected = f'/usr/qml/OpenWave/{module}/qmldir'
            if self.exists(expected): self.pass_('QML', f'OpenWave.{module}')
            else: self.warn('QML', f'OpenWave.{module} not found at {expected}')
        for rel in ('/etc/openwave/kms.json', '/etc/udev/rules.d/70-openwave-drm.rules'):
            if self.exists(rel): self.pass_('Graphics configuration', rel)
            else: self.fail('Graphics configuration', f'{rel} missing')

    def elf_audit(self):
        readelf = self.repo / 'toolchain/bin/llvm-readelf'
        if not readelf.is_file():
            self.fail('ELF audit', f'no target llvm-readelf: {readelf}')
            return
        print('\nScanning target ELF files throughout the rootfs (this may take a little time)...', flush=True)
        elf_paths = []
        known_locations = defaultdict(list)
        for dirpath, dirs, files in os.walk(self.root, followlinks=False):
            for filename in files:
                path = Path(dirpath, filename)
                if path.is_symlink() or not path.is_file(): continue
                try:
                    with path.open('rb') as f:
                        if f.read(4) != b'\x7fELF': continue
                except OSError: continue
                relative = '/' + path.relative_to(self.root).as_posix()
                elf_paths.append((relative, path))
                known_locations[filename].append(relative)
        if not elf_paths:
            self.fail('ELF audit', 'no ELF objects found')
            return
        fail_items, warning_items = [], []
        edge_count, interpreter_count = 0, 0
        for guest, path in elf_paths:
            proc = subprocess.run([str(readelf), '-h', '-l', '-d', str(path)],
                                  capture_output=True, text=True)
            if proc.returncode:
                fail_items.append(f'{guest}: llvm-readelf error {proc.stderr.strip()[:150]}')
                continue
            output = proc.stdout
            if not re.search(r'Machine:\s+Advanced Micro Devices X86-64', output):
                fail_items.append(f'{guest}: ELF machine is not x86-64')
            interp = re.findall(r'Requesting program interpreter:\s*([^\]]+)\]', output)
            for i in interp:
                interpreter_count += 1
                if not self.exists(i): fail_items.append(f'{guest}: missing ELF interpreter {i}')
            paths = list(LIB_DIRS)
            dynamic = re.findall(r'\((?:RUNPATH|RPATH)\)\s+Library (?:run)?path:\s*\[([^]]+)\]', output)
            for value in dynamic:
                for segment in value.split(':'):
                    segment = segment.replace('${ORIGIN}', os.path.dirname(guest)).replace('$ORIGIN', os.path.dirname(guest))
                    segment = segment.replace('${LIB}', 'lib64').replace('$LIB', 'lib64')
                    if segment.startswith('/'): paths.insert(0, segment)
            for soname in re.findall(r'\(NEEDED\)\s+Shared library:\s*\[([^]]+)\]', output):
                edge_count += 1
                if '/' in soname:
                    matches = self.exists(soname) if soname.startswith('/') else False
                else:
                    matches = any(self.exists(d.rstrip('/') + '/' + soname) for d in paths)
                if matches: continue
                if soname in known_locations:
                    warning_items.append(f'{guest}: {soname} exists only outside standard/direct RUNPATH search dirs ({known_locations[soname][0]})')
                else:
                    fail_items.append(f'{guest}: missing dependency {soname}')
        if fail_items:
            self.fail('ELF dependencies', f'{len(fail_items)} issues across {len(elf_paths)} ELFs / {edge_count} links')
            for entry in fail_items[:60]: print('       -', entry)
            if len(fail_items) > 60: print(f'       ... and {len(fail_items)-60} more')
        else:
            self.pass_('ELF dependencies', f'{len(elf_paths)} ELFs; {edge_count} dependencies; {interpreter_count} interpreters; no missing files')
        if warning_items:
            self.warn('ELF search paths', f'{len(warning_items)} nonstandard-location dependencies (loader verification needed)')
            for entry in warning_items[:30]: print('       -', entry)
            if len(warning_items) > 30: print(f'       ... and {len(warning_items)-30} more')
        else:
            self.pass_('ELF search paths', 'all detected names resolved through standard/direct RUNPATH directories')

    def finish(self):
        print('\n' + '=' * 68)
        print('OPENWAVE HOST READINESS SUMMARY')
        print(f"PASS={self.counts['PASS']} WARN={self.counts['WARN']} FAIL={self.counts['FAIL']}")
        print('Static rootfs audit only. It does NOT prove SELinux file labels,')
        print('D-Bus ownership, Qt plugin loading, or OpenRC startup in the guest.')
        if self.counts['FAIL']:
            print('RESULT: BLOCKED — resolve FAIL entries before an enforcing boot test.')
            return 1
        if self.counts['WARN']:
            print('RESULT: STATIC CHECKS PASSED WITH WARNINGS — investigate warnings.')
            return 0
        print('RESULT: STATIC CHECKS PASSED — proceed to controlled guest checks.')
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo', type=Path, default=Path.cwd())
    ap.add_argument('--rootfs', type=Path)
    args = ap.parse_args()
    repo = args.repo.absolute()
    root = (args.rootfs or repo / 'rootfs').absolute()
    if not (repo / 'userspace/openrc').is_dir() or not root.is_dir():
        print('Missing repository userspace/openrc or rootfs directory', file=sys.stderr)
        return 2
    audit = Audit(repo, root)
    print(f'OpenWave repository: {repo}\nRootfs: {root}\nREAD-ONLY: no files will be modified.\n')
    audit.run_validator()
    audit.check_core()
    audit.check_security()
    audit.plugin_audit()
    audit.elf_audit()
    return audit.finish()

if __name__ == '__main__':
    sys.exit(main())
