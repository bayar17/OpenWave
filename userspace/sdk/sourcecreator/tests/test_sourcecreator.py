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
        self.assertIn('QtQuick', out)


if __name__ == '__main__':
    unittest.main()
