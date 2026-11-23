#!/usr/bin/env python3
import importlib.machinery
import importlib.util
import io
import json
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / 'sourcecreator'
ROOT = HERE.parents[3]
loader = importlib.machinery.SourceFileLoader('sourcecreator', str(SOURCE))
spec = importlib.util.spec_from_loader('sourcecreator', loader)
sc = importlib.util.module_from_spec(spec)
loader.exec_module(sc)


OW_SOURCE = HERE.parent / 'sourcecreator-ow'
ow_loader = importlib.machinery.SourceFileLoader('sourcecreator_ow', str(OW_SOURCE))
ow_spec = importlib.util.spec_from_loader('sourcecreator_ow', ow_loader)
ow = importlib.util.module_from_spec(ow_spec)
ow_loader.exec_module(ow)


def run_ow(*arguments):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = ow.sourcecreator.main(list(arguments), team=True)
    return code, out.getvalue(), err.getvalue()


def run(*arguments):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = sc.main(list(arguments))
    return code, out.getvalue(), err.getvalue()


class Base(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: subprocess.run(['rm', '-rf', str(self.work)]))

    def create(self, *extra, name='AuthoPersiano'):
        target = self.work / name
        code, out, err = run('--template=QtQuick', f'--directory={target}', *extra)
        return code, target, out, err


