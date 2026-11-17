#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).with_name('engine-launcher.c')


class EngineLauncherTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = Path(tempfile.mkdtemp())
        cls.apps = cls.work / 'Applications'
        cls.bundle = cls.apps / 'com.example.engine'
        (cls.bundle / 'sbin').mkdir(parents=True)
        for name in ('WPEWebProcess', 'WPENetworkProcess', 'WPEGPUProcess', 'other'):
            helper = cls.bundle / 'sbin' / name
            helper.write_text('#!/bin/sh\necho "$0|$*"\n')
            helper.chmod(0o755)
        cls.launcher = cls.work / 'launcher'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', f'-DOPENWAVE_ENGINE_LAUNCHER_TEST_PREFIX="{cls.apps}/"',
                        '-o', str(cls.launcher), str(SOURCE)], check=True)
        cls.names = {}
        for name in ('WPEWebProcess', 'WPENetworkProcess', 'WPEGPUProcess', 'other'):
            link = cls.work / 'usr' / name
            link.parent.mkdir(exist_ok=True)
            shutil.copy2(cls.launcher, link)
            cls.names[name] = link

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work)

    def run_helper(self, name, root, *arguments):
        env = {'PATH': os.environ['PATH']}
        if root is not None:
            env['OPENWAVE_WEB_ENGINE_ROOT'] = str(root)
        return subprocess.run([str(self.names[name]), *arguments], capture_output=True, text=True, env=env)

    def test_starts_the_bundles_helper_of_the_same_name_with_the_same_arguments(self):
        for name in ('WPEWebProcess', 'WPENetworkProcess', 'WPEGPUProcess'):
            result = self.run_helper(name, self.bundle, '1', '--x')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), f'{self.bundle}/sbin/{name}|1 --x')

    def test_refuses_a_name_that_is_not_an_engine_helper(self):
        result = self.run_helper('other', self.bundle)
        self.assertEqual(result.returncode, 127)
        self.assertEqual(result.stdout, '')

    def test_refuses_without_or_with_a_bad_engine_root(self):
        for root in (None, '', '/usr', '/tmp', self.apps, f'{self.apps}/../etc', f'{self.bundle}/sbin',
                     f'{self.apps}/com.example.engine/../..', '/System/Applications', 'relative/path'):
            with self.subTest(root=root):
                result = self.run_helper('WPEWebProcess', root)
                self.assertEqual(result.returncode, 127)
                self.assertEqual(result.stdout, '')

    def test_missing_helper_in_the_bundle_fails(self):
        empty = self.apps / 'com.example.empty'
        empty.mkdir()
        self.assertEqual(self.run_helper('WPEWebProcess', empty).returncode, 127)


if __name__ == '__main__':
    unittest.main()
