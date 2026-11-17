"""Android payload domain: separation from the LXC monitor and from the host."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'waydroid'))
import test_policy
from prepare_apparmor_free import ANDROID_CONTEXT

POLICY = test_policy.POLICY


class AndroidPayloadTests(unittest.TestCase):
    compile = test_policy.PolicyTests.compile

    def holds(self, rules):
        result = self.compile(rules)
        self.assertEqual(result.returncode, 0, result.stderr)

    def required(self, *rules):
        for rule in rules:
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0, 'grant is missing: ' + rule)
                self.assertIn('neverallow', result.stderr)

    def test_release_policy_has_no_permissive_domain(self):
        self.assertNotIn('typepermissive', POLICY.read_text())

    def test_lxc_template_context_names_the_payload_domain(self):
        self.assertEqual(ANDROID_CONTEXT, 'system_u:system_r:openwave_android_t:s0')
        source = POLICY.read_text()
        self.assertIn('(type openwave_android_t)', source)
        self.assertIn('(roletype system_r openwave_android_t)', source)

    def test_payload_is_entered_only_from_the_monitor_or_an_administrator(self):
        self.required(
            '(neverallow openwave_lxc_t openwave_android_t (process (transition)))',
            '(neverallow openwave_lxc_t self (process (setexec)))',
            '(neverallow openwave_lxc_t openwave_android_t (process2 (nnp_transition)))',
            '(neverallow openwave_android_t openwave_android_system_t (file (entrypoint)))')
        self.holds('''
            (neverallow openwave_app_t openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_native_service openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_waydroid_manager_t openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_waydroid_net_t openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_android_t openwave_lxc_t (process (transition dyntransition)))
            (neverallow openwave_android_t openwave_system_t (process (transition dyntransition)))
            (neverallow openwave_android_t openwave_waydroid_manager_t (process (transition dyntransition)))
            (neverallow openwave_android_t openwave_default_t (file (entrypoint)))
            (neverallow openwave_android_t openwave_runtime_t (file (entrypoint)))
        ''')

    def test_android_files_have_their_own_types(self):
        source = POLICY.read_text()
        for line in (
                '(genfscon erofs / (system_u object_r openwave_android_system_t ((s0) (s0))))',
                '(genfscon overlay / (system_u object_r openwave_android_system_t ((s0) (s0))))',
                '(typetransition openwave_android_tmpfs_creator openwave_runtime_t file openwave_android_tmpfs_t)',
                '(typetransition openwave_android_tmpfs_creator openwave_runtime_t dir openwave_android_tmpfs_t)',
                '(typetransition openwave_android_tmpfs_creator openwave_runtime_t chr_file openwave_android_tmpfs_t)'):
            self.assertIn(line, source)
        kernel = (POLICY.parent / 'kernel-policy.c').read_text()
        self.assertIn('!strcmp(name, "unlabeled")', kernel)
        self.assertIn('"openwave_unlabeled_t"', kernel)

    def test_payload_cannot_modify_or_execute_host_content(self):
        self.holds('''
            (neverallow openwave_android_t openwave_default_t
                        (file (execute execute_no_trans entrypoint write append create unlink rename setattr)))
            (neverallow openwave_android_t openwave_default_t (dir (write add_name remove_name create rmdir)))
            (neverallow openwave_android_t openwave_runtime_t
                        (file (execute execute_no_trans entrypoint write append create unlink rename setattr)))
            (neverallow openwave_android_t openwave_runtime_t (dir (write add_name remove_name create rmdir)))
            (neverallow openwave_android_t openwave_elf_loader_t (file (all)))
            (neverallow openwave_android_t openwave_shared_lib_t (file (all)))
            (neverallow openwave_android_t openwave_app_data_t (file (all)))
            (neverallow openwave_android_t openwave_user_data_t (file (all)))
            (neverallow openwave_android_t openwave_private_t (file (all)))
            (neverallow openwave_android_t openwave_state_t (file (all)))
            (neverallow openwave_android_t openwave_waydroid_state_t (file (all)))
            (neverallow openwave_android_t openwave_lxc_run_t (file (all)))
            (neverallow openwave_android_t openwave_accounts_t (file (all)))
            (neverallow openwave_android_t openwave_android_image
                        (file (write append create unlink rename setattr)))
        ''')

    def test_payload_has_no_policy_kernel_log_or_module_authority(self):
        self.holds('''
            (neverallow openwave_android_t openwave_all_types (security (load_policy setenforce setbool)))
            (neverallow openwave_android_t openwave_security_t (file (write append)))
            (neverallow openwave_android_t openwave_selinux_enforce_t (file (write append)))
            (neverallow openwave_android_t openwave_system_t
                        (system (syslog_read syslog_mod syslog_console module_load module_request)))
            (neverallow openwave_android_t self (capability (sys_module sys_boot sys_rawio)))
            (neverallow openwave_android_t self (capability2 (mac_admin mac_override)))
            (neverallow openwave_android_t openwave_all_types (process (ptrace)))
        ''')

    def test_payload_device_access_is_limited_to_named_types(self):
        self.holds('''
            (neverallow openwave_android_t openwave_device_t
                        (chr_file (read write open ioctl map append setattr create unlink rename link)))
            (neverallow openwave_android_t openwave_device_t (blk_file (all)))
            (neverallow openwave_android_t openwave_loop_block_t (blk_file (all)))
            (neverallow openwave_android_t openwave_loop_control_t (chr_file (all)))
            (neverallow openwave_android_t openwave_binder_control_t (chr_file (all)))
            (neverallow openwave_android_t openwave_input_t (chr_file (all)))
            (neverallow openwave_android_t openwave_gpu_t (chr_file (read write open ioctl map setattr)))
        ''')
        self.required(
            '(neverallow openwave_android_t openwave_binderfs_t (chr_file (ioctl)))',
            '(neverallow openwave_android_t self (binder (set_context_mgr)))')

    def test_payload_audio_and_display_use_only_the_shared_sockets(self):
        self.holds('''
            (neverallow openwave_android_t openwave_pipewire_runtime_t (sock_file (all)))
            (neverallow openwave_android_t openwave_dbus_runtime_t (sock_file (all)))
            (neverallow openwave_android_t openwave_dbus_t (unix_stream_socket (connectto)))
            (neverallow openwave_android_t openwave_appd_t (unix_stream_socket (connectto)))
            (neverallow openwave_android_t openwave_app_t (unix_stream_socket (connectto)))
        ''')
        self.required(
            '(neverallow openwave_android_t openwave_pulse_runtime_t (sock_file (write)))',
            '(neverallow openwave_android_t openwave_pipewire_t (unix_stream_socket (connectto)))',
            '(neverallow openwave_android_t openwave_shell_t (unix_stream_socket (connectto)))')

    def test_monitor_and_manager_cannot_run_android_or_host_programs(self):
        self.holds('''
            (neverallow openwave_lxc_t openwave_runtime_t (file (execute execute_no_trans)))
            (neverallow openwave_lxc_t openwave_android_image (file (execute_no_trans entrypoint write)))
            (neverallow openwave_lxc_t self (capability (sys_module sys_boot sys_rawio)))
            (neverallow openwave_waydroid_manager_t openwave_android_image
                        (file (execute_no_trans entrypoint write append create)))
            (neverallow openwave_waydroid_manager_t openwave_gpu_t (chr_file (setattr write)))
        ''')

    def test_coldboot_writes_reach_only_loop_and_misc_device_classes(self):
        source = POLICY.read_text()
        for line in (
                '(genfscon sysfs /devices/virtual/block (system_u object_r openwave_sysfs_android_device_t ((s0) (s0))))',
                '(genfscon sysfs /devices/virtual/misc (system_u object_r openwave_sysfs_android_device_t ((s0) (s0))))'):
            self.assertIn(line, source)
        # Exactly two sysfs subtrees carry the writable type.
        self.assertEqual(source.count('object_r openwave_sysfs_android_device_t'), 2)
        self.holds('''
            (neverallow openwave_android_t openwave_sysfs_t (file (write append create setattr)))
            (neverallow openwave_android_t openwave_sysfs_t (dir (write add_name create)))
            (neverallow openwave_app_t openwave_sysfs_android_device_t (file (write append)))
        ''')
        self.required(
            '(neverallow openwave_android_t openwave_sysfs_android_device_t (file (write)))')

    def test_native_sysfs_rules_are_mirrored_for_the_relabeled_subtrees(self):
        import re
        flat = ' '.join(POLICY.read_text().split())
        native = set(re.findall(r'\(allow (\w+) openwave_sysfs_t \((dir|file|lnk_file) ', flat))
        mirrored = set(re.findall(r'\(allow (\w+) openwave_sysfs_android_device_t \((dir|file|lnk_file) ', flat))
        native.discard(('openwave_android_t', 'dir')); native.discard(('openwave_android_t', 'file'))
        native.discard(('openwave_android_t', 'lnk_file'))
        self.assertTrue(native, 'no native sysfs rules found')
        self.assertEqual(native - mirrored, set())

    def test_native_applications_get_nothing_from_the_android_domain(self):
        self.holds('''
            (neverallow openwave_app_t openwave_android_t (process (all)))
            (neverallow openwave_app_t openwave_android_t (binder (all)))
            (neverallow openwave_app_t openwave_android_image (file (execute execute_no_trans entrypoint)))
            (neverallow openwave_app_t openwave_android_tmpfs_t (file (all)))
            (neverallow openwave_app_t openwave_waydroid_shared_t (file (all)))
            (neverallow openwave_app_t openwave_pulse_runtime_t (sock_file (all)))
            (neverallow openwave_app_t openwave_pipewire_runtime_t (sock_file (all)))
        ''')


if __name__ == '__main__':
    unittest.main()
