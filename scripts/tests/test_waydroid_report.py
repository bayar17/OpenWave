"""An empty successful diagnostic must never certify Android framework boot."""
from scripts.openwave_waydroid_report import evaluate
import unittest


def probe(name, value, status=0):
    return f'WAYDROID_PROBE_BEGIN {name}\n{value}\nWAYDROID_PROBE_END {name} exit={status}\n'


class WaydroidReportTests(unittest.TestCase):
    def test_complete_framework_still_does_not_certify_graphics_or_back(self):
        serial = 'WAYDROID_QA BEGIN enforcing=1\n' + ''.join(probe(*item) for item in (
            ('startup', 'ready'), ('container', 'RUNNING'), ('servicemanager', 'running'),
            ('zygote', 'running'), ('system_server', '123'), ('boot_completed', '1'),
            ('packages', 'package:com.android.settings'))) + 'WAYDROID_QA END enforcing=1\n'
        result = evaluate(serial)
        self.assertTrue(result['framework_passed'])
        self.assertFalse(result['integration_passed'])
        self.assertEqual(result['stages']['graphics'], 'NOT TESTED')
        for old, new in [('package:com.android.settings', ''), ('\n1\n', '\n\n'),
                         ('\n123\n', '\n0\n'), ('\n123\n', '\n\n'),
                         ('enforcing=1', 'enforcing=0'), ('exit=0', 'exit=124')]:
            with self.subTest(old=old):
                self.assertFalse(evaluate(serial.replace(old, new))['framework_passed'])

    def test_empty_successful_commands_and_missing_container_are_failures(self):
        result = evaluate(probe('container', '') + probe('boot_completed', ''))
        self.assertFalse(result['framework_passed'])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertEqual(result['stages']['boot_completed'], 'BLOCKED')
