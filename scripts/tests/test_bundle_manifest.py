import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bundle_manifest', ROOT / 'cmake' / 'openwave-bundle-manifest.py')
bundle_manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle_manifest)

HEADER = '''schema = 1
id = "org.example.viewer"
name = "Viewer"
version = "1.0.0"
executable = "bin/viewer"
'''


class OpensTests(unittest.TestCase):
    def test_declared_extensions_reach_the_manifest(self):
        manifest = json.loads(bundle_manifest.convert(HEADER + 'accepts_paths = true\nopens = ["docx", "xlsx"]\n'))
        self.assertEqual(manifest['opens'], ['docx', 'xlsx'])
        self.assertTrue(manifest['acceptsPaths'])

    def test_no_opens_means_no_field(self):
        self.assertNotIn('opens', json.loads(bundle_manifest.convert(HEADER)))

    def test_invalid_declarations_are_refused(self):
        for text in ('opens = ["docx"]\n',
                     'accepts_paths = true\nopens = ["DOCX"]\n',
                     'accepts_paths = true\nopens = ["docx", "docx"]\n',
                     'accepts_paths = true\nopens = ["do.cx"]\n',
                     'accepts_paths = true\nopens = [""]\n',
                     'accepts_paths = true\nopens = "docx"\n',
                     'accepts_paths = true\nopens = [%s]\n' % ', '.join('"a%d"' % n for n in range(33))):
            with self.assertRaises(ValueError, msg=text):
                bundle_manifest.convert(HEADER + text)


class DesktopEntryTests(unittest.TestCase):
    FULL = HEADER + 'icon = "share/icons/v.png"\naccepts_paths = true\nmime_types = ["image/png"]\nuri_schemes = ["viewer"]\ncategories = ["Graphics", "Viewer"]\ncomment = "Looks at pictures"\n'

    def test_declared_metadata_reaches_manifest_and_desktop_entry(self):
        text = bundle_manifest.convert(self.FULL)
        manifest = json.loads(text)
        self.assertEqual(manifest['uriSchemes'], ['viewer'])
        self.assertEqual(manifest['categories'], ['Graphics', 'Viewer'])
        entry = bundle_manifest.desktop_entry(text)
        self.assertTrue(entry.startswith('[Desktop Entry]\nType=Application\n'))
        self.assertIn('Exec=/System/Services/openwave-launch org.example.viewer %F\n', entry)
        self.assertIn('Icon=/Applications/org.example.viewer/share/icons/v.png\n', entry)
        self.assertIn('MimeType=image/png;x-scheme-handler/viewer;\n', entry)
        self.assertIn('X-OpenWave-AppId=org.example.viewer\n', entry)

    def test_system_bundles_use_the_system_root(self):
        text = bundle_manifest.convert(HEADER.replace('org.example.viewer', 'com.openwave.viewer') + 'icon = "a.png"\n', system=True)
        self.assertIn('Icon=/System/Applications/com.openwave.viewer/a.png\n', bundle_manifest.desktop_entry(text))

    def test_a_library_has_no_desktop_entry(self):
        text = bundle_manifest.convert('schema = 1\nid = "org.example.lib"\nname = "Lib"\nversion = "1"\nkind = "library"\n')
        self.assertIsNone(bundle_manifest.desktop_entry(text))

    def test_names_cannot_inject_desktop_keys(self):
        text = bundle_manifest.convert(HEADER.replace('name = "Viewer"', 'name = "A\\nExec=/bin/sh"'))
        entry = bundle_manifest.desktop_entry(text)
        self.assertEqual([line for line in entry.splitlines() if line.startswith('Exec=')],
                         ['Exec=/System/Services/openwave-launch org.example.viewer'])

    def test_invalid_metadata_is_refused(self):
        for text in ('uri_schemes = ["viewer"]\n', 'accepts_paths = true\nuri_schemes = ["Viewer"]\n',
                     'accepts_paths = true\nuri_schemes = ["a", "a"]\n', 'categories = ["Bad Name"]\n',
                     'categories = ["A", "A"]\n', 'comment = "x\\ny"\n',
                     'accepts_paths = true\nuri_schemes = [%s]\n' % ', '.join('"a%d"' % n for n in range(17))):
            with self.assertRaises(ValueError, msg=text):
                bundle_manifest.convert(HEADER + text)


if __name__ == '__main__':
    unittest.main()
