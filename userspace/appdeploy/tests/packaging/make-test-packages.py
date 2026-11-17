#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import textwrap

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
SDK = ROOT / 'toolchain'
SYSROOT = SDK / 'sysroot' / 'x86_64-unknown-openwave'

APP_TOML = '''\
schema = 1
id = "{id}"
name = "{name}"
version = "{version}"
kind = "{kind}"
{executable}runtime = "openwave-qt"
single_instance = true
accepts_paths = false

[sandbox]
profile = "standard"

[permissions]
request = [{permissions}]
'''


class Package:
    def __init__(self, name, version='1.0.0', release='1', kind='application', key='app', requires=(),
                 files=None, extra_spec='', klass='application', arch='x86_64', toml=None, summary=None,
                 provides_class=True, conflicts=(), permissions=(), embed=None):
        self.name, self.version, self.release, self.kind, self.key = name, version, release, kind, key
        self.requires, self.files, self.extra_spec, self.klass = list(requires), files or {}, extra_spec, klass
        self.arch, self.toml, self.summary = arch, toml, summary or f'{name} (appdeploy test package)'
        self.provides_class, self.conflicts, self.permissions = provides_class, list(conflicts), list(permissions)
        self.embed = embed

    @property
    def filename(self):
        return f'{self.name}-{self.version}-{self.release}.{self.arch}.rpm'


def run(command, **kwargs):
    kwargs.setdefault('check', True)
    kwargs.setdefault('stdout', subprocess.PIPE)
    kwargs.setdefault('stderr', subprocess.STDOUT)
    kwargs.setdefault('text', True)
    result = subprocess.run(command, **kwargs)
    return result


