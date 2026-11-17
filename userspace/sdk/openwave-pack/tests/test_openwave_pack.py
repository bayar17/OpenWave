import importlib.machinery
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SOURCE = HERE.parent / 'openwave-pack'
SDK = ROOT / 'toolchain'

loader = importlib.machinery.SourceFileLoader('openwave_pack', str(SOURCE))
spec = importlib.util.spec_from_loader('openwave_pack', loader)
pack = importlib.util.module_from_spec(spec)
loader.exec_module(pack)
pack.SDK = SDK

TOML = '''schema = 1
id = "org.example.sample"
name = "Sample"
version = "1.2.0"
kind = "application"
executable = "bin/sample"
runtime = "openwave-qt"

[sandbox]
profile = "standard"

[permissions]
request = []
'''

READY = (SDK / 'sysroot/x86_64-unknown-openwave/usr/bin/appdeploy-check').is_file() \
    and (SDK / 'sysroot/x86_64-unknown-openwave/usr/bin/rpmbuild').is_file() and shutil.which('gpg')


def make_stage(root, toml=TOML, with_manifest=True):
    bundle = root / 'Applications/org.example.sample'
    for directory in ('bin', 'lib64', 'share/icons'):
        (bundle / directory).mkdir(parents=True)
    (bundle / 'Application.toml').write_text(toml)
    (bundle / 'bin/sample').write_text('#!/bin/sh\nexit 0\n')
    (bundle / 'bin/sample').chmod(0o4775)
    (bundle / 'share/icons/icon.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg"/>')
    if with_manifest:
        (bundle / 'manifest.json').write_text('{}')
    return root


@unittest.skipUnless(READY, 'needs the SDK sysroot tools (rpmbuild, appdeploy-check) and gpg')
class PackTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.work, True)

    def listing(self, rpm):
        command = pack.target_command('rpm', ['-qplv', str(rpm)])
        return subprocess.run(command, env=pack.target_environment(), text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout

    def test_package_follows_the_device_policy(self):
        rpm = pack.package(make_stage(self.work / 'stage'), self.work / 'out', '1', 'MIT')
        self.assertEqual(rpm.name, 'org.example.sample-1.2.0-1.noarch.rpm')
        listing = self.listing(rpm)
        self.assertIn('/Applications/org.example.sample/Application.toml', listing)
        self.assertNotIn('manifest.json', listing)
        self.assertNotRegex(listing, r'^-rwsr', 'no setuid bit survives packaging')
        self.assertEqual(pack.check([rpm], None), 0)

    def test_a_bundle_directory_can_be_given_directly(self):
        stage = make_stage(self.work / 'stage')
        rpm = pack.package(stage / 'Applications/org.example.sample', self.work / 'out', '1', 'MIT')
        self.assertTrue(rpm.is_file())

    def test_refusals(self):
        with self.assertRaises(pack.Error):
            pack.package(make_stage(self.work / 'a', TOML.replace('org.example.sample', 'sample')), self.work / 'o', '1', 'MIT')
        with self.assertRaises(pack.Error):
            pack.package(make_stage(self.work / 'b', TOML.replace('1.2.0', '1.2-beta')), self.work / 'o', '1', 'MIT')
        with self.assertRaises(pack.Error):
            pack.package(self.work / 'nothing', self.work / 'o', '1', 'MIT')

    def test_reserved_namespace_is_reported_by_the_policy(self):
        stage = self.work / 'r'
        bundle = stage / 'Applications/com.openwave.mine'
        bundle.mkdir(parents=True)
        (bundle / 'Application.toml').write_text(TOML.replace('org.example.sample', 'com.openwave.mine'))
        rpm = pack.package(stage, self.work / 'out', '1', 'MIT')
        self.assertEqual(pack.check([rpm], None), 1)

    def test_signing_and_trust(self):
        home = self.work / 'gnupg'
        environment = pack.gpg_environment(home)
        subprocess.run(['gpg', '--batch', '--pinentry-mode', 'loopback', '--passphrase', '', '--quick-generate-key',
                        'Test <t@example.org>', 'rsa3072', 'sign', '1d'], env=environment, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        listing = subprocess.run(['gpg', '--batch', '--with-colons', '--fingerprint'], env=environment, text=True,
                                 stdout=subprocess.PIPE).stdout
        fingerprint = next(line.split(':')[9] for line in listing.splitlines() if line.startswith('fpr:'))
        public = self.work / 'key.asc'
        public.write_text(subprocess.run(['gpg', '--batch', '--armor', '--export', fingerprint], env=environment, text=True,
                                         stdout=subprocess.PIPE).stdout)
        rpm = pack.package(make_stage(self.work / 'stage'), self.work / 'out', '1', 'MIT')
        pack.sign([rpm], fingerprint, str(home))
        self.assertEqual(pack.check([rpm], public), 0)
        other = self.work / 'other'
        other.mkdir()
        tampered = other / rpm.name
        data = bytearray(rpm.read_bytes())
        data[-20] ^= 0xFF
        tampered.write_bytes(bytes(data))
        self.assertNotEqual(pack.check([tampered], public), 0)


    def test_a_passphrase_protected_key_signs_without_pinentry(self):
        import argparse
        home = self.work / 'protected'
        public = self.work / 'protected.asc'
        os.environ['OPENWAVE_PASSPHRASE'] = 'correct horse'
        self.addCleanup(os.environ.pop, 'OPENWAVE_PASSPHRASE', None)
        fingerprint = pack.keygen(argparse.Namespace(name='Guarded', email='g@example.org', gnupghome=str(home),
                                                     public_key=str(public), expire='1d', no_passphrase=False))
        rpm = pack.package(make_stage(self.work / 'stage'), self.work / 'out', '1', 'MIT')
        wrong = self.work / 'wrong'
        wrong.write_text('not the passphrase\n')
        with self.assertRaises(pack.Error):
            pack.sign([rpm], fingerprint, str(home), str(wrong))
        pack.sign([rpm], fingerprint, str(home))
        self.assertEqual(pack.check([rpm], public), 0)


    def test_the_package_carries_the_publisher_key_and_the_device_reports_a_first_install(self):
        import contextlib
        import io
        home = self.work / 'gnupg2'
        environment = pack.gpg_environment(home)
        subprocess.run(['gpg', '--batch', '--pinentry-mode', 'loopback', '--passphrase', '', '--quick-generate-key',
                        'Publisher <p@example.org>', 'rsa3072', 'sign', '1d'], env=environment, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        listing = subprocess.run(['gpg', '--batch', '--with-colons', '--fingerprint'], env=environment, text=True,
                                 stdout=subprocess.PIPE).stdout
        fingerprint = next(line.split(':')[9] for line in listing.splitlines() if line.startswith('fpr:'))
        key = pack.export_public_key(fingerprint, str(home))
        rpm = pack.package(make_stage(self.work / 'stage'), self.work / 'out', '1', 'MIT', key)
        self.assertIn(pack.PUBLISHER_KEY, self.listing(rpm))
        pack.sign([rpm], fingerprint, str(home))
        report = io.StringIO()
        with contextlib.redirect_stdout(report):
            status = pack.check([rpm], None)
        self.assertEqual(status, 0)
        self.assertIn('signature publisher', report.getvalue())
        self.assertIn('Publisher <p@example.org>', report.getvalue())
        self.assertIn(fingerprint[-16:].lower(), report.getvalue())


if __name__ == '__main__':
    unittest.main()
