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
        self.assertEqual(result[1]['name'], 'com.android.settings')

    def test_handlers_are_parsed_and_validated(self):
        text = 'org.mozilla.firefox=https,http;com.example.mail=mailto,evil;../x=http;com.example.none=;org.lineageos.setupwizard=http'
        self.assertEqual(apps.parse_handlers(text), {'org.mozilla.firefox': ['http', 'https'], 'com.example.mail': ['mailto']})
        self.assertEqual(apps.parse_handlers(None), {})
        self.assertEqual(apps.parse_handlers('a.b=http;' * 10000), {})

    def test_catalogue_adds_schemes_only_for_known_handlers(self):
        result = apps.catalogue([entry('org.mozilla.firefox', 'Firefox'), entry('com.android.deskclock', 'Clock')],
                                icon_reader=lambda package: b'', handlers={'org.mozilla.firefox': ['http', 'https']})
        by_package = {e['package']: e for e in result}
        self.assertEqual(by_package['org.mozilla.firefox']['schemes'], ['http', 'https'])
        self.assertNotIn('schemes', by_package['com.android.deskclock'])

    def test_removable_packages_are_parsed_and_validated(self):
        self.assertEqual(apps.parse_removable('org.a.one,../x,org.lineageos.setupwizard,com.b.two,bad'), {'org.a.one', 'com.b.two'})
        self.assertEqual(apps.parse_removable(None), set())
        self.assertEqual(apps.parse_removable('a.b,' * 40000), set())

    def test_catalogue_marks_only_removable_apps(self):
        result = apps.catalogue([entry('org.mozilla.firefox', 'Firefox'), entry('com.android.deskclock', 'Clock')],
                                icon_reader=lambda package: b'', removable={'org.mozilla.firefox'})
        by_package = {e['package']: e for e in result}
        self.assertTrue(by_package['org.mozilla.firefox']['removable'])
        self.assertNotIn('removable', by_package['com.android.deskclock'])

    def test_remove_succeeds_when_the_package_disappears_whatever_the_code(self):
        class Fake:
            def __init__(self): self.calls = 0
            def remove_report(self): return ''
            def remove(self, package): return 1
            def apps(self):
                self.calls += 1
                return [{'packageName': 'org.a.one'}] if self.calls < 3 else [{'packageName': 'org.b.two'}]
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None)
        self.assertEqual(result, {'removed': True, 'code': 1})

    def test_remove_reports_a_package_that_stays_installed(self):
        class Fake:
            def remove_report(self): return ''
            def remove(self, package): return -1
            def apps(self): return [{'packageName': 'org.a.one'}]
        ticks = iter(range(0, 10000))
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None, clock=lambda: next(ticks))
        self.assertEqual(result['removed'], False)
        self.assertEqual(result['code'], -1)
        self.assertIn('did not answer', result['detail'])

    def test_remove_reports_android_rejecting_the_call(self):
        class Fake:
            def remove_report(self): return ''
            def remove(self, package): raise apps.Failure('Android rejected transaction 13 (-1)')
            def apps(self): return []
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None)
        self.assertEqual(result, {'removed': False, 'detail': 'Android rejected transaction 13 (-1)'})

    def test_remove_stops_at_once_when_the_user_cancels(self):
        class Fake:
            def __init__(self): self.reports = iter(['', 'org.a.one,3,1700'])
            def remove_report(self): return next(self.reports, 'org.a.one,3,1700')
            def remove(self, package): return 0
            def apps(self): return [{'packageName': 'org.a.one'}]
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None)
        self.assertEqual(result, {'removed': False, 'cancelled': True})

    def test_remove_reports_a_refusal_with_the_status(self):
        class Fake:
            def __init__(self): self.reports = iter(['', 'org.a.one,2,1700'])
            def remove_report(self): return next(self.reports, 'org.a.one,2,1700')
            def remove(self, package): return 0
            def apps(self): return [{'packageName': 'org.a.one'}]
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None)
        self.assertEqual(result['removed'], False)
        self.assertEqual(result['code'], 2)

    def test_an_old_report_for_the_same_package_is_ignored(self):
        class Fake:
            def __init__(self): self.calls = 0
            def remove_report(self): return 'org.a.one,3,1000'
            def remove(self, package): return 0
            def apps(self):
                self.calls += 1
                return [{'packageName': 'org.a.one'}] if self.calls < 3 else []
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None)
        self.assertEqual(result, {'removed': True, 'code': 0})

    def test_a_report_for_another_package_is_ignored(self):
        class Fake:
            def __init__(self): self.reports = iter(['', 'org.b.two,3,1700'])
            def remove_report(self): return next(self.reports, 'org.b.two,3,1700')
            def remove(self, package): return 0
            def apps(self): return [{'packageName': 'org.a.one'}]
        ticks = iter(range(0, 10000))
        result = apps.remove_package(Fake(), 'org.a.one', sleep=lambda seconds: None, clock=lambda: next(ticks))
        self.assertIn('did not answer', result['detail'])

    def test_a_crash_while_removing_is_reported_with_its_location(self):
        try:
            {}['missing']
        except KeyError as error:
            detail = apps.remove_crash_detail(error)
        self.assertTrue(detail.startswith("KeyError: 'missing' at "))
        self.assertIn('test_android_apps.py', detail)
        self.assertLessEqual(len(detail), 120)

    def test_the_removal_alarm_becomes_a_reported_failure(self):
        with self.assertRaises(apps.Failure):
            apps.remove_timed_out(14, None)
        self.assertIn('signal.signal(signal.SIGALRM, remove_timed_out)', SOURCE.read_text())

    def test_an_unset_remove_report_is_never_read_as_a_string(self):
        class Reader:
            def __init__(self, values):
                self.values = list(values)

            def read_int32(self):
                return True, self.values.pop(0)

            def read_string16(self):
                raise AssertionError('reading a null string16 crashes the binder binding')

        class Reply:
            def __init__(self, values):
                self.values = values

            def init_reader(self):
                return Reader(self.values)

        platform = object.__new__(apps.Platform)
        platform.transact = lambda *args, **kwargs: (Reply([0, -1]), Reader([-1]))
        self.assertEqual(platform.remove_report(), '')

    def test_a_set_remove_report_is_read_after_the_length_check(self):
        class Reader:
            def __init__(self, values, text):
                self.values = list(values)
                self.text = text

            def read_int32(self):
                return True, self.values.pop(0)

            def read_string16(self):
                return self.text

        class Reply:
            def init_reader(self):
                return Reader([0], 'pkg.name,0,5')

        platform = object.__new__(apps.Platform)
        platform.transact = lambda *args, **kwargs: (Reply(), Reader([12], ''))
        self.assertEqual(platform.remove_report(), 'pkg.name,0,5')

    def test_remove_reports_are_parsed_strictly(self):
        self.assertEqual(apps.parse_remove_report('org.a.one,0,17'), ('org.a.one', 0, 17))
        for bad in ('', None, 'org.a.one,x,1', 'org.a.one,0', '../x,0,1', 'a,b,c,d', 'org.a.one,0,1' + 'x' * 300):
            self.assertIsNone(apps.parse_remove_report(bad), bad)

    def test_only_allowed_links_are_accepted(self):
        for good, token in (('https://example.com/a?b=c', 'https'), ('http://example.com', 'http'), ('mailto:a@example.com', 'mailto'),
                            ('tel:+15551234', 'tel'), ('sms:+15551234', 'sms'), ('smsto:+15551234', 'sms'), ('HTTPS://EXAMPLE.COM', 'https')):
            self.assertEqual(apps.link_token(good), token, good)
        for bad in ('', 'file:///etc/passwd', 'content://x/y', 'intent://x#Intent;end', 'javascript:alert(1)', 'https://a b',
                    'https://a\nb', 'https://a\x00b', 'https://' + 'a' * 3000, 'example.com', None, 5, 'market://details?id=a.b'):
            self.assertIsNone(apps.link_token(bad), bad)

    def test_view_target_names_the_package_and_encodes_the_link(self):
        target = apps.view_target('org.mozilla.firefox', 'https://example.com/?a=b&c=d#frag')
        self.assertEqual(target, 'openwave-view://org.mozilla.firefox/?u=https%3A%2F%2Fexample.com%2F%3Fa%3Db%26c%3Dd%23frag')

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
        if apps.os.geteuid():
            self.assertEqual(apps.main(['launch', 'com.android.deskclock']), 126)