class GeneratedFiles(Base):
    def test_the_requested_layout_and_application_toml_values(self):
        code, target, out, err = self.create('--project-name=Authentic Persian', '--project-id=org.example.authopersiano',
                                             '--project-version=2.1.0', '--project-executable=authentic')
        self.assertEqual(code, 0, err)
        self.assertEqual({p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file()},
                         {'Application.toml', 'CMakeLists.txt', 'src/Main.qml', 'src/Main.cpp', 'include/Main.hpp', 'share/icons/icon.svg'})
        toml = tomllib.loads((target / 'Application.toml').read_text())
        self.assertEqual((toml['id'], toml['name'], toml['version'], toml['executable']),
                         ('org.example.authopersiano', 'Authentic Persian', '2.1.0', 'bin/authentic'))
        self.assertIn('qt_add_executable(authentic', (target / 'CMakeLists.txt').read_text())
        self.assertIn('loadFromModule("AuthenticPersian", "Main")', (target / 'src/Main.cpp').read_text())

    def test_defaults_come_from_the_directory_name(self):
        code, target, out, err = self.create()
        self.assertEqual(code, 0, err)
        toml = tomllib.loads((target / 'Application.toml').read_text())
        self.assertEqual((toml['id'], toml['name'], toml['version'], toml['executable']),
                         ('com.example.authopersiano', 'AuthoPersiano', '0.1.0', 'bin/authopersiano'))

    def test_executable_may_be_given_with_its_bin_prefix(self):
        code, target, out, err = self.create('--project-executable=bin/tool')
        self.assertEqual(code, 0, err)
        self.assertEqual(tomllib.loads((target / 'Application.toml').read_text())['executable'], 'bin/tool')

    def test_generated_application_toml_is_accepted_by_the_manifest_generator(self):
        code, target, out, err = self.create('--project-id=org.example.apptoml')
        generator = ROOT / 'cmake/openwave-bundle-manifest.py'
        result = subprocess.run([sys.executable, str(generator), str(target / 'Application.toml'), '/dev/stdout'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['id'], 'org.example.apptoml')

    def test_without_extra_libs_there_are_no_libraries(self):
        code, target, out, err = self.create()
        self.assertFalse((target / 'libs').exists())
        text = (target / 'CMakeLists.txt').read_text()
        self.assertNotIn('add_subdirectory', text)
        self.assertNotIn('@', text)
        self.assertNotIn('describe', (target / 'src/Main.cpp').read_text())

    def test_extra_libs_creates_a_skeleton_per_library_and_links_it(self):
        code, target, out, err = self.create('--extra-libs=core,net')
        self.assertEqual(code, 0, err)
        for lib in ('core', 'net'):
            for relative in (f'libs/{lib}/CMakeLists.txt', f'libs/{lib}/include/{lib}/{lib}.hpp', f'libs/{lib}/src/{lib}.cpp'):
                self.assertTrue((target / relative).is_file(), relative)
        cmake = (target / 'CMakeLists.txt').read_text()
        self.assertIn('add_subdirectory(libs/core)', cmake)
        self.assertIn('target_link_libraries(authopersiano PRIVATE net)', cmake)
        self.assertIn('core::describe()', (target / 'src/Main.cpp').read_text())
        self.assertIn('/Applications/com.example.authopersiano/lib64', (target / 'libs/core/CMakeLists.txt').read_text())

    def test_bare_extra_libs_means_one_library_named_support(self):
        code, target, out, err = self.create('--extra-libs')
        self.assertEqual(code, 0, err)
        self.assertTrue((target / 'libs/support/src/support.cpp').is_file())

    def test_no_placeholder_is_left_over(self):
        code, target, out, err = self.create('--extra-libs=a1,b_2')
        for path in target.rglob('*'):
            if path.is_file():
                self.assertNotRegex(path.read_text(), r'@[A-Za-z_]+@|@@', path)

    def test_dry_run_writes_nothing(self):
        target = self.work / 'dry'
        code, out, err = run('--template=QtQuick', f'--directory={target}', '--dry-run')
        self.assertEqual(code, 0, err)
        self.assertIn('src/Main.qml', out)
        self.assertFalse(target.exists())


class Refusals(Base):
    def refused(self, *extra, text):
        code, target, out, err = self.create(*extra)
        self.assertEqual(code, 1)
        self.assertIn(text, err)
        self.assertFalse(target.exists(), 'nothing may be created for a refused request')

    def test_bad_values(self):
        self.refused('--project-id=nodots', text='--project-id')
        self.refused('--project-id=com.openwave.mine', text='reserved')
        self.refused('--project-id=org.openwave.webapp.x', text='reserved')
        self.refused('--project-version=1.0-beta', text='--project-version')
        self.refused('--project-executable=Has Space', text='--project-executable')
        self.refused('--project-executable=../x', text='--project-executable')
        self.refused('--project-name=a"b', text='--project-name')
        self.refused('--extra-libs=Bad-Name', text='extra library')
        self.refused('--extra-libs=a,a', text='twice')
        self.refused('--extra-libs=authopersiano', text='name of the executable')

    def test_unknown_template_and_missing_arguments(self):
        code, out, err = run('--template=Nope', f'--directory={self.work}/x')
        self.assertEqual(code, 1)
        self.assertIn('no blueprint', err)
        self.assertEqual(run('--directory=x')[0], 1)
        self.assertEqual(run('--template=QtQuick')[0], 1)

    def test_never_overwrites(self):
        target = self.work / 'busy'
        target.mkdir()
        (target / 'keep.txt').write_text('mine')
        code, out, err = run('--template=QtQuick', f'--directory={target}')
        self.assertEqual(code, 1)
        self.assertIn('not empty', err)
        self.assertEqual((target / 'keep.txt').read_text(), 'mine')
        self.assertEqual([p.name for p in target.iterdir()], ['keep.txt'])

    def test_an_empty_existing_directory_is_fine(self):
        target = self.work / 'empty'
        target.mkdir()
        self.assertEqual(run('--template=QtQuick', f'--directory={target}')[0], 0)


class BlueprintEngine(Base):
    def blueprint(self, body):
        path = self.work / 't.blueprint'
        path.write_text('[blueprint]\nformat = 1\nname = "T"\n' + body)
        return path

    def test_control_lines_and_loops(self):
        path = self.blueprint('[[file]]\npath = "out.txt"\ncontent = \'\'\'\n@@if extra_libs\nlibs:\n@@for lib in extra_libs\n- @lib@ (@lib_class@)\n@@endfor\n@@else\nnone\n@@endif\n\'\'\'\n')
        for libs, expected in (('--extra-libs=my_lib,b', 'libs:\n- my_lib (MyLib)\n- b (B)\n'), (None, 'none\n')):
            target = self.work / f'o{libs and 1 or 0}'
            args = ['--blueprint', str(path), f'--directory={target}'] + ([libs] if libs else [])
            self.assertEqual(run(*args)[0], 0)
            self.assertEqual((target / 'out.txt').read_text(), expected)

    def test_errors(self):
        for body, text in (
            ('[[file]]\npath = "a"\ncontent = "@nope@"\n', 'unknown variable'),
            ('[[file]]\npath = "../a"\ncontent = "x"\n', 'escapes'),
            ('[[file]]\npath = "/abs"\ncontent = "x"\n', 'escapes'),
            ('[[file]]\npath = "a"\ncontent = "x"\n[[file]]\npath = "a"\ncontent = "y"\n', 'twice'),
            ('[[file]]\npath = "a"\ncontent = "@@if extra_libs\\nx"\n', 'without @@endif'),
            ('[[file]]\npath = "a"\ncontent = "@@bogus"\n', 'unknown directive'),
            ('', 'lists no files'),
        ):
            with self.subTest(body=body):
                target = self.work / 'e'
                code, out, err = run('--blueprint', str(self.blueprint(body)), f'--directory={target}')
                self.assertEqual(code, 1)
                self.assertIn(text, err)
                self.assertFalse(target.exists())

    def test_unsupported_format(self):
        path = self.work / 'f.blueprint'
        path.write_text('[blueprint]\nformat = 2\n[[file]]\npath="a"\ncontent="x"\n')
        code, out, err = run('--blueprint', str(path), f'--directory={self.work}/g')
        self.assertEqual(code, 1)
        self.assertIn('format', err)

    def test_list_templates(self):
        code, out, err = run('--list-templates')
        self.assertEqual(code, 0)
        for name in ('QtQuick', 'Applet', 'SettingsModule'):
            self.assertIn(name, out)


class AppletBlueprint(Base):
    def generate(self, *extra, name='Torch Light'):
        target = self.work / 'applet'
        code, out, err = run('--template=Applet', f'--directory={target}', f'--project-name={name}', *extra)
        return code, target, err

    def test_layout_and_desktop_entry(self):
        code, target, err = self.generate()
        self.assertEqual(code, 0, err)
        self.assertEqual(sorted(path.name for path in target.iterdir()),
                         ['MainApplet.qml', 'README.md', 'applet.desktop', 'icon.svg'])
        import configparser
        parser = configparser.RawConfigParser(strict=True)
        parser.optionxform = str
        parser.read_string((target / 'applet.desktop').read_text())
        entry = parser['Desktop Entry']
        self.assertEqual(entry['Type'], 'X-OpenWave-Applet')
        self.assertEqual(entry['Name'], 'Torch Light')
        self.assertEqual(entry['X-OpenWave-Applet-ApiVersion'], '1')
        self.assertTrue((target / entry['Icon']).is_file())
        self.assertNotIn('X-OpenWave-Applet-Settings', entry)

    def test_main_applet_uses_the_shared_base_type(self):
        code, target, err = self.generate()
        source = (target / 'MainApplet.qml').read_text()
        self.assertIn('import OpenWave.QuickMenu as Applets', source)
        self.assertIn('Applets.Applet {', source)
        self.assertIn('qsTr("Torch Light")', source)
        self.assertNotIn('//', source)

    def test_install_name_is_a_valid_applet_id(self):
        import re
        pattern = re.compile(r'^[A-Za-z][A-Za-z0-9_]{0,63}$')
        for name, executable in (('Torch Light', None), ('Wi-Fi Helper', 'wifi-helper'), ('Ten', 'a')):
            extra = [f'--project-executable={executable}'] if executable else []
            code, target, err = self.generate(*extra, name=name)
            self.assertEqual(code, 0, err)
            readme = (target / 'README.md').read_text()
            install = [line for line in readme.splitlines() if line.startswith('cp -r .')][0]
            self.assertTrue(pattern.match(install.rsplit('/', 1)[1]), install)
            subprocess.run(['rm', '-rf', str(target)])

    def test_applet_name_always_matches_the_applet_id_rule(self):
        import re
        pattern = re.compile(r'^[A-Za-z][A-Za-z0-9_]{0,63}$')
        for text in ('torch-light', 'wifi', '3d_viewer', 'a' * 100, 'x-y-z', '9'):
            self.assertRegex(sc.applet_name(text), pattern)
        self.assertEqual(sc.applet_name('torch-light'), 'torch_light')

    def test_no_placeholder_is_left_over(self):
        code, target, err = self.generate()
        for path in target.iterdir():
            self.assertNotRegex(path.read_text(), r'@[A-Za-z_]+@', path.name)


class SettingsModuleBlueprint(Base):
    ID_PATTERN = r'^[a-z][a-z0-9_-]*(\.[a-z][a-z0-9_-]*){2,}$'

    def generate(self, *extra, name='Power Tools'):
        target = self.work / 'module'
        code, out, err = run('--template=SettingsModule', f'--directory={target}', f'--project-name={name}', *extra)
        return code, target, err

    def test_module_json_satisfies_the_registry_rules(self):
        import re
        code, target, err = self.generate('--project-id=org.example.powertools')
        self.assertEqual(code, 0, err)
        metadata = json.loads((target / 'module.json').read_text())
        self.assertLessEqual(len((target / 'module.json').read_bytes()), 64 * 1024)
        self.assertRegex(metadata['id'], self.ID_PATTERN)
        self.assertLessEqual(len(metadata['id']), 128)
        self.assertTrue(1 <= len(metadata['name']) <= 64)
        self.assertLessEqual(len(metadata.get('description', '')), 256)
        self.assertRegex(metadata.get('icon', ''), r'^[A-Za-z0-9._-]*$')
        self.assertLessEqual(len(metadata.get('category', '')), 64)
        self.assertEqual(metadata['apiVersion'], 1)
        self.assertTrue((target / metadata['entry']).is_file())
        self.assertLessEqual(len(metadata['search']), 16)
        keys = [entry['key'] for entry in metadata['search']]
        self.assertEqual(len(keys), len(set(keys)))
        for entry in metadata['search']:
            self.assertLessEqual(len(entry['title']), 64)
            self.assertLessEqual(len(entry['detail']), 128)

    def test_entry_qml_has_the_host_hooks(self):
        code, target, err = self.generate('--project-id=org.example.powertools')
        source = (target / 'MainModule.qml').read_text()
        for needle in ('function openEntry(key)', 'function back()', 'import OpenWave.Fonts 1.0', 'import OceanSettings 1.0'):
            self.assertIn(needle, source)
        self.assertNotIn('//', source)

    def test_upper_case_project_ids_become_lower_case_module_ids(self):
        code, target, err = self.generate('--project-id=org.Example.PowerTools')
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((target / 'module.json').read_text())['id'], 'org.example.powertools')

    def test_long_project_names_still_fit_the_registry_limits(self):
        code, target, err = self.generate('--project-id=org.example.long', name='N' * 64)
        self.assertEqual(code, 0, err)
        metadata = json.loads((target / 'module.json').read_text())
        self.assertLessEqual(len(metadata['search'][0]['detail']), 128)
        self.assertLessEqual(len(metadata['description']), 256)

    def test_system_namespace_ids_are_refused(self):
        code, target, err = self.generate('--project-id=com.openwave.settings.extra')
        self.assertEqual(code, 1)
        self.assertIn('reserved', err)

    def test_no_placeholder_is_left_over(self):
        code, target, err = self.generate('--project-id=org.example.powertools')
        for path in target.iterdir():
            self.assertNotRegex(path.read_text(), r'@[A-Za-z_]+@', path.name)


