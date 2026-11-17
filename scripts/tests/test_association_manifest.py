import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('manifest', ROOT / 'cmake/openwave-bundle-manifest.py')
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
HEADER = 'schema = 1\nid = "org.test.viewer"\nname = "Viewer"\nversion = "1"\nexecutable = "bin/viewer"\n'


class AssociationManifestTests(unittest.TestCase):
    def test_mime_extensions_and_developer_survive_packaging(self):
        result = json.loads(manifest.convert(HEADER + 'accepts_paths = true\nopens = ["xlsx", "pdf"]\nmime_types = ["application/pdf", "application/x-test"]\ndeveloper = "Test"\n'))
        self.assertEqual(result['opens'], ['xlsx', 'pdf'])
        self.assertNotIn('xslx', result['opens'])
        self.assertEqual(result['mimeTypes'], ['application/pdf', 'application/x-test'])
        self.assertEqual(result['developer'], 'Test')

    def test_invalid_mime_declarations_fail(self):
        for invalid in ['mime_types = ["application/pdf"]', 'accepts_paths = true\nmime_types = ["image/*"]',
                        'accepts_paths = true\nmime_types = ["Image/PNG"]', 'mime_types = 2',
                        'accepts_paths = true\nmime_types = ["image/png", "image/png"]',
                        'accepts_paths = true\nmime_types = [["image/png"]]']:
            with self.subTest(invalid=invalid), self.assertRaises(ValueError): manifest.convert(HEADER + invalid)

    def test_gallery_declares_only_implemented_image_types(self):
        document = (ROOT / 'OceanLauncher/OceanPhotos/Application.toml').read_text()
        result = json.loads(manifest.convert(document, system=True))
        self.assertTrue(result['acceptsPaths'])
        self.assertEqual(set(result['opens']), {'png', 'jpg', 'jpeg', 'webp', 'gif', 'svg'})
        self.assertIn('image/jpeg', result['mimeTypes'])

if __name__ == '__main__': unittest.main()
