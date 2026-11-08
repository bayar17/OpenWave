"""Host-only checks for explicit SELinux permissive debug and AVC summaries."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from openwave_selinux_avc_report import analyze_avcs
from openwave_mount_preflight_report import evaluate

AVC0 = ('audit: type=1400 audit(1794143229.511:92): avc:  denied '
        '{ getattr } for pid=1771 comm="python3.14" '
        'path="/usr/libexec/openwave-waydroid/waydroid-mount" '
        'scontext=system_u:system_r:openwave_system_t:s0 '
        'tcontext=system_u:object_r:openwave_waydroid_mount_exec_t:s0 '
        'tclass=file permissive=0')
AVC1 = AVC0.replace('permissive=0', 'permissive=1')


class SelinuxDebugTests(unittest.TestCase):
    def invalid_cli(self, *args):
        return subprocess.run(
            [sys.executable, str(ROOT / 'scripts/test-openwave-boot.py'),
             '--image', '/does/not/exist.img',
             '--output', '/does/not/exist/output', *args],
            capture_output=True, text=True, timeout=10)

    def test_debug_requires_explicit_permissive_preflight(self):
        for flags in (('--selinux-debug',),
                      ('--selinux-debug', '--mount-preflight'),
                      ('--selinux-debug', '--mode', 'permissive'),
                      ('--mount-preflight', '--mode', 'permissive')):
            with self.subTest(flags=flags):
                result = self.invalid_cli(*flags)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('error:', result.stderr)

    def test_other_suites_still_reject_permissive(self):
        for suite in ('--waydroid', '--app-launch', '--portal'):
            with self.subTest(suite=suite):
                r = self.invalid_cli(suite, '--mode', 'permissive')
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertIn('require enforcing mode', r.stderr)

    def test_avc_aggregation_counts_permissions_and_domains(self):
        result = analyze_avcs(AVC0 + '\n' + AVC1 + '\n')
        self.assertEqual(result['total_avc_records'], 2)
        self.assertEqual(result['permissive_flags'], {'0': 1, '1': 1})
        self.assertEqual(result['groups'][0]['source_domain'], 'openwave_system_t')
        self.assertEqual(result['groups'][0]['target_type'], 'openwave_waydroid_mount_exec_t')
        self.assertEqual(result['groups'][0]['class'], 'file')
        self.assertEqual(result['groups'][0]['permissions'], 'getattr')
        self.assertEqual(result['groups'][0]['count'], 2)
        self.assertFalse(result['auto_policy_changes'])

    def test_avc_summary_no_records_is_not_enforcement_success(self):
        result = analyze_avcs('MOUNT_QA_END prerequisites=BLOCKED\n')
        self.assertEqual(result['total_avc_records'], 0)
        self.assertTrue(result['evidence_only'])

    def test_interleaved_audit_is_not_accepted_as_gate_detail(self):
        serial = ('MOUNT_QA_BEGIN enforcing=0\n'
                  'MOUNT_QA_GATE helper_inode=BLOCKED owner=0 ' + AVC0 + '\n'
                  'MOUNT_QA_END prerequisites=BLOCKED\n')
        result = evaluate(serial)
        self.assertEqual(result['gates']['helper_inode']['detail'], 'owner=0')
        self.assertFalse(result['preflight_passed'])
        self.assertFalse(result['release_ready'])

    def test_debug_options_do_not_execute_mount_calls(self):
        guest = (ROOT / 'userspace/openrc/qa/waydroid-mount-preflight.start').read_text()
        self.assertIn('openwave.qa=mount-preflight', guest)
        self.assertIn('loop_block_nodes', guest)
        self.assertNotIn('os.system(', guest)
        self.assertNotIn('mknod(', guest)
        self.assertNotIn('modprobe ', guest)
        self.assertNotIn('umount(', guest)
        self.assertIn('mount_syscalls=NOT_ATTEMPTED', guest)


if __name__ == '__main__':
    unittest.main()
