"""Build command safety and failure propagation without compiling dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BuildCommandsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'project with spaces'
        self.root.mkdir()
        for path in ('build-openwave.sh', 'env.sh', 'scripts/configure-openwave-cmake.py'):
            destination = self.root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, destination)
        self.sdk = self.root / 'sdk'
        (self.sdk / 'sysroot/x86_64-unknown-openwave/usr/include').mkdir(parents=True)
        (self.sdk / 'bin').mkdir()
        for name in ('clang', 'clang++'):
            compiler = self.sdk / 'bin' / name
            compiler.write_text('#!/bin/sh\nexit 0\n')
            compiler.chmod(0o755)
        self.tools = self.root / 'host-tools'
        self.tools.mkdir()
        self.log = self.root / 'commands.jsonl'
        cmake = self.tools / 'cmake'
        cmake.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ['COMMAND_LOG'], 'a') as log:
    log.write(json.dumps(args) + '\\n')
phase = 'build' if '--build' in args else 'install' if '--install' in args else 'configure'
sdk_build = any('/sdk-x86_64-unknown-openwave' in arg for arg in args)
sys.exit(23 if sdk_build and phase == os.environ.get('FAIL_PHASE') else 0)
''')
        cmake.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(OPENWAVE_SDK=str(self.sdk),
                        OPENWAVE_TARGET_TRIPLE='x86_64-unknown-openwave',
                        PATH=str(self.tools) + os.pathsep + self.env['PATH'],
                        COMMAND_LOG=str(self.log))
        self.env.pop('FAIL_PHASE', None)

    def command(self, *args, **overrides):
        return subprocess.run([str(self.root / 'build-openwave.sh'), *args],
                              cwd=self.root.parent, env=self.env | overrides,
                              capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_help_does_not_require_or_create_sdk_staging(self):
        result = self.command('userspace', '--help', OPENWAVE_SDK='/missing-sdk')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('userspace [--image PATH]', result.stdout)
        self.assertFalse((self.root / 'out').exists())

    def test_invalid_arguments_fail_before_building(self):
        for args in (('unknown',), ('sdk', 'extra'), ('userspace', '--image'),
                     ('browser-engine', 'invalid'), ('image',), ('test-appd',)):
            with self.subTest(args=args):
                result = self.command(*args, OPENWAVE_SDK='/missing-sdk')
                self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.root / 'out').exists())

    def test_sdk_build_uses_project_root_from_another_directory(self):
        result = self.command('sdk')
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 3)
        self.assertIn(str(self.root / 'userspace/sdk'), calls[0])
        self.assertIn('-DCMAKE_INSTALL_PREFIX=' + str(self.sdk), calls[0])
        self.assertIn('--build', calls[1])
        self.assertIn('--install', calls[2])

    def test_sdk_failure_stops_before_installation(self):
        for phase, expected in (('configure', 1), ('build', 2)):
            with self.subTest(phase=phase):
                self.log.unlink(missing_ok=True)
                result = self.command('sdk', FAIL_PHASE=phase)
                self.assertEqual(result.returncode, 23, result.stderr)
                self.assertEqual(len(self.calls()), expected)
                self.assertFalse(any('--install' in call for call in self.calls()))

    def test_nested_sdk_failure_stops_userspace_and_packing(self):
        image = self.root / 'new.img'
        result = self.command('userspace', '--image', str(image), FAIL_PHASE='build')
        self.assertEqual(result.returncode, 23, result.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 5)
        self.assertFalse(any(str(self.root / 'userspace') in call for call in calls))
        self.assertFalse(any('--install' in call and any('/sdk-x86_64-unknown-openwave' in arg for arg in call) for call in calls))
        self.assertFalse(image.exists())

    def test_existing_images_are_preserved(self):
        image = self.root / 'existing.img'
        image.write_bytes(b'existing data')
        runtime = self.root / 'runtime'
        (runtime / 'usr/bin').mkdir(parents=True)
        (runtime / 'init').touch()
        (runtime / 'usr/bin/ocean-launcher').touch()
        commands = (('userspace', '--image', str(image)),
                    ('image', str(runtime), str(image)),
                    ('test-appd', str(image)), ('test-browser-input', str(image)))
        for args in commands:
            with self.subTest(args=args):
                result = self.command(*args)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn('Refusing to overwrite', result.stderr)
                self.assertEqual(image.read_bytes(), b'existing data')
        self.assertEqual(self.calls(), [])


if __name__ == '__main__':
    unittest.main()