class OpenWaveTeamMode(Base):
    def make(self, template, *extra, name='Team Tool'):
        target = self.work / 'out'
        code, out, err = run_ow(f'--template={template}', f'--directory={target}', f'--project-name={name}', *extra)
        return code, target, out, err

    def test_com_openwave_projects_are_system_bundles(self):
        for identifier in ('com.openwave.teamtool', 'com.openwave.apps.teamtool'):
            code, target, out, err = self.make('QtQuick', f'--project-id={identifier}', '--extra-libs=support')
            self.assertEqual(code, 0, err)
            self.assertIn(f'id = "{identifier}"', (target / 'Application.toml').read_text())
            cmake = (target / 'CMakeLists.txt').read_text()
            self.assertIn(f'openwave_install_bundle(team-tool {identifier} SYSTEM)', cmake)
            self.assertIn(f'/System/Applications/{identifier}/share/icons', cmake)
            library = (target / 'libs/support/CMakeLists.txt').read_text()
            self.assertIn(f'/System/Applications/{identifier}/lib64', library)
            self.assertNotIn('/Applications/' + identifier, library.replace('/System/Applications/' + identifier, ''))
            self.assertNotIn('DESTINATION /Applications/', cmake)
            self.assertIn(f'file:///System/Applications/{identifier}/share/icons/icon.svg', (target / 'src/Main.qml').read_text())
            self.assertIn('system bundles', out)
            self.assertIn('OS image', out)
            subprocess.run(['rm', '-rf', str(target)])

    def test_org_openwave_projects_are_ordinary_applications(self):
        code, target, out, err = self.make('QtQuick', '--project-id=org.openwave.teamtool')
        self.assertEqual(code, 0, err)
        cmake = (target / 'CMakeLists.txt').read_text()
        self.assertIn('openwave_install_bundle(team-tool org.openwave.teamtool)', cmake)
        self.assertNotIn('SYSTEM', cmake)
        self.assertIn('/Applications/org.openwave.teamtool/share/icons', cmake)
        self.assertNotIn('OS image', out)

    def test_the_regular_tool_never_makes_system_bundles(self):
        code, out, err = run('--template=QtQuick', f'--directory={self.work}/regular', '--project-id=org.example.regular')
        self.assertEqual(code, 0, err)
        cmake = (self.work / 'regular' / 'CMakeLists.txt').read_text()
        self.assertNotIn('SYSTEM', cmake)
        self.assertIn('/Applications/org.example.regular/share/icons', cmake)

    def test_the_generated_manifest_passes_the_sdk_manifest_generator(self):
        import importlib.util as util
        generator = util.spec_from_file_location('bundle_manifest', ROOT / 'cmake/openwave-bundle-manifest.py')
        module = util.module_from_spec(generator)
        generator.loader.exec_module(module)
        code, target, out, err = self.make('QtQuick', '--project-id=com.openwave.teamtool')
        self.assertEqual(code, 0, err)
        text = (target / 'Application.toml').read_text()
        module.convert(text, system=True)
        with self.assertRaises(Exception):
            module.convert(text, system=False)

    def test_applets_and_settings_modules_accept_system_ids(self):
        code, target, out, err = self.make('Applet', '--project-id=com.openwave.torch')
        self.assertEqual(code, 0, err)
        self.assertTrue((target / 'MainApplet.qml').is_file())
        subprocess.run(['rm', '-rf', str(target)])
        code, target, out, err = self.make('SettingsModule', '--project-id=com.openwave.settings.teamtool')
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((target / 'module.json').read_text())['id'], 'com.openwave.settings.teamtool')

    def test_the_default_id_is_in_the_openwave_namespace(self):
        code, target, out, err = self.make('QtQuick', name='Team Tool')
        self.assertEqual(code, 0, err)
        self.assertIn('id = "com.openwave.teamtool"', (target / 'Application.toml').read_text())

    def test_namespaces_generated_by_appd_stay_refused(self):
        for identifier in ('org.openwave.android.calculator', 'org.openwave.webapp.example'):
            for template in ('QtQuick', 'Applet', 'SettingsModule'):
                code, target, out, err = self.make(template, f'--project-id={identifier}')
                self.assertEqual(code, 1, (template, identifier))
                self.assertIn('generated by appd', err)
                self.assertFalse(target.exists())

    def test_other_validation_still_applies(self):
        code, target, out, err = self.make('QtQuick', '--project-id=notanid')
        self.assertEqual(code, 1)
        code, target, out, err = self.make('QtQuick', '--project-id=com.openwave.x', '--project-version=one')
        self.assertEqual(code, 1)

    def test_the_regular_tool_still_refuses_system_namespaces(self):
        for template in ('QtQuick', 'Applet', 'SettingsModule'):
            code, out, err = run(f'--template={template}', f'--directory={self.work}/plain-{template}',
                                 '--project-id=com.openwave.teamtool')
            self.assertEqual(code, 1, template)
            self.assertIn('reserved', err)

    def test_the_regular_tool_prints_no_team_note(self):
        code, out, err = run('--template=QtQuick', f'--directory={self.work}/plain', '--project-id=org.example.plain')
        self.assertEqual(code, 0, err)
        self.assertNotIn('OS image', out)

    def test_the_wrapper_runs_from_the_command_line(self):
        target = self.work / 'cli'
        result = subprocess.run([sys.executable, str(OW_SOURCE), '--template=Applet', f'--directory={target}',
                                 '--project-id=com.openwave.cliapplet', '--dry-run'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('MainApplet.qml', result.stdout)
        self.assertFalse(target.exists())


if __name__ == '__main__':
    unittest.main()