class Builder:
    def __init__(self, out, host):
        self.out, self.host = out, host
        self.env = dict(os.environ)
        lib = host / 'usr/lib/x86_64-linux-gnu'
        self.env.update(LD_LIBRARY_PATH=f'{lib}:' + self.env.get('LD_LIBRARY_PATH', ''),
                        PATH=f'{host}/usr/bin:' + self.env['PATH'], RPM_CONFIGDIR=str(host / 'usr/lib/rpm'),
                        GNUPGHOME=str(out / 'gnupg'), HOME=str(out / 'home'))
        (out / 'home').mkdir(parents=True, exist_ok=True)
        self.keys = {}

    def make_keys(self):
        home = self.out / 'gnupg'
        home.mkdir(mode=0o700, parents=True, exist_ok=True)
        for tag, uid in (('app', 'AppDeploy Test Application Key <app@appdeploy.test>'),
                         ('system', 'AppDeploy Test System Key <system@appdeploy.test>'),
                         ('untrusted', 'AppDeploy Test Untrusted Key <untrusted@appdeploy.test>'),
                         ('rival', 'AppDeploy Test Rival Key <rival@appdeploy.test>')):
            marker = home / f'{tag}.fpr'
            if not marker.exists():
                run(['gpg', '--batch', '--pinentry-mode', 'loopback', '--passphrase', '',
                     '--quick-generate-key', uid, 'rsa3072', 'sign', 'never'], env=self.env)
                listing = run(['gpg', '--batch', '--with-colons', '--fingerprint', uid], env=self.env).stdout
                fpr = next(line.split(':')[9] for line in listing.splitlines() if line.startswith('fpr:'))
                marker.write_text(fpr + '\n')
            fpr = marker.read_text().strip()
            self.keys[tag] = fpr
            armored = run(['gpg', '--batch', '--armor', '--export', fpr], env=self.env).stdout
            directory = self.out / 'keys' / {'app': 'application', 'system': 'system', 'untrusted': 'untrusted', 'rival': 'rival'}[tag]
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f'{tag}.asc').write_text(armored)
            (directory / f'{tag}.asc').chmod(0o644)
            directory.chmod(0o755)
            directory.parent.chmod(0o755)

    def compile_hello(self):
        binary = self.out / 'build' / 'hello'
        binary.parent.mkdir(parents=True, exist_ok=True)
        run([str(SDK / 'bin/clang'), '--target=x86_64-unknown-openwave', f'--sysroot={SYSROOT}', '-O2', '-fPIC',
             '-Wl,-rpath,$ORIGIN/../lib:/usr/lib64:/usr/lib', str(HERE / 'hello.c'), '-o', str(binary)])
        return binary

    def toml_for(self, pkg, executable=True):
        if pkg.toml is not None:
            return pkg.toml
        executable_line = 'executable = "bin/hello"\n' if executable and pkg.kind == 'application' else ''
        permissions = ', '.join(f'"{p}"' for p in pkg.permissions)
        return APP_TOML.format(id=pkg.name, name=pkg.name.split('.')[-1].title(), version=pkg.version,
                               kind=pkg.kind, executable=executable_line, permissions=permissions)

    def build(self, pkg, hello):
        top = self.out / 'build' / f'rpm-{pkg.name}-{pkg.version}'
        shutil.rmtree(top, ignore_errors=True)
        for sub in ('BUILD', 'RPMS', 'SOURCES', 'SPECS', 'SRPMS', 'BUILDROOT'):
            (top / sub).mkdir(parents=True)
        stage = top / 'stage'
        files = dict(pkg.files)
        if pkg.klass == 'application':
            base = f'/Applications/{pkg.name}'
            files.setdefault(f'{base}/Application.toml', ('text', self.toml_for(pkg)))
            if pkg.kind == 'application':
                files.setdefault(f'{base}/bin/hello', ('file', hello, 0o755))
            if pkg.embed:
                directory = {'app': 'application', 'system': 'system', 'untrusted': 'untrusted', 'rival': 'rival'}[pkg.embed]
                files.setdefault(f'{base}/share/openwave/publisher.asc',
                                 ('text', (self.out / 'keys' / directory / f'{pkg.embed}.asc').read_text()))
        listing = []
        for path, entry in files.items():
            target = stage / path.lstrip('/')
            target.parent.mkdir(parents=True, exist_ok=True)
            mode = 0o644
            if entry[0] == 'text':
                target.write_text(entry[1])
            elif entry[0] == 'file':
                shutil.copy2(entry[1], target)
                mode = entry[2]
            elif entry[0] == 'link':
                os.symlink(entry[1], target)
            if entry[0] != 'link':
                os.chmod(target, mode)
                listing.append(f'%attr({mode:04o},root,root) "{path}"')
            else:
                listing.append(f'%attr(-,root,root) "{path}"')
        owned = set()
        for path in files:
            parts = Path(path).parts[1:-1]
            root = {'application': 2, 'system': 2}[pkg.klass]
            for index in range(root, len(parts) + 1):
                owned.add('/' + '/'.join(parts[:index]))
        if pkg.klass == 'application':
            owned.add(f'/Applications/{pkg.name}')
        dirs = [f'%attr(0755,root,root) %dir "{d}"' for d in sorted(owned, key=lambda d: (d.count('/'), d))
                if d.count('/') >= 2]
        requires = ''.join(f'Requires: {r}\n' for r in pkg.requires)
        conflicts = ''.join(f'Conflicts: {c}\n' for c in pkg.conflicts)
        provides = ''
        if pkg.provides_class:
            provides = (f'Provides: openwave-application({pkg.name}) = {pkg.version}\n' if pkg.klass == 'application'
                        else f'Provides: openwave-system-package({pkg.name}) = {pkg.version}\n')
        spec = textwrap.dedent(f'''\
            %global debug_package %{{nil}}
            %global __os_install_post %{{nil}}
            %global _build_id_links none
            AutoReqProv: no
            Name: {pkg.name}
            Version: {pkg.version}
            Release: {pkg.release}
            Summary: {pkg.summary}
            License: MIT
            BuildArch: {pkg.arch}
            ''') + provides + requires + conflicts + textwrap.dedent('''\
            %description
            Test package for OpenWave appdeploy.
            ''') + pkg.extra_spec + '\n%install\ncp -a ' + str(stage) + '/. %{buildroot}/\n\n%files\n' + \
            '\n'.join(dirs + listing) + '\n'
        spec_path = top / 'SPECS' / f'{pkg.name}.spec'
        spec_path.write_text(spec)
        run(['rpmbuild', '-bb', '--define', f'_topdir {top}', '--define', '_rpmformat 4',
             '--define', '_binary_payload w9.gzdio', '--define', '_buildhost appdeploy-test',
             '--define', 'source_date_epoch_from_changelog 0', '--define', 'use_source_date_epoch_as_buildtime 1',
             '--define', '_source_date_epoch 1700000000', '--define', '_binary_filedigest_algorithm 8',
             str(spec_path)], env=self.env)
        built = next((top / 'RPMS').rglob('*.rpm'))
        destination = self.out / 'packages' / 'unsigned' / pkg.filename
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built, destination)
        return destination

    def sign(self, rpm, tag, destination):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(rpm, destination)
        if tag is None:
            return destination
        run(['rpmsign', '--addsign', '--define', f'_gpg_name {self.keys[tag]}',
             '--define', f'_gpg_path {self.out / "gnupg"}',
             str(destination)], env=self.env)
        return destination