class PatientIconTests(unittest.TestCase):
    def reader_with(self, appears_after, wait=1.5, step=0.1):
        now = [0.0]
        calls = []

        def clock():
            return now[0]

        def sleep(seconds):
            now[0] += seconds

        def reader(package):
            calls.append(package)
            return b'png' if now[0] >= appears_after else b''

        return apps.patient_icon_reader(wait, step, reader, sleep, clock), calls, now

    def test_an_icon_that_appears_during_the_wait_is_returned(self):
        read, calls, _ = self.reader_with(0.35)
        self.assertEqual(read('com.example.app'), b'png')
        self.assertGreater(len(calls), 1)

    def test_an_icon_that_never_appears_stops_waiting_at_the_deadline(self):
        read, _, now = self.reader_with(10.0)
        self.assertEqual(read('com.example.app'), b'')
        self.assertLessEqual(now[0], 1.6)

    def test_the_wait_budget_is_shared_between_packages(self):
        read, _, now = self.reader_with(10.0)
        read('com.example.one')
        spent = now[0]
        read('com.example.two')
        self.assertEqual(now[0], spent)

    def test_an_icon_already_present_costs_nothing(self):
        read, calls, now = self.reader_with(0.0)
        self.assertEqual(read('com.example.app'), b'png')
        self.assertEqual((len(calls), now[0]), (1, 0.0))


class RemoveIntentTests(unittest.TestCase):
    def test_remove_asks_tinh_through_a_fixed_intent(self):
        calls = []

        platform = apps.Platform.__new__(apps.Platform)
        platform.call = lambda code, *strings, **options: calls.append((code, strings, options))
        self.assertEqual(apps.Platform.remove(platform, 'org.mozilla.firefox'), 0)
        self.assertEqual(calls, [(13, ('com.openwave.tinh.action.REMOVE_APP', 'package:org.mozilla.firefox'), {})])


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
        self.assertEqual(apps.idle_event(41, 41), (None, 41))
        self.assertEqual(apps.idle_event(0, 41), (None, 41))
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

