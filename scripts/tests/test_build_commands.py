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
        for path in ('build-openwave', 'env.sh', 'scripts/configure-openwave-cmake.py'):
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
llvm_build = any('/out/llvm-build' in arg for arg in args)
if llvm_build:
    environment = {key: os.environ.get(key) for key in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CMAKE_TOOLCHAIN_FILE', 'PKG_CONFIG_SYSROOT_DIR')}
    Path(os.environ['COMMAND_LOG'] + '.env').write_text(json.dumps(environment))
subject = 'llvm' if llvm_build else 'sdk' if sdk_build else ''
sys.exit(23 if subject == os.environ.get('FAIL_COMPONENT', 'sdk') and phase == os.environ.get('FAIL_PHASE') else 0)
''')
        cmake.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(OPENWAVE_SDK=str(self.sdk),
                        OPENWAVE_TARGET_TRIPLE='x86_64-unknown-openwave',
                        PATH=str(self.tools) + os.pathsep + self.env['PATH'],
                        COMMAND_LOG=str(self.log))
        self.env.pop('FAIL_PHASE', None)
        self.env.pop('FAIL_COMPONENT', None)
        self.llvm = self.root / 'llvm-project'
        self.llvm.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.llvm)], check=True)
        self.source = self.llvm / 'triple.txt'
        self.source.write_text('upstream\n')
        subprocess.run(['git', '-C', str(self.llvm), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.llvm), '-c', 'user.name=Test',
                        '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'Upstream'], check=True)
        self.patch = self.root / 'patches/llvm/openwave-llvm.patch'
        self.patch.parent.mkdir(parents=True)
        self.patch.write_text('diff --git a/triple.txt b/triple.txt\n'
                              '--- a/triple.txt\n+++ b/triple.txt\n'
                              '@@ -1 +1 @@\n-upstream\n+openwave\n')

    def command(self, *args, **overrides):
        return subprocess.run([str(self.root / 'build-openwave'), *args],
                              cwd=self.root.parent, env=self.env | overrides,
                              capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_help_does_not_require_or_create_sdk_staging(self):
        result = self.command('userspace', '--help', OPENWAVE_SDK='/missing-sdk')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('userspace [--image PATH]', result.stdout)
        self.assertFalse((self.root / 'out').exists())
        self.assertEqual(self.source.read_text(), 'upstream\n')

    def test_invalid_arguments_fail_before_building(self):
        for args in (('unknown',), ('sdk', 'extra'), ('userspace', '--image'),
                     ('browser-engine', 'invalid'), ('image',), ('test-appd',),
                     ('llvm', 'extra'), ('all', '--image')):
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
        self.assertEqual(self.source.read_text(), 'openwave\n')

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
        commands = (('userspace', '--image', str(image)), ('all', '--image', str(image)),
                    ('image', str(runtime), str(image)),
                    ('test-appd', str(image)), ('test-browser-input', str(image)))
        for args in commands:
            with self.subTest(args=args):
                result = self.command(*args)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn('Refusing to overwrite', result.stderr)
                self.assertEqual(image.read_bytes(), b'existing data')
        self.assertEqual(self.calls(), [])

    def test_patch_command_does_not_require_sdk(self):
        result = self.command('llvm-patch', OPENWAVE_SDK='/missing-sdk')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_text(), 'openwave\n')
        self.assertEqual(self.calls(), [])

    def test_llvm_build_applies_patch_and_uses_native_environment(self):
        result = self.command('llvm', CC='target-cc', CXX='target-cxx',
                              CFLAGS='--target=openwave', CXXFLAGS='--target=openwave',
                              LDFLAGS='--sysroot=target', CMAKE_TOOLCHAIN_FILE='target.cmake',
                              PKG_CONFIG_SYSROOT_DIR='/target')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_text(), 'openwave\n')
        calls = self.calls()
        self.assertEqual(len(calls), 3)
        self.assertIn('-DLLVM_ENABLE_PROJECTS=clang;lld', calls[0])
        self.assertIn('-DCMAKE_C_COMPILER=/usr/bin/cc', calls[0])
        self.assertIn('-DCMAKE_INSTALL_PREFIX=' + str(self.sdk), calls[0])
        self.assertIn('--install', calls[2])
        environment = json.loads(Path(str(self.log) + '.env').read_text())
        self.assertTrue(all(value is None for value in environment.values()))

    def test_llvm_existing_configuration_is_retained(self):
        cache = self.root / 'out/llvm-build/CMakeCache.txt'
        cache.parent.mkdir(parents=True)
        cache.write_text('LLVM_TARGETS_TO_BUILD:STRING=X86;AArch64\n')
        result = self.command('llvm')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(any(arg.startswith(('-DLLVM_TARGETS_TO_BUILD=', '-DLLVM_ENABLE_PROJECTS=',
                                             '-DCMAKE_C_COMPILER=')) for arg in self.calls()[0]))
        self.assertEqual(cache.read_text(), 'LLVM_TARGETS_TO_BUILD:STRING=X86;AArch64\n')

    def test_llvm_build_failure_does_not_install(self):
        result = self.command('llvm', FAIL_COMPONENT='llvm', FAIL_PHASE='build')
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(len(self.calls()), 2)
        self.assertFalse(any('--install' in call for call in self.calls()))

    def stub_all_stages(self):
        builder = self.root / 'build-openwave'
        logger = self.root / 'log-stage.py'
        logger.write_text('import json, os, sys\n'
                          'with open(os.environ["COMMAND_LOG"], "a") as log:\n'
                          '    log.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                          'sys.exit(23 if sys.argv[1] == os.environ.get("FAIL_STAGE") else 0)\n')
        functions = ''
        for stage in ('llvm', 'browser_engine', 'audio', 'bluetooth_audio', 'mdns', 'userspace'):
            functions += (f'build_{stage}() (\n'
                          f'    python3 "$ROOT/log-stage.py" {stage} "$@"\n)\n')
        builder.write_text(builder.read_text().replace('\nCOMMAND=', '\n' + functions + '\nCOMMAND=', 1))

    def test_all_orders_stages_and_passes_image_to_final_userspace(self):
        self.stub_all_stages()
        image = str(self.root / 'new.img')
        result = self.command('all', '--image', image)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [['llvm'], ['browser_engine', 'deps'], ['audio'],
                                       ['bluetooth_audio'], ['mdns'], ['browser_engine', 'engine'],
                                       ['userspace', '--image', image]])

    def test_all_stops_at_failed_llvm_stage(self):
        self.stub_all_stages()
        result = self.command('all', FAIL_STAGE='llvm')
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(self.calls(), [['llvm']])

    def test_all_stops_at_failed_dependency_stage(self):
        self.stub_all_stages()
        result = self.command('all', FAIL_STAGE='mdns')
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(self.calls()[-1], ['mdns'])
        self.assertFalse(any(call[0] == 'userspace' for call in self.calls()))

    def test_repeated_patch_preserves_local_changes(self):
        result = self.command('llvm-patch')
        self.assertEqual(result.returncode, 0, result.stderr)
        local = self.llvm / 'local.cpp'
        local.write_text('local addition\n')
        result = self.command('llvm-patch')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('already applied', result.stdout)
        self.assertEqual(self.source.read_text(), 'openwave\n')
        self.assertEqual(local.read_text(), 'local addition\n')

    def test_conflicting_patch_stops_before_building(self):
        self.source.write_text('conflicting local change\n')
        result = self.command('sdk')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('conflicts', result.stderr)
        self.assertEqual(self.source.read_text(), 'conflicting local change\n')
        self.assertEqual(self.calls(), [])

    def test_partially_applied_patch_does_not_modify_other_files(self):
        second = self.llvm / 'second.txt'
        second.write_text('upstream\n')
        self.source.write_text('openwave\n')
        self.patch.write_text(self.patch.read_text() +
                              'diff --git a/second.txt b/second.txt\n'
                              '--- a/second.txt\n+++ b/second.txt\n'
                              '@@ -1 +1 @@\n-upstream\n+openwave\n')
        result = self.command('llvm-patch')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.source.read_text(), 'openwave\n')
        self.assertEqual(second.read_text(), 'upstream\n')

    def test_uninitialized_submodule_is_restored(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.root.joinpath('.gitmodules').write_text(
            '[submodule "llvm-project"]\n\tpath = llvm-project\n'
            '\turl = ' + str(self.llvm) + '\n')
        subprocess.run(['git', '-C', str(self.root), 'add', '.gitmodules', 'llvm-project'],
                       check=True, capture_output=True)
        subprocess.run(['git', '-C', str(self.root), 'submodule', 'absorbgitdirs', 'llvm-project'],
                       check=True, capture_output=True)
        shutil.rmtree(self.llvm)
        result = self.command('llvm-patch')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_text(), 'openwave\n')


if __name__ == '__main__':
    unittest.main()
