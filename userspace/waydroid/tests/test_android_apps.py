"""appd's Android bridge: fixed operations, validated Android data."""
from pathlib import Path
import base64
import importlib.util
import struct
import tempfile
import unittest
import zlib

SOURCE = Path(__file__).resolve().parents[1] / 'android_apps.py'
spec = importlib.util.spec_from_file_location('android_apps', SOURCE)
apps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apps)


def png(width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + b'\0\0\0' * width for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def entry(package, name='App', categories=('android.intent.category.LAUNCHER',)):
    return {'packageName': package, 'name': name, 'categories': list(categories)}


class AndroidAppsTests(unittest.TestCase):
    def test_package_names(self):
        for good in ('com.android.deskclock', 'org.lineageos.etar', 'a.b', 'x_1.y_2'):
            self.assertTrue(apps.valid_package(good), good)
        for bad in ('', 'deskclock', 'a.b\n', 'a.b\r', '.a.b', 'a..b', '1a.b', 'a.1b', 'a.b/c', 'a.b;c', 'a.b ', '../a.b',
                    'a.' + 'b' * 120, None, 3):
            self.assertFalse(apps.valid_package(bad), bad)

    def test_names_are_bounded_and_printable(self):
        self.assertEqual(apps.clean_name('Clock\n\x1b[31m'), 'Clock[31m')
        self.assertEqual(len(apps.clean_name('x' * 500)), apps.MAX_NAME)
        self.assertEqual(apps.clean_name(None), '')

    def test_catalogue_keeps_only_valid_launchable_unique_visible_apps(self):
        result = apps.catalogue([
            entry('com.android.deskclock', 'Clock'),
            entry('com.android.deskclock', 'Duplicate'),
            entry('com.android.launcher3', 'Home'),
            entry('org.lineageos.setupwizard'),
            entry('com.example.service', categories=()),
            entry('../../etc', 'Evil'),
            entry('com.android.settings', '\x00'),
        ], icon_reader=lambda package: b'')
        self.assertEqual([e['package'] for e in result], ['com.android.deskclock', 'com.android.settings'])
        self.assertEqual(result[0]['name'], 'Clock')
        self.assertEqual(result[1]['name'], 'com.android.settings')  # empty name falls back

    def test_icons_are_bounded_pngs_and_never_followed_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'a.good.png').write_bytes(png(48, 48))
            Path(directory, 'a.huge.png').write_bytes(png(1024, 8))
            Path(directory, 'a.fake.png').write_bytes(b'GIF89a' + b'\0' * 64)
            Path(directory, 'a.link.png').symlink_to(Path(directory, 'a.good.png'))
            self.assertEqual(apps.png_dimensions(apps.read_icon('a.good', directory)), (48, 48))
            for package in ('a.huge', 'a.fake', 'a.link', 'a.missing'):
                with self.subTest(package=package):
                    self.assertEqual(apps.read_icon(package, directory), b'')
            result = apps.catalogue([entry('a.good')], icon_reader=lambda p: apps.read_icon(p, directory))
            self.assertEqual(base64.b64decode(result[0]['icon'])[:8], b'\x89PNG\r\n\x1a\n')

    def test_operations_and_arguments_are_fixed(self):
        text = SOURCE.read_text()
        self.assertTrue(text.startswith('#!/usr/bin/python3.14 -I\n'))
        for forbidden in ('subprocess', 'os.system', 'os.environ', 'getenv', 'shell=True', 'eval(', 'exec('):
            self.assertNotIn(forbidden, text)
        self.assertIn("if argv == ['list']:", text)
        self.assertIn("argv[0] == 'launch' and valid_package(argv[1]) and argv[1] not in HIDDEN", text)
        self.assertIn("SERVICE = 'waydroidplatform'", text)
        self.assertIn('signal.alarm(TIME_LIMIT)', text)

    def test_hidden_packages_cannot_be_launched(self):
        self.assertIn('com.android.launcher3', apps.HIDDEN)
        # Non-root callers are refused before any argument is considered.
        if apps.os.geteuid():
            self.assertEqual(apps.main(['launch', 'com.android.deskclock']), 126)


if __name__ == '__main__':
    unittest.main()


class UserMonitorTests(unittest.TestCase):
    def test_unlocked_and_package_events(self):
        self.assertEqual(apps.monitor_event(1), {'event': 'unlocked'})
        self.assertEqual(apps.monitor_event(2, 0, 'com.example.app'),
                         {'event': 'package', 'change': 'added', 'package': 'com.example.app'})
        self.assertEqual(apps.monitor_event(2, 1, 'com.example.app')['change'], 'removed')

    def test_untrusted_events_are_dropped(self):
        for mode, package in ((7, 'com.example.app'), (0, 'not a package'), (0, 'com.example.app\n'),
                              (0, None), (0, 'a' * 200 + '.b')):
            self.assertIsNone(apps.monitor_event(2, mode, package))
        self.assertIsNone(apps.monitor_event(99))

    def test_both_interface_names(self):
        self.assertEqual(set(apps.USER_MONITOR_INTERFACES),
                         {'id.waydro.waydroid.IUserMonitor', 'lineageos.waydroid.IUserMonitor'})

    def test_emit_is_one_json_line(self):
        import io
        stream = io.StringIO()
        apps.emit({'event': 'unlocked'}, stream)
        self.assertEqual(stream.getvalue(), '{"event":"unlocked"}\n')


class CloseAllTests(unittest.TestCase):
    def test_close_all_is_one_fixed_intent(self):
        calls = []

        class Platform:
            def call(self, code, *strings, **options):
                calls.append((code, strings, options))

        apps.close_all(Platform())
        self.assertEqual(calls, [(13, ('com.openwave.tinh.action.CLOSE_APPS', ''), {})])


class IdleTests(unittest.TestCase):
    def test_each_idle_stamp_is_reported_once(self):
        self.assertEqual(apps.idle_event(0, 0), (None, 0))
        self.assertEqual(apps.idle_event(41, 0), ({'event': 'idle'}, 41))
        self.assertEqual(apps.idle_event(41, 41), (None, 41))        # still idle: nothing new
        self.assertEqual(apps.idle_event(0, 41), (None, 41))         # an app is in front
        self.assertEqual(apps.idle_event(43, 41), ({'event': 'idle'}, 43))

    def test_untrusted_readings_are_ignored(self):
        for value in (-1, None, 'x', 1.5):
            self.assertEqual(apps.idle_event(value, 7), (None, 7))


class TasksTests(unittest.TestCase):
    def test_running_packages_are_validated_and_deduplicated(self):
        event = apps.tasks_event('com.android.calculator2,com.android.deskclock,com.android.calculator2')
        self.assertEqual(event, {'event': 'tasks', 'packages': ['com.android.calculator2', 'com.android.deskclock']})

    def test_untrusted_task_text_is_dropped(self):
        self.assertEqual(apps.tasks_event('')['packages'], [])
        self.assertEqual(apps.tasks_event(None)['packages'], [])
        self.assertEqual(apps.tasks_event('not a package,../etc,com.openwave.tinh,com.example.ok\n')['packages'], [])
        self.assertEqual(apps.tasks_event('com.example.ok,' + 'x' * 5000)['packages'], [])

    def test_close_one_is_a_fixed_intent_with_the_package(self):
        calls = []

        class Platform:
            def call(self, code, *strings, **options):
                calls.append((code, strings, options))

        apps.close_one(Platform(), 'com.android.calculator2')
        self.assertEqual(calls, [(13, ('com.openwave.tinh.action.CLOSE_APPS', 'package:com.android.calculator2'), {})])

