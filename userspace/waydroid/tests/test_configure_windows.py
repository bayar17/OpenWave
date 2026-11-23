"""The per-application window adapter sets exactly one property, idempotently."""
from pathlib import Path
import importlib.util
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'configure_windows.py'
spec = importlib.util.spec_from_file_location('configure_windows', SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

CFG = '[waydroid]\narch = x86_64\nbinder = binder\n\n[properties]\nro.sf.lcd_density = 180\n'


class ConfigureWindowsTests(unittest.TestCase):
    def test_cfg_gains_only_the_property_and_keeps_everything_else(self):
        updated = module.configure_cfg(CFG)
        import configparser
        config = configparser.ConfigParser(); config.read_string(updated)
        self.assertEqual(config['properties']['persist.waydroid.multi_windows'], 'false')
        self.assertEqual(config['properties']['persist.waydroid.width'], '')
        self.assertEqual(config['properties']['persist.waydroid.height'], '')
        self.assertNotIn('persist.waydroid.task_streams', config['properties'])
        self.assertEqual(config['properties']['ro.setupwizard.mode'], 'DISABLED')
        self.assertEqual(config['properties']['ro.sf.lcd_density'], '320')
        # Waydroid's 180 is replaced: 180 makes the QEMU display a tablet.
        self.assertEqual(config['waydroid']['binder'], 'binder')
        self.assertIsNone(module.configure_cfg(updated))

    def test_cfg_without_properties_section(self):
        updated = module.configure_cfg('[waydroid]\narch = x86_64\n')
        self.assertIn('persist.waydroid.multi_windows = false', updated)

    def test_uninitialized_cfg_is_refused(self):
        with self.assertRaises(ValueError):
            module.configure_cfg('[other]\nx = 1\n')

    def test_prop_line_is_unique_and_overrides_false(self):
        text = 'ro.hardware.gralloc=gbm\npersist.waydroid.multi_windows=true\npersist.waydroid.width=720\nwaydroid.x=1\n'
        updated = module.configure_prop(text)
        self.assertEqual(updated.count('persist.waydroid.multi_windows='), 1)
        self.assertNotIn('width=720', updated)
        self.assertIn('persist.waydroid.multi_windows=false\n', updated)
        self.assertIn('persist.waydroid.width=\n', updated)
        self.assertNotIn('task_streams', updated)
        self.assertIn('ro.hardware.gralloc=gbm\n', updated)
        self.assertIsNone(module.configure_prop(updated))

    def test_source_touches_only_fixed_files(self):
        text = SOURCE.read_text()
        self.assertIn("ROOT = Path('/var/lib/waydroid')", text)
        self.assertIn("('waydroid.cfg', configure_cfg), ('waydroid_base.prop', configure_prop)", text)
        self.assertNotIn('subprocess', text)
        self.assertTrue(text.startswith('#!/usr/bin/python3.14 -I\n'))


if __name__ == '__main__':
    unittest.main()
