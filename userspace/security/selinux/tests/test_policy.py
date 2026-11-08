"""Compile real policy mutations to verify that security invariants fail closed."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
TRIPLE = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
BUILD = Path(os.environ.get('OPENWAVE_POLICY_BUILD',
    ROOT / 'out/boot-build' / TRIPLE / 'policy'))
POLICY = ROOT / 'userspace/security/selinux/openwave.cil'


class PolicyTests(unittest.TestCase):
    def test_waydroid_network_authority_is_separate_and_path_scoped(self):
        result = self.compile('''
            (neverallow openwave_waydroid_manager_t self (capability (net_admin net_raw sys_admin)))
            (neverallow openwave_waydroid_manager_t self (unix_dgram_socket (ioctl)))
            (neverallow openwave_waydroid_net_t self (capability (sys_admin sys_boot sys_time)))
            (neverallow openwave_waydroid_net_t openwave_network_sysctl_t (file (write)))
            (neverallow openwave_waydroid_net_t openwave_interface_sysctl_t (file (write)))
            (neverallow openwave_waydroid_net_t openwave_proc_t (file (write)))
            (neverallow openwave_waydroid_net_t openwave_runtime_t (file (write create)))
            (neverallow openwave_waydroid_net_t openwave_waydroid_state_t (file (write create execute)))
            (neverallow openwave_waydroid_net_t openwave_waydroid_user_t (file (all)))
            (neverallow openwave_waydroid_net_t openwave_private_t (file (all)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(neverallow openwave_waydroid_net_t self (capability (net_admin)))',
            '(neverallow openwave_waydroid_net_t self (unix_dgram_socket (ioctl)))',
            '(allow openwave_waydroid_net_t self (capability (sys_admin)))',
            '(allow openwave_app_t openwave_waydroid_net_exec_t (file (execute)))',
            '(allow openwave_app_t openwave_waydroid_forwarding_t (file (write)))',
        ):
            result = self.compile(rule)
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn('neverallow', result.stderr)

    def test_waydroid_manager_can_start_without_host_administration_authority(self):
        result = self.compile('''
            (neverallow openwave_waydroid_manager_t self (capability (sys_admin sys_boot sys_time)))
            (neverallow openwave_waydroid_manager_t openwave_all_types (security (load_policy setenforce)))
            (neverallow openwave_waydroid_manager_t openwave_runtime_t (file (write create unlink)))
            (neverallow openwave_waydroid_manager_t openwave_waydroid_state_t (file (execute)))
            (neverallow openwave_waydroid_manager_t openwave_accounts_t (file (write create unlink rename)))
            (neverallow openwave_waydroid_manager_t openwave_waydroid_user_t (file (execute)))
            (neverallow openwave_waydroid_manager_t openwave_waydroid_snapshot_t (file (execute_no_trans entrypoint)))
            (neverallow openwave_system_t self (memfd_file (execute)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.compile('(neverallow openwave_system_t openwave_waydroid_manager_t (process (transition)))')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('neverallow', result.stderr)

    def test_waydroid_callback_mappings_cannot_become_program_entrypoints(self):
        result = self.compile('(neverallow openwave_waydroid_manager_t openwave_waydroid_snapshot_t (file (execute)))')
        self.assertNotEqual(result.returncode, 0, result.stderr)
        for permission in ('execute_no_trans', 'entrypoint'):
            result = self.compile(f'(allow openwave_waydroid_manager_t openwave_waydroid_snapshot_t (file ({permission})))')
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn('neverallow', result.stderr)

    def test_waydroid_log_queries_cannot_enable_filesystem_shutdown(self):
        for target in ('openwave_runtime_t', 'openwave_service_run_t'):
            result = self.compile(f'(allowx openwave_waydroid_manager_t {target} (ioctl file (0x587d)))')
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn('neverallowx', result.stderr)

    def test_binder_initialization_does_not_open_generic_or_application_devices(self):
        result = self.compile('''
            (neverallow openwave_system_t openwave_default_t (chr_file (read write open ioctl)))
            (neverallow openwave_system_t openwave_binderfs_t (chr_file (read write open ioctl)))
            (neverallow openwave_app_t openwave_binder_control_t (chr_file (all)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(neverallow openwave_system_t openwave_binder_control_t (chr_file (ioctl)))',
            '(allow openwave_app_t openwave_binderfs_t (chr_file (open)))',
        ):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

    def test_waydroid_provisioning_does_not_enable_container_privileges(self):
        result = self.compile('''
            (neverallow openwave_waydroid_manager_t openwave_all_types (security (load_policy setenforce)))
            (neverallow openwave_waydroid_manager_t self (capability (sys_admin sys_boot)))
            (neverallow openwave_appd_t openwave_waydroid_storage (file (write create execute)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(allow openwave_app_t openwave_waydroid_user_t (file (read)))',
            '(allow openwave_app_t openwave_waydroid_state_t (dir (write)))',
            '(neverallow openwave_system_t openwave_waydroid_runtime_t (dir (create)))',
        ):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

    def compile(self, rule=''):
        self.assertTrue((BUILD / 'compile-policy').is_file(), 'Run ./build-openwave selinux first')
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            policy = directory / 'policy.cil'
            policy.write_text(POLICY.read_text() + '\n' + rule + '\n')
            return subprocess.run([str(BUILD / 'compile-policy'), str(directory / 'policy.35'),
                                   str(BUILD / 'kernel.cil'), str(BUILD / 'services.cil'), str(policy)],
                                  capture_output=True, text=True)

    def test_policy_compiles(self):
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compositor_can_receive_read_write_client_cursor_descriptors(self):
        # SCM_RIGHTS imports check the descriptor's O_RDWR access even when
        # Wayland only maps the cursor pixels. Assert effective compiled rules,
        # including the native-renderer attribute expansion.
        for client in ('openwave_app_t', 'openwave_settings_t', 'openwave_files_t'):
            with self.subTest(client=client):
                result = self.compile(
                    f'(neverallow openwave_shell_t {client} (memfd_file (write)))')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

    def test_privileged_grants_are_rejected(self):
        rules = ['(allow openwave_app_t openwave_private_t (file (read)))',
                 '(allow openwave_app_t openwave_security_t (security (load_policy)))',
                 '(allow openwave_appd_t openwave_security_t (security (load_policy)))',
                 '(allow openwave_appd_t openwave_security_t (security (setenforce)))',
                 '(allow openwave_app_t self (capability (sys_admin)))',
                 '(allow openwave_app_t self (capability (net_bind_service)))',
                 '(allow openwave_app_t openwave_interface_sysctl_t (file (write)))',
                 '(allow openwave_app_t openwave_system_t (process (transition)))',
                 '(allow openwave_power_t self (capability (sys_boot)))',
                 '(allow openwave_shell_t openwave_power_fifo_t (fifo_file (write)))',
                 '(allow openwave_app_t openwave_power_fifo_t (fifo_file (open)))',
                 '(allow openwave_system_t openwave_runtime_t (system (firmware_load)))',
                 '(allow openwave_networkmanager_t openwave_firmware_t (file (write)))']
        for rule in rules:
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

    def test_portal_relay_cannot_open_paths_or_mutate_shell_profiles(self):
        # Existing SCM_RIGHTS descriptors need their original access mode in
        # the relay. It cannot open paths, nor may the picker mutate homes.
        result = self.compile('''
            (neverallow openwave_dbus_t openwave_user_data_t (file (open append create unlink rename setattr)))
            (neverallow openwave_dbus_t openwave_app_data_t (file (open write append create)))
            (neverallow openwave_shell_t openwave_user_data_t (file (write append create unlink rename setattr)))
            (neverallow openwave_shell_t openwave_user_data_t (dir (write add_name remove_name create)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_download_relay_and_installer_permissions_are_scoped(self):
        for required in (
            '(neverallow openwave_dbus_t openwave_user_data_t (file (write)))',
            '(neverallow openwave_portal_t openwave_user_data_t (file (create)))',
            '(neverallow openwave_appd_t openwave_user_bundle_t (file (create)))',
            '(neverallow openwave_app_t openwave_user_app_exec_t (file (entrypoint)))',
        ):
            with self.subTest(rule=required):
                result = self.compile(required)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)
        result = self.compile('''
            (neverallow openwave_appd_t openwave_runtime_t (file (write create unlink rename)))
            (neverallow openwave_appd_t openwave_app_exec_t (file (write create unlink rename)))
            (neverallow openwave_portal_t openwave_app_data_t (file (write create)))
            (neverallow openwave_app_t openwave_user_bundle_t (file (write create)))
            (neverallow openwave_app_t openwave_user_app_exec_t (file (write create)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_confined_app_executable_verification_does_not_grant_tracing(self):
        for broker in ('openwave_portal_t', 'openwave_promptd_t'):
            with self.subTest(broker=broker):
                for object_class in ('lnk_file', 'file'):
                    result = self.compile(f'(neverallow {broker} openwave_app_t ({object_class} (read)))')
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('neverallow', result.stderr)
                result = self.compile(f'''
                    (neverallow {broker} openwave_app_t (file (open write)))
                    (neverallow {broker} openwave_app_t (process (ptrace)))
                ''')
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_uninstall_quarantine_and_settings_power_remain_scoped(self):
        result = self.compile('(neverallow openwave_appd_t openwave_app_data_t (dir (rename)))')
        self.assertNotEqual(result.returncode, 0)
        result = self.compile('''
            (neverallow openwave_settings_t openwave_power_fifo_t (fifo_file (all)))
            (neverallow openwave_settings_t openwave_system_t (process (signal sigkill)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_network_profiles_remain_private_while_dns_is_readable(self):
        result = self.compile('(allow openwave_app_t openwave_networkmanager_state_t (file (read)))')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('neverallow', result.stderr)
        result = self.compile('(neverallow openwave_app_t openwave_dns_config_t (file (read)))')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('neverallow', result.stderr)

    def test_linker_objects_are_readonly_for_confined_clients(self):
        result = self.compile('''
            (neverallow openwave_app_t openwave_elf_loader_t
                (file (write append create setattr unlink rename)))
            (neverallow openwave_lxc_t openwave_shared_lib_t
                (file (write append create setattr unlink rename)))
            (neverallow openwave_waydroid_manager_t openwave_shared_lib_t
                (file (write append create setattr unlink rename)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(allow openwave_app_t openwave_elf_loader_t (file (write)))',
            '(allow openwave_appd_t openwave_shared_lib_t (file (setattr)))',
            '(allow openwave_lxc_t openwave_shared_lib_t (file (write)))',
            '(allow openwave_waydroid_manager_t openwave_elf_loader_t (file (append)))',
            '(allow openwave_waydroid_net_t openwave_shared_lib_t (file (relabelto)))',
        ):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn('neverallow', result.stderr)

    def test_binary_inspector_rejects_missing_loader_rule_and_attribute_write(self):
        mutations = [re.sub(
            r'\(allow openwave_dynamic_link_client openwave_elf_loader_t\s+\(file \([^)]*\)\)\)',
            '', POLICY.read_text()), POLICY.read_text() +
            '\n(allow openwave_native_service openwave_runtime_t (file (write)))\n']
        for text in mutations:
            with self.subTest(), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                source = directory / 'mutated.cil'; source.write_text(text)
                binary = directory / 'policy.35'
                subprocess.run([str(BUILD / 'compile-policy'), str(binary),
                    str(BUILD / 'kernel.cil'), str(BUILD / 'services.cil'), str(source)],
                    check=True, capture_output=True)
                result = subprocess.run([str(BUILD / 'inspect-qa-policy'), str(binary)],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn('QA_POLICY FAIL', result.stdout)


if __name__ == '__main__':
    unittest.main()
