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

    def test_manager_service_success_does_not_hide_failed_container_start(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['stages']['manager'], 'PASS')
        self.assertEqual(result['stages']['session_start'], 'FAIL')
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertFalse(result['framework_passed'])

    def test_guest_qa_collects_failed_lxc_and_overlay_evidence_read_only(self):
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        script = (root / 'userspace/openrc/qa/waydroid.start').read_text()
        self.assertIn('WAYDROID_QA overlay_path=%s status=missing-or-inaccessible', script)
        self.assertIn('WAYDROID_QA mountinfo START', script)
        self.assertIn('WAYDROID_QA LXC_LOG', script)
        self.assertNotIn('mkdir -p /var/lib/waydroid/overlay', script)
        self.assertNotIn('setenforce 0', script)
        self.assertNotIn('MNT_DETACH', script)
        import subprocess
        check = subprocess.run(['/bin/sh', '-n', str(root / 'userspace/openrc/qa/waydroid.start')], capture_output=True, text=True)
        self.assertEqual(check.returncode, 0, check.stderr)

    def test_lxc_policy_root_access_is_search_only(self):
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        policy = (root / 'userspace/security/selinux/openwave.cil').read_text()
        self.assertEqual(policy.count(
            '(allow openwave_lxc_t openwave_default_t (dir (search)))'), 1)
        self.assertNotIn(
            '(allow openwave_lxc_t openwave_default_t (dir (read open write search)))',
            policy)
        self.assertNotIn(
            '(allow openwave_lxc_t openwave_default_t (file (read open)))', policy)

    def test_lxc_libunwind_loader_denial_is_reported_separately(self):
        log = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'WAYDROID_QA LOG /var/lib/waydroid/waydroid.log\n'
            + 'lxc-start: error while loading shared libraries: libunwind.so.1: '
              'cannot open shared object file: Permission denied\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(log)
        self.assertEqual(result['stages']['lxc_loader'], 'FAIL')
        self.assertEqual(result['lxc_failed_library'], 'libunwind.so.1')
        self.assertEqual(result['stages']['session_start'], 'FAIL')
        self.assertFalse(result['framework_passed'])

    def test_missing_lxc_loader_error_does_not_count_as_pass(self):
        result = evaluate('WAYDROID_QA BEGIN enforcing=1\n'
                          + probe('container', 'STOPPED')
                          + 'WAYDROID_QA END enforcing=1\n')
        self.assertEqual(result['stages']['lxc_loader'], 'UNVERIFIED')
        self.assertIsNone(result['lxc_failed_library'])

    def test_lxc_runtime_directory_access_is_search_and_getattr_only(self):
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        policy = (root / 'userspace/security/selinux/openwave.cil').read_text()
        self.assertEqual(policy.count(
            '(allow openwave_lxc_t openwave_runtime_t (dir (search getattr)))'), 1)
        self.assertNotIn(
            '(allow openwave_lxc_t openwave_runtime_t (dir (read open search)))', policy)
        self.assertIn(
            '(allow openwave_lxc_t openwave_default_t (dir (search)))', policy)

    def test_empty_successful_commands_and_missing_container_are_failures(self):
        result = evaluate(probe('container', '') + probe('boot_completed', ''))
        self.assertFalse(result['framework_passed'])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertEqual(result['stages']['boot_completed'], 'BLOCKED')
