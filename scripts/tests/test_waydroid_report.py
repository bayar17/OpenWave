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

    def test_lxc_config_access_denial_reports_exact_path(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'WAYDROID_QA LOG /var/lib/waydroid/waydroid.log\n'
            + '(1916) lxc-start -P /var/lib/waydroid/lxc -F -n waydroid\n'
            + 'lxc-start: waydroid: ../../../third_party/lxc-6.0.6/'
              'src/lxc/tools/lxc_start.c: lxc_start_main: 191 '
              'You lack access to /var/lib/waydroid/lxc\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['stages']['lxc_config_access'], 'FAIL')
        self.assertEqual(result['lxc_denied_config_path'], '/var/lib/waydroid/lxc')
        self.assertFalse(result['framework_passed'])

    def test_lxc_config_access_is_unverified_without_container_evidence(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('container', 'STOPPED')
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['stages']['lxc_config_access'], 'UNVERIFIED')
        self.assertIsNone(result['lxc_denied_config_path'])

    def test_lxc_host_traversal_permission_scope(self):
        from pathlib import Path
        policy = (Path(__file__).resolve().parents[2] /
                  'userspace/security/selinux/openwave.cil').read_text()
        for typ in ('openwave_private_t', 'openwave_proc_t', 'openwave_sysfs_t'):
            self.assertEqual(policy.count(
                '(allow openwave_lxc_t ' + typ + ' (dir (search)))'), 1)
            self.assertNotIn('(allow openwave_lxc_t ' + typ +
                             ' (dir (read open search)))', policy)
            self.assertNotIn('(allow openwave_lxc_t ' + typ +
                             ' (file (read open)))', policy)

    def test_lxc_var_lib_search_denial_is_typed_and_does_not_certify_boot(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'audit: type=1400 audit(1794168547.994:141): avc:  denied  { search } '
              'for pid=1951 comm="lxc-start" name="lib" dev="vda" ino=10050 '
              'scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_state_t:s0 '
              'tclass=dir permissive=0\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['lxc_search_denial_types'], ['openwave_state_t'])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertEqual(result['stages']['lxc_config_access'], 'UNVERIFIED')
        self.assertFalse(result['framework_passed'])

    def test_search_denial_types_do_not_include_non_lxc_processes(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + 'audit: avc: denied { search } for pid=55 comm="python3" '
              'scontext=system_u:system_r:openwave_waydroid_manager_t:s0 '
              'tcontext=system_u:object_r:openwave_private_t:s0 '
              'tclass=dir permissive=0\n'
            + probe('container', 'STOPPED')
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['lxc_search_denial_types'], [])
        self.assertFalse(result['framework_passed'])

    def test_lxc_var_lib_state_parent_grant_is_search_only(self):
        from pathlib import Path
        policy = (Path(__file__).resolve().parents[2] /
                  'userspace/security/selinux/openwave.cil').read_text()
        self.assertEqual(policy.count(
            '(allow openwave_lxc_t openwave_state_t (dir (search)))'), 1)
        self.assertNotIn(
            '(allow openwave_lxc_t openwave_state_t (dir (read open search)))', policy)
        self.assertNotIn(
            '(allow openwave_lxc_t openwave_state_t (file (read open)))', policy)

    def test_lxc_create_cache_and_metadata_denials_are_reported(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'audit: avc: denied { create } for pid=1957 comm="lxc-start" '
              'name=".cache" scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_waydroid_state_t:s0 '
              'tclass=dir permissive=0\n'
            + 'audit: avc: denied { getattr } for pid=1957 comm="lxc-start" '
              'name="/" scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_security_t:s0 '
              'tclass=filesystem permissive=0\n'
            + 'audit: avc: denied { read } for pid=1957 comm="lxc-start" '
              'name="filesystems" scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_proc_t:s0 '
              'tclass=file permissive=0\n'
            + 'audit: avc: denied { read } for pid=1859 comm="python3.14" '
              'name="build.prop" scontext=system_u:system_r:openwave_waydroid_manager_t:s0 '
              'tcontext=system_u:object_r:openwave_default_t:s0 '
              'tclass=file permissive=0\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertFalse(result['framework_passed'])
        self.assertEqual(result['lxc_search_denial_types'], [])
        self.assertEqual(result['lxc_avc_denials'], [
            {'permissions': ['read'], 'target_type': 'openwave_proc_t',
             'target_class': 'file'},
            {'permissions': ['getattr'], 'target_type': 'openwave_security_t',
             'target_class': 'filesystem'},
            {'permissions': ['create'], 'target_type': 'openwave_waydroid_state_t',
             'target_class': 'dir'},
        ])

    def test_lxc_create_is_scoped_to_waydroid_state_and_metadata_only(self):
        from pathlib import Path
        policy = (Path(__file__).resolve().parents[2] /
                  'userspace/security/selinux/openwave.cil').read_text()
        self.assertIn(
            '(allow openwave_lxc_t openwave_waydroid_state_t\n'
            '       (dir (search read open getattr write add_name remove_name create)))',
            policy)
        self.assertIn('(allow openwave_lxc_t openwave_run_t (dir (search getattr)))', policy)
        self.assertIn(
            '(allow openwave_lxc_t openwave_security_t (filesystem (getattr)))',
            policy)
        self.assertNotIn('(allow openwave_lxc_t openwave_run_t (dir (read open)))',
                         policy)
        self.assertNotIn('(allow openwave_lxc_t openwave_proc_t (file (read open)))',
                         policy)
        self.assertNotIn('(allow openwave_lxc_t openwave_security_t '
                         '(security (load_policy)))', policy)

    def test_lxc_run_root_search_denial_is_reported_and_never_pass(self):
        avc = ('audit: type=1400 audit(1794171770.524:138): '
               'avc:  denied  { search } for pid=1968 comm="lxc-start" '
               'name="/" dev="tmpfs" ino=1 '
               'scontext=system_u:system_r:openwave_lxc_t:s0 '
               'tcontext=system_u:object_r:openwave_run_t:s0 '
               'tclass=dir permissive=0\n')
        serial = ('WAYDROID_QA BEGIN enforcing=1\n'
                  + probe('startup', 'Starting waydroid [ ok ]')
                  + probe('request_session', 'OSError: container failed to start', 1)
                  + probe('container', 'STOPPED')
                  + avc + avc
                  + 'WAYDROID_QA END enforcing=1\n')
        result = evaluate(serial)
        self.assertEqual(result['lxc_search_denial_types'], ['openwave_run_t'])
        self.assertEqual(result['lxc_avc_denials'], [
            {'permissions': ['search'], 'target_type': 'openwave_run_t',
             'target_class': 'dir'}])
        self.assertFalse(result['framework_passed'])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertEqual(result['stages']['session_start'], 'FAIL')

    def test_lxc_run_root_policy_does_not_grant_run_directory_listing(self):
        from pathlib import Path
        policy = (Path(__file__).resolve().parents[2] /
                  'userspace/security/selinux/openwave.cil').read_text()
        self.assertEqual(
            policy.count('(allow openwave_lxc_t openwave_run_t (dir (search getattr)))'),
            1)
        for forbidden in (
            '(allow openwave_lxc_t openwave_run_t (dir (read open)))',
            '(allow openwave_lxc_t openwave_run_t (file (open read)))',
        ):
            self.assertNotIn(forbidden, policy)

    def test_lxc_anonymous_memfd_denial_has_exact_type_and_class(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'audit: type=1400 audit(1794174210.992:138): '
              'avc: denied { create } for pid=1962 comm="lxc-start" '
              'anonclass=[memfd] '
              'scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_lxc_t:s0 '
              'tclass=memfd_file permissive=0\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['lxc_search_denial_types'], [])
        self.assertEqual(result['lxc_avc_denials'], [
            {'permissions': ['create'], 'target_type': 'openwave_lxc_t',
             'target_class': 'memfd_file'}])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertFalse(result['framework_passed'])
        self.assertFalse(result['integration_passed'])

    def test_lxc_memfd_policy_is_self_create_write_without_execmem(self):
        from pathlib import Path
        policy = (Path(__file__).resolve().parents[2] /
                  'userspace/security/selinux/openwave.cil').read_text()
        self.assertEqual(
            policy.count('(allow openwave_lxc_t self (memfd_file (create write)))'),
            1)
        self.assertNotIn(
            '(allow openwave_lxc_t self (memfd_file (create read write open)))',
            policy)
        self.assertNotIn(
            '(allow openwave_lxc_t self (process (execmem)))', policy)
        self.assertEqual(policy.count(
            '(unix_stream_socket (create bind listen accept read write connect connectto))'),
            1)

    def test_lxc_memfd_config_write_and_self_socket_connect_denials(self):
        serial = (
            'WAYDROID_QA BEGIN enforcing=1\n'
            + probe('startup', 'Starting waydroid [ ok ]')
            + probe('request_session', 'OSError: container failed to start', 1)
            + probe('container', 'STOPPED')
            + 'audit: type=1400 audit(1794178039.835:138): '
              'avc: denied { write } for pid=2012 comm="lxc-start" '
              'path=2F6D656D66643A2E6C78635F636F6E6669675F66696C65202864656C6574656429 '
              'dev="tmpfs" ino=99 '
              'scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:object_r:openwave_lxc_t:s0 '
              'tclass=memfd_file permissive=0\n'
            + 'audit: type=1400 audit(1794178039.843:139): '
              'avc: denied { connect } for pid=2012 comm="lxc-start" '
              'scontext=system_u:system_r:openwave_lxc_t:s0 '
              'tcontext=system_u:system_r:openwave_lxc_t:s0 '
              'tclass=unix_stream_socket permissive=0\n'
            + 'WAYDROID_QA END enforcing=1\n'
        )
        result = evaluate(serial)
        self.assertEqual(result['lxc_search_denial_types'], [])
        self.assertEqual(result['lxc_avc_denials'], [
            {'permissions': ['write'], 'target_type': 'openwave_lxc_t',
             'target_class': 'memfd_file'},
            {'permissions': ['connect'], 'target_type': 'openwave_lxc_t',
             'target_class': 'unix_stream_socket'},
        ])
        self.assertEqual(result['stages']['session_start'], 'FAIL')
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertFalse(result['framework_passed'])

    def test_empty_successful_commands_and_missing_container_are_failures(self):
        result = evaluate(probe('container', '') + probe('boot_completed', ''))
        self.assertFalse(result['framework_passed'])
        self.assertEqual(result['stages']['container'], 'FAIL')
        self.assertEqual(result['stages']['boot_completed'], 'BLOCKED')
