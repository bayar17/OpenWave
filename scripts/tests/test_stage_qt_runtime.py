"""Exercise the runtime stager with Qt's prefix-relative plugin/QML layout."""
from pathlib import Path
import importlib.util
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('runtime_stager', ROOT / 'scripts/stage-openwave-runtime.py')
stager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stager)


@unittest.skipUnless(platform.machine() == 'x86_64' and shutil.which('cc') and shutil.which('readelf'),
                     'ELF fixture needs an x86_64 C compiler and readelf')
class QtRuntimeLayoutTests(unittest.TestCase):
    def test_prefix_relative_plugins_and_qml_are_deployed_without_host_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            sdk = work / 'sdk'
            sysroot = sdk / 'sysroot/x86_64-unknown-openwave'
            runtime = work / 'runtime'
            runtime.mkdir()
            (sdk / 'bin').mkdir(parents=True)
            (sdk / 'bin/llvm-readelf').symlink_to(shutil.which('readelf'))
            (sdk / 'bin/moc').write_text('host tool fixture')
            (runtime / 'usr/share').mkdir(parents=True)
            (runtime / 'usr/share/fonts').symlink_to('../lib/fonts')
            fonts = sysroot / 'usr/share/fonts/openwave'
            fonts.mkdir(parents=True)
            (fonts / 'fixture.ttf').write_bytes(b'font fixture')
            fixture = work / 'runtime.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-nostdlib', '-x', 'c', '-', '-o', str(fixture)],
                           input='int fixture(void) { return 0; }\n', text=True, check=True,
                           capture_output=True)
            compose = work / 'compose'
            (compose / 'en_US.UTF-8').mkdir(parents=True)
            (compose / 'compose.dir').write_text('en_US.UTF-8/Compose en_US.UTF-8\n')
            (compose / 'locale.alias').write_text('en_US.utf8 en_US.UTF-8\n')
            (compose / 'en_US.UTF-8/Compose').write_text('<Multi_key> <a> <e> : "æ"\n')
            license_file = work / 'copyright'
            license_file.write_text('Compose data license fixture\n')
            compose_command = ['python3', str(ROOT / 'scripts/stage-openwave-compose.py'),
                               '--sysroot', str(sysroot), '--source', str(compose),
                               '--license', str(license_file), '--manifest', str(work / 'compose.json')]
            subprocess.run(compose_command, capture_output=True, text=True, check=True)
            (compose / 'en_US.UTF-8/Compose').write_bytes(fixture.read_bytes())
            rejected = subprocess.run(compose_command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('must not contain ELF', rejected.stderr)
            self.assertTrue((sysroot / 'usr/share/X11/locale/en_US.UTF-8/Compose').read_text().startswith('<Multi_key>'))
            modules = runtime / 'usr/lib64/gconv'
            modules.mkdir(parents=True)
            shutil.copyfile(fixture, modules / 'runtime.so')
            subprocess.run(['cc', '-shared', '-fPIC', '-nostdlib', '-x', 'c', '-',
                            '-L' + str(work), '-Wl,-rpath,$ORIGIN', '-l:runtime.so',
                            '-o', str(modules / 'convert.so')],
                           input='extern int fixture(void); int convert(void) { return fixture(); }\n',
                           text=True, check=True, capture_output=True)
            libraries = ['usr/lib64/libQt6Core.so.6', 'usr/lib64/libopenwave-fixture.so.1',
                         'usr/plugins/platforms/libqeglfs.so', 'usr/qml/QtQuick/libqtquick2plugin.so']
            for module in ('Fonts', 'Theme', 'Controls', 'Application'):
                folder = sysroot / 'usr/qml/OpenWave' / module
                folder.mkdir(parents=True)
                (folder / 'qmldir').write_text('module OpenWave.' + module + '\n')
                libraries.append(str(folder.relative_to(sysroot) / 'plugin.so'))
            for relative in libraries:
                destination = sysroot / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(fixture, destination)
            (sysroot / 'usr/qml/QtQuick/Example.qml').write_text('import QtQuick\nItem {}\n')
            (sysroot / 'usr/qml/QtQuick/plugins.qmltypes').write_text('development metadata')
            result = subprocess.run(['python3', str(ROOT / 'scripts/stage-openwave-runtime.py'),
                                     '--repo', str(ROOT), '--sdk', str(sdk), '--rootfs', str(runtime)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for relative in libraries:
                self.assertEqual((runtime / relative).read_bytes(), fixture.read_bytes(), relative)
            self.assertTrue((runtime / 'usr/qml/QtQuick/Example.qml').is_file())
            self.assertFalse((runtime / 'usr/qml/QtQuick/plugins.qmltypes').exists())
            self.assertFalse((runtime / 'bin/moc').exists())
            self.assertFalse((runtime / 'usr/bin/moc').exists())
            self.assertTrue((runtime / 'usr/share/X11/locale/en_US.UTF-8/Compose').is_file())
            self.assertTrue((runtime / 'usr/share/licenses/x11-compose/copyright').is_file())
            self.assertTrue((runtime / 'usr/share/fonts').is_symlink())
            self.assertEqual((runtime / 'usr/lib/fonts/openwave/fixture.ttf').read_bytes(), b'font fixture')
            (modules / 'runtime.so').unlink()
            missing = subprocess.run(['python3', str(ROOT / 'scripts/stage-openwave-runtime.py'),
                                      '--repo', str(ROOT), '--sdk', str(sdk), '--rootfs', str(runtime)],
                                     capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn('runtime.so required by', missing.stderr)

    def test_destination_aliases_stay_in_guest_and_reject_escapes_and_loops(self):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / 'runtime'
            runtime.mkdir()
            instance = object.__new__(stager.Stager)
            instance.rootfs = runtime
            (runtime / 'absolute').symlink_to('/usr/lib')
            self.assertEqual(instance.safe_destination(Path('absolute/plugin.so')), runtime / 'usr/lib/plugin.so')
            (runtime / 'escape').symlink_to('../outside')
            with self.assertRaisesRegex(RuntimeError, 'escapes rootfs'):
                instance.safe_destination(Path('escape/plugin.so'))
            (runtime / 'loop').symlink_to('loop')
            with self.assertRaisesRegex(RuntimeError, 'symlink loop'):
                instance.safe_destination(Path('loop/plugin.so'))


if __name__ == '__main__':
    unittest.main()
