"""Exercise the target loader's real default paths inside a guest root."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from openwave_runtime_loader import guest_path, guest_runner, library_aliases, loader_list

SDK = ROOT / 'toolchain'
SYSROOT = SDK / 'sysroot/x86_64-unknown-openwave'


class LoaderTests(unittest.TestCase):
    def fixture(self, directory):
        root = directory / 'root'
        for relative in ('bin', 'lib64', 'usr/lib', 'usr/lib64', 'usr/bin'):
            (root / relative).mkdir(parents=True, exist_ok=True)
        source = directory / 'probe.c'
        source.write_text('#include <zlib.h>\nint main(void) { return zlibVersion()[0] == 0; }\n')
        subprocess.run([str(SDK / 'bin/clang'), '--target=x86_64-unknown-openwave',
                        '--sysroot=' + str(SYSROOT), str(source), '-lz',
                        '-o', str(root / 'usr/bin/probe')], check=True)
        for name in ('ld-linux-x86-64.so.2', 'libc.so.6', 'libm.so.6'):
            shutil.copy2(SYSROOT / 'lib64' / name, root / 'lib64' / name)
        shutil.copy2(ROOT / 'rootfs/bin/busybox', root / 'bin/busybox')
        shutil.copy2(SYSROOT / 'usr/lib/libz.so.1.3.1', root / 'usr/lib/libz.so.1.3.1')
        # An absolute guest link must resolve inside the fixture, not the host.
        (root / 'usr/lib/libz.so.1').symlink_to('/usr/lib/libz.so.1.3.1')
        return root

    def test_actual_loader_fails_then_resolves_zlib_without_environment_search(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = self.fixture(Path(temporary))
            # A host test environment must not hide the missing guest alias.
            with patch.dict(os.environ, {'LD_LIBRARY_PATH': str(root / 'usr/lib')}):
                with self.assertRaisesRegex(RuntimeError, 'libz.so.1'):
                    loader_list(root, '/usr/bin/probe')
            aliases = library_aliases(root)
            self.assertIn('usr/lib64/libz.so.1', aliases)
            self.assertEqual(os.readlink(root / 'usr/lib64/libz.so.1'), '../lib/libz.so.1.3.1')
            output = loader_list(root, '/usr/bin/probe')
            self.assertIn('libz.so.1 => /usr/lib64/libz.so.1', output)
            self.assertEqual(library_aliases(root), [])

    def test_trace_mode_zero_exit_does_not_hide_missing_symbol_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            root = self.fixture(temporary)
            compiler = [str(SDK / 'bin/clang'), '--target=x86_64-unknown-openwave',
                        '--sysroot=' + str(SYSROOT)]
            source = temporary / 'versioned.c'
            source.write_text('int versioned_answer(void) { return 0; }\n')
            current = temporary / 'libversioned.so.1'
            for version, destination in (('OPENWAVE_2', current),
                                         ('OPENWAVE_1', root / 'usr/lib64/libversioned.so.1')):
                script = temporary / (version + '.map')
                script.write_text(version + ' { global: versioned_answer; local: *; };\n')
                subprocess.run(compiler + ['-shared', '-fPIC', str(source),
                               '-Wl,-soname,libversioned.so.1',
                               '-Wl,--version-script=' + str(script), '-o', str(destination)], check=True)
            source = temporary / 'version-probe.c'
            source.write_text('extern int versioned_answer(void);\n'
                              'int main(void) { return versioned_answer(); }\n')
            subprocess.run(compiler + [str(source), '-L' + str(temporary),
                           '-l:libversioned.so.1', '-o', str(root / 'usr/bin/version-probe')], check=True)
            env = {key: value for key, value in os.environ.items() if not key.startswith('LD_')}
            raw = subprocess.run(guest_runner(root) + [
                                  '/lib64/ld-linux-x86-64.so.2', '--inhibit-cache',
                                  '--list', '/usr/bin/version-probe'],
                                 env=env, capture_output=True, text=True)
            self.assertEqual(raw.returncode, 0, raw.stderr)
            self.assertIn("version `OPENWAVE_2' not found", raw.stderr)
            with self.assertRaisesRegex(RuntimeError, 'OPENWAVE_2'):
                loader_list(root, '/usr/bin/version-probe')
            shutil.copy2(current, root / 'usr/lib64/libversioned.so.1')
            self.assertIn('libversioned.so.1', loader_list(root, '/usr/bin/version-probe'))

    def test_eager_trace_rejects_stale_lazy_symbol_without_running_program(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            root = self.fixture(temporary)
            compiler = [str(SDK / 'bin/clang'), '--target=x86_64-unknown-openwave',
                        '--sysroot=' + str(SYSROOT)]
            source = temporary / 'lazy.c'
            current = temporary / 'liblazy.so.1'
            source.write_text('int lazy_answer(void) { return 42; }\n')
            subprocess.run(compiler + ['-shared', '-fPIC', str(source),
                           '-Wl,-soname,liblazy.so.1', '-o', str(current)], check=True)
            source.write_text('int different_answer(void) { return 0; }\n')
            subprocess.run(compiler + ['-shared', '-fPIC', str(source),
                           '-Wl,-soname,liblazy.so.1', '-o', str(root / 'usr/lib64/liblazy.so.1')], check=True)
            source.write_text('#include <fcntl.h>\n#include <unistd.h>\n'
                              'extern int lazy_answer(void);\n'
                              '__attribute__((constructor)) static void init(void) '
                              '{ close(open("/constructor-ran", O_WRONLY|O_CREAT, 0600)); }\n'
                              'int main(void) { close(open("/main-ran", O_WRONLY|O_CREAT, 0600)); '
                              'return lazy_answer(); }\n')
            subprocess.run(compiler + [str(source), '-L' + str(temporary), '-l:liblazy.so.1',
                           '-o', str(root / 'usr/bin/lazy-probe')], check=True)
            with self.assertRaisesRegex(RuntimeError, 'undefined symbol: lazy_answer'):
                loader_list(root, '/usr/bin/lazy-probe')
            shutil.copy2(current, root / 'usr/lib64/liblazy.so.1')
            self.assertIn('liblazy.so.1', loader_list(root, '/usr/bin/lazy-probe'))
            self.assertFalse((root / 'constructor-ran').exists())
            self.assertFalse((root / 'main-ran').exists())

    def test_conflicting_default_library_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = self.fixture(Path(temporary))
            destination = root / 'usr/lib64/libz.so.1'
            destination.write_bytes(b'conflicting installed file')
            with self.assertRaisesRegex(RuntimeError, 'Conflicting loader default'):
                library_aliases(root)
            self.assertEqual(destination.read_bytes(), b'conflicting installed file')
            self.assertFalse((root / 'usr/lib64/libz.so.1.3.1').exists())

    def test_symlinked_library_directory_cannot_escape_to_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            root = temporary / 'root'; root.mkdir()
            (root / 'usr').mkdir()
            external = temporary / 'external'; external.mkdir()
            (root / 'usr/lib64').symlink_to(external)
            with self.assertRaisesRegex(RuntimeError, 'Unsafe runtime library directory'):
                library_aliases(root)
            self.assertEqual(list(external.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