def packages():
    hello_files = {}
    p = []
    p.append(Package('org.openwave.test.hello', '1.0.0'))
    p.append(Package('org.openwave.test.hello', '2.0.0', permissions=['files.read']))
    p.append(Package('org.openwave.test.lib', '1.0.0', kind='library',
                     files={'/Applications/org.openwave.test.lib/lib/libappdeploytest.so.1': ('text', 'library payload\n')}))
    p.append(Package('org.openwave.test.viewer', '1.0.0', requires=['org.openwave.test.lib >= 1.0']))
    p.append(Package('org.openwave.test.cleanup', '1.0.0', kind='library', files={
        '/Applications/org.openwave.test.cleanup/share/data/deep/er/file.txt': ('text', 'x\n'),
        '/Applications/org.openwave.test.cleanup/resources/a.txt': ('text', 'a\n')}))
    p.append(Package('openwave-test-sys-a', '1.0.0', klass='system', key='system',
                     files={'/opt/openwave-test/shared.txt': ('text', 'from a\n')}))
    p.append(Package('openwave-test-sys-b', '1.0.0', klass='system', key='system',
                     files={'/opt/openwave-test/shared.txt': ('text', 'from b\n')}))
    p.append(Package('openwave-test-sys-unowned', '1.0.0', klass='system', key='system',
                     files={'/opt/openwave-test/unowned.txt': ('text', 'overwrites\n')}))
    p.append(Package('openwave-test-sys-bad', '1.0.0', klass='system', key='app',
                     files={'/opt/openwave-test/signed-by-application-key.txt': ('text', 'x\n')}))
    p.append(Package('openwave-test-sys-etc', '1.0.0', klass='system', key='system',
                     files={'/etc/selinux/config': ('text', 'SELINUX=disabled\n')}))
    p.append(Package('org.openwave.test.badtoml', '1.0.0', toml='schema = 1\nid = "org.openwave.test.badtoml"\n'
                     'name = "Bad"\nversion = "1.0.0"\nkind = "application"\nexecutable = "../../etc/passwd"\n'
                     'runtime = "openwave-qt"\n[sandbox]\nprofile = "standard"\n[permissions]\nrequest = []\n'))
    p.append(Package('org.openwave.test.mismatch', '1.0.0', toml=APP_TOML.format(
        id='org.openwave.test.other', name='Other', version='1.0.0', kind='application',
        executable='executable = "bin/hello"\n', permissions='')))
    p.append(Package('org.openwave.test.scriptlet', '1.0.0', extra_spec='%post -p <lua>\nprint("scriptlet")\n'))
    p.append(Package('org.openwave.test.scriptlet-sh', '1.0.0', extra_spec='%post\ntouch /tmp/appdeploy-test-scriptlet\n'))
    p.append(Package('org.openwave.test.trigger', '1.0.0', extra_spec='%triggerin -p <lua> -- bash\nprint("trigger")\n'))
    p.append(Package('org.openwave.test.filetrigger', '1.0.0', extra_spec='%filetriggerin -p <lua> -- /usr/lib\nprint("filetrigger")\n'))
    p.append(Package('org.openwave.test.symlink', '1.0.0', files={
        '/Applications/org.openwave.test.symlink/resources/escape': ('link', '/etc/passwd')}))
    p.append(Package('org.openwave.test.dotdot', '1.0.0', files={
        '/Applications/org.openwave.test.dotdot/resources/up': ('link', '../../../etc')}))
    p.append(Package('org.openwave.test.outside', '1.0.0', files={
        '/Applications/org.openwave.test.hello/bin/hello': ('text', 'replacement\n')}))
    p.append(Package('org.openwave.test.suid', '1.0.0', files={
        '/Applications/org.openwave.test.suid/bin/hello': ('file', None, 0o4755)}))
    p.append(Package('org.openwave.test.manifest', '1.0.0', files={
        '/Applications/org.openwave.test.manifest/manifest.json': ('text', '{"schemaVersion": 1}\n')}))
    p.append(Package('org.openwave.test.bundle', '1.0.0', files={
        '/Applications/org.openwave.test.bundle/sbin/helper': ('file', None, 0o755),
        '/Applications/org.openwave.test.bundle/lib/libbundle.so.1': ('text', 'lib\n'),
        '/Applications/org.openwave.test.bundle/lib64/libbundle64.so.1': ('text', 'lib64\n'),
        '/Applications/org.openwave.test.bundle/plugins/platforms/libplugin.so': ('text', 'plugin\n'),
        '/Applications/org.openwave.test.bundle/qml/Bundle/qmldir': ('text', 'module Bundle\n'),
        '/Applications/org.openwave.test.bundle/resources/images/a.png': ('text', 'png\n'),
        '/Applications/org.openwave.test.bundle/resources/sounds/a.ogg': ('text', 'ogg\n'),
        '/Applications/org.openwave.test.bundle/resources/fonts/a.ttf': ('text', 'ttf\n'),
        '/Applications/org.openwave.test.bundle/share/icons/icon.svg': ('text', '<svg/>\n'),
        '/Applications/org.openwave.test.bundle/share/translations/de.qm': ('text', 'qm\n'),
        '/Applications/org.openwave.test.bundle/share/data/x.txt': ('text', 'x\n')}))
    p.append(Package('org.openwave.test.layout', '1.0.0', files={
        '/Applications/org.openwave.test.layout/extras/x.txt': ('text', 'not a standard directory\n')}))
    p.append(Package('org.openwave.test.noclass', '1.0.0', provides_class=False))
    p.append(Package('org.openwave.test.reserved', '1.0.0', toml=APP_TOML.format(
        id='com.openwave.test.reserved', name='Reserved', version='1.0.0', kind='application',
        executable='executable = "bin/hello"\n', permissions='')))
    p.append(Package('org.openwave.test.untrusted', '1.0.0', key='untrusted'))
    p.append(Package('org.openwave.test.pub', '1.0.0', key='untrusted', embed='untrusted'))
    p.append(Package('org.openwave.test.pub', '2.0.0', key='untrusted', embed='untrusted'))
    p.append(Package('org.openwave.test.pub', '3.0.0', key='rival', embed='rival'))
    p.append(Package('org.openwave.test.pubmismatch', '1.0.0', key='rival', embed='untrusted'))
    p.append(Package('org.openwave.test.pubadmin', '1.0.0', key='app', embed='untrusted'))
    p.append(Package('org.openwave.test.unsigned', '1.0.0', key=None))
    return p


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--host-rpm', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    host = (args.host_rpm or out / 'host-rpm').resolve()
    if not (host / 'usr/bin/rpmbuild').exists():
        sys.exit(f'No host RPM tools in {host}; run fetch-host-rpm-tools.sh first')
    out.mkdir(parents=True, exist_ok=True)
    builder = Builder(out, host)
    builder.make_keys()
    hello = builder.compile_hello()
    signed = out / 'packages' / 'signed'
    shutil.rmtree(out / 'packages', ignore_errors=True)
    built = []
    for pkg in packages():
        files = {}
        for path, entry in pkg.files.items():
            if entry[0] == 'file' and entry[1] is None:
                entry = ('file', hello, entry[2])
            files[path] = entry
        pkg.files = files
        rpm = builder.build(pkg, hello)
        destination = builder.sign(rpm, pkg.key, signed / pkg.filename)
        built.append((pkg, destination))
        print(f'built {destination.name} signed-by={pkg.key}')
    source = next(d for p, d in built if p.name == 'org.openwave.test.hello' and p.version == '1.0.0')
    tampered = bytearray(source.read_bytes())
    tampered[-40] ^= 0xFF
    (signed / 'org.openwave.test.tampered-1.0.0-1.x86_64.rpm').write_bytes(bytes(tampered))
    keys = {tag: builder.keys[tag] for tag in builder.keys}
    (out / 'fingerprints.txt').write_text(''.join(f'{tag} {fpr}\n' for tag, fpr in keys.items()))
    print('packages:', signed)


if __name__ == '__main__':
    main()
