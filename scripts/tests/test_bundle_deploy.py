#!/usr/bin/env python3
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import openwave_bundle_deploy as deploy

INSTALLED = '/Applications/com.example.engine'


def needed(path):
    out = subprocess.run(['readelf', '-d', str(path)], capture_output=True, text=True, check=True).stdout
    return re.findall(r'\(NEEDED\).*?\[(.*?)\]', out)


def runpath(path):
    return subprocess.run(['patchelf', '--print-rpath', str(path)], capture_output=True, text=True, check=True).stdout.strip()


@unittest.skipUnless(shutil.which('cc') and shutil.which('patchelf') and shutil.which('readelf'), 'needs cc, patchelf, readelf')
class BundleDeployTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.work)
        src = self.work / 'src'
        src.mkdir()
        (src / 'a.c').write_text('int a(void){return 1;}\n')
        (src / 'b.c').write_text('int a(void); int b(void){return a()+1;}\n')
        (src / 'c.c').write_text('int c(void){return 3;}\n')
        (src / 'main.c').write_text('int b(void); int c(void); int main(void){return b()+c()-5;}\n')
        rt = self.work / 'rt'
        lib = rt / 'usr/lib64'
        lib.mkdir(parents=True)
        cc = ['cc', '-shared', '-fPIC']
        subprocess.run(cc + ['-Wl,-soname,liba.so.1', '-o', str(lib / 'liba.so.1.0'), str(src / 'a.c')], check=True)
        os.symlink('liba.so.1.0', lib / 'liba.so.1')
        subprocess.run(cc + ['-Wl,-soname,libc3.so.1', '-o', str(lib / 'libc3.so.1'), str(src / 'c.c')], check=True)
        subprocess.run(cc + ['-Wl,-soname,libengine.so.1', '-o', str(lib / 'libengine.so.1'), str(src / 'b.c'),
                             f'-L{lib}', '-l:liba.so.1'], check=True)
        (rt / 'usr/libexec').mkdir()
        subprocess.run(['cc', '-o', str(rt / 'usr/libexec/helper'), str(src / 'main.c'), f'-L{lib}', '-l:libengine.so.1',
                        '-l:libc3.so.1', '-Wl,--allow-shlib-undefined'], check=True)
        (rt / 'usr/lib64/plugins').mkdir()
        shutil.copy2(lib / 'libc3.so.1', rt / 'usr/lib64/plugins/libplugin.so')
        self.rt, self.lib = rt, lib
        self.payload = [('usr/lib64/libengine.so.1', 'lib64/libengine.so.1'),
                        ('usr/libexec/helper', 'sbin/helper'),
                        ('usr/lib64/plugins/*.so', 'plugins/p/')]
        self.bundle = self.work / 'bundle'
        for directory in ('lib64', 'sbin', 'plugins', 'qml'):
            (self.bundle / directory).mkdir(parents=True)

    def test_payload_is_copied_with_absolute_runpath_and_without_symlinks(self):
        copied = deploy.deploy_payload(self.bundle, INSTALLED, [self.rt], payload=self.payload)
        self.assertEqual({p.relative_to(self.bundle).as_posix() for p in copied},
                         {'lib64/libengine.so.1', 'sbin/helper', 'plugins/p/libplugin.so'})
        for relative in ('lib64/libengine.so.1', 'sbin/helper', 'plugins/p/libplugin.so'):
            self.assertEqual(runpath(self.bundle / relative), f'{INSTALLED}/lib64:/usr/lib64:/usr/lib')
        self.assertFalse(any(p.is_symlink() for p in self.bundle.rglob('*')))
        self.assertEqual(os.stat(self.bundle / 'sbin/helper').st_mode & 0o111, 0o111)

    def test_existing_files_are_kept_and_a_missing_one_is_an_error(self):
        deploy.deploy_payload(self.bundle, INSTALLED, [self.rt], payload=self.payload)
        self.assertEqual(deploy.deploy_payload(self.bundle, INSTALLED, [self.work / 'empty'], payload=self.payload), [])
        with self.assertRaises(RuntimeError):
            deploy.deploy_payload(self.work / 'other', INSTALLED, [self.work / 'empty'], payload=self.payload)

    def test_closure_copies_only_what_the_base_lacks(self):
        deploy.deploy_payload(self.bundle, INSTALLED, [self.rt], payload=self.payload)
        base = self.work / 'base'
        base.mkdir()
        shutil.copy2(self.lib / 'libc3.so.1', base / 'libc3.so.1')
        libraries = {'liba.so.1': self.lib / 'liba.so.1', 'libc3.so.1': self.lib / 'libc3.so.1'}
        libc = Path(subprocess.run(['cc', '-print-file-name=libc.so.6'], capture_output=True, text=True).stdout.strip())
        for name in needed(self.bundle / 'lib64/libengine.so.1') + needed(self.bundle / 'sbin/helper'):
            if name not in libraries and name != 'libengine.so.1':
                libraries[name] = libc.parent / name if (libc.parent / name).exists() else libc
        copied = deploy.private_closure(self.bundle, INSTALLED, libraries, [base, Path('/lib/x86_64-linux-gnu')]
                                        + [Path(p) for p in ('/usr/lib64', '/lib64', '/usr/lib', '/lib')], needed)
        names = {p.name for p in copied}
        self.assertIn('liba.so.1', names)
        self.assertNotIn('libc3.so.1', names)
        self.assertEqual(runpath(self.bundle / 'lib64/liba.so.1'), f'{INSTALLED}/lib64:/usr/lib64:/usr/lib')
        self.assertFalse((self.bundle / 'lib64/liba.so.1').is_symlink())

    def test_a_library_nobody_provides_is_an_error(self):
        deploy.deploy_payload(self.bundle, INSTALLED, [self.rt], payload=self.payload)
        with self.assertRaises(RuntimeError):
            deploy.private_closure(self.bundle, INSTALLED, {}, [], needed)


if __name__ == '__main__':
    unittest.main()
