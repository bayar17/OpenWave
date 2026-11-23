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
    def test_waydroid_mount_preflight_observer_cannot_execute_or_control_device(self):
        allowed = self.compile('''
            (neverallow openwave_system_t openwave_waydroid_mount_exec_t
                        (file (execute execute_no_trans entrypoint write append setattr)))
            (neverallow openwave_system_t openwave_loop_control_t
                        (chr_file (open read write ioctl setattr)))
        ''')
        self.assertEqual(allowed.returncode, 0, allowed.stderr)
        for denied in (
            '(neverallow openwave_system_t openwave_waydroid_mount_exec_t (file (read)))',
            '(neverallow openwave_system_t openwave_waydroid_mount_exec_t (file (getattr)))',
            '(neverallow openwave_system_t openwave_loop_control_t (chr_file (getattr)))',
        ):
            with self.subTest(rule=denied):
                result = self.compile(denied)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn('neverallow', result.stderr)

    def assert_exact_permissions(self, source, target, cls, permissions):
        wanted = ' '.join(permissions)
        baseline = ' associate' if cls == 'filesystem' else ''
        result = self.compile(f'(neverallow {source} {target} ({cls} (not ({wanted}{baseline}))))')
        self.assertEqual(result.returncode, 0,
                         f'{source} -> {target}:{cls} exceeds {{{wanted}}}\n{result.stderr}')
        for permission in permissions:
            with self.subTest(source=source, target=target, cls=cls, permission=permission):
                missing = self.compile(f'(neverallow {source} {target} ({cls} ({permission})))')
                self.assertNotEqual(missing.returncode, 0,
                                    f'{source} -> {target}:{cls} {permission} is not granted')
                self.assertIn('neverallow', missing.stderr)

    def assert_no_permissions(self, source, target, *classes):
        rules = '\n'.join(
            f'(neverallow {source} {target} ({cls} (not (associate))))' if cls == 'filesystem'
            else f'(neverallow {source} {target} ({cls} (all)))' for cls in classes)
        result = self.compile(rules)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_lxc_host_root_is_traversal_and_pivot_mountpoint_only(self):
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_default_t', 'dir',
                                      ('search', 'mounton'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_default_t', 'filesystem',
                                      ('getattr', 'unmount'))
        self.assert_no_permissions('openwave_lxc_t', 'openwave_default_t',
                                   'file', 'lnk_file', 'chr_file', 'blk_file',
                                   'sock_file', 'fifo_file')

    def test_lxc_runtime_dirs_are_traversal_and_rootfs_mountpoint_only(self):
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_runtime_t', 'dir',
                                      ('search', 'getattr', 'mounton'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_runtime_t', 'filesystem',
                                      ('getattr', 'mount', 'remount'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_runtime_t', 'lnk_file',
                                      ('read',))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_runtime_t', 'file',
                                      ('open', 'read', 'getattr', 'map'))

    def test_lxc_private_proc_sysfs_scopes_match_container_setup(self):
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_private_t', 'dir', ('search',))
        self.assert_no_permissions('openwave_lxc_t', 'openwave_private_t',
                                   'file', 'lnk_file', 'sock_file', 'filesystem')
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_proc_t', 'dir',
                                      ('search', 'mounton', 'open', 'read'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_proc_t', 'file', ('open', 'read'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_proc_t', 'filesystem',
                                      ('mount', 'remount'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_sysfs_t', 'dir',
                                      ('search', 'getattr', 'mounton'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_sysfs_t', 'file',
                                      ('getattr', 'open', 'read'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_sysfs_t', 'filesystem',
                                      ('mount',))

    def test_lxc_var_lib_state_parent_is_search_only(self):
        # Stage 2M got through /var, /sys, /proc and reached the
        # openwave_state_t-labeled /var/lib parent. Waydroid data itself
        # remains under the dedicated openwave_waydroid_state_t label.
        safe = self.compile("""
            (neverallow openwave_lxc_t openwave_state_t
                        (dir (read open write create add_name remove_name
                              setattr mounton relabelfrom relabelto)))
            (neverallow openwave_lxc_t openwave_state_t
                        (file (read open write create execute execute_no_trans
                               append unlink setattr)))
        """)
        self.assertEqual(safe.returncode, 0, safe.stderr)
        denied = self.compile(
            '(neverallow openwave_lxc_t openwave_state_t (dir (search)))'
        )
        self.assertNotEqual(denied.returncode, 0, denied.stderr)
        self.assertIn('neverallow', denied.stderr)

    def test_lxc_state_cache_and_read_only_host_metadata_boundary(self):
        # Stage 2N no longer has a search denial, but lxc-start needs to
        # create its .cache directory in the Waydroid state type. Only the
        # previously scoped state type is allowed directory creation.
        allowed = self.compile("""
            (neverallow openwave_lxc_t openwave_waydroid_state_t
                        (dir (setattr relabelfrom relabelto mounton)))
            (neverallow openwave_lxc_t openwave_run_t
                        (dir (read open write create add_name remove_name
                              setattr mounton)))
            (neverallow openwave_lxc_t openwave_security_t
                        (filesystem (mount remount unmount)))
            (neverallow openwave_lxc_t openwave_security_t
                        (security (load_policy setenforce setbool)))
            (neverallow openwave_lxc_t openwave_proc_t
                        (file (write create setattr)))
        """)
        self.assertEqual(allowed.returncode, 0, allowed.stderr)
        for rule in (
            '(neverallow openwave_lxc_t openwave_waydroid_state_t (dir (create)))',
            '(neverallow openwave_lxc_t openwave_run_t (dir (getattr)))',
            '(neverallow openwave_lxc_t openwave_security_t (filesystem (getattr)))',
        ):
            with self.subTest(rule=rule):
                denied = self.compile(rule)
                self.assertNotEqual(denied.returncode, 0, denied.stderr)
                self.assertIn('neverallow', denied.stderr)

    def test_lxc_run_root_traversal_is_search_and_metadata_only(self):
        # Stage 2O got through the state .cache create denial, then
        # lxc-start was denied search on /run (openwave_run_t:dir).
        # This grant does not allow directory enumeration or mutation.
        allowed = self.compile("""
            (neverallow openwave_lxc_t openwave_run_t
                        (dir (read open write create add_name remove_name
                              setattr mounton relabelfrom relabelto)))
            (neverallow openwave_lxc_t openwave_run_t
                        (file (open read write create execute execute_no_trans
                               append unlink setattr)))
            (neverallow openwave_lxc_t openwave_run_t
                        (lnk_file (read write create unlink)))
        """)
        self.assertEqual(allowed.returncode, 0, allowed.stderr)
        for denied in (
            '(neverallow openwave_lxc_t openwave_run_t (dir (search)))',
            '(neverallow openwave_lxc_t openwave_run_t (dir (getattr)))',
        ):
            with self.subTest(rule=denied):
                fail = self.compile(denied)
                self.assertNotEqual(fail.returncode, 0, fail.stderr)
                self.assertIn('neverallow', fail.stderr)

    def test_lxc_config_memfds_are_dedicated_and_never_executable(self):
        self.assertIn('(typetransition openwave_lxc_t openwave_lxc_t memfd_file openwave_lxc_memfd_t)',
                      POLICY.read_text())
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_lxc_memfd_t', 'memfd_file',
                                      ('create', 'write', 'read', 'getattr', 'map'))
        safe = self.compile("""
            (neverallow openwave_lxc_t self (memfd_file (all)))
            (typeattribute qa_memfd_not_lxc)
            (typeattributeset qa_memfd_not_lxc (and (all) (not (openwave_lxc_memfd_t))))
            (neverallow openwave_lxc_t qa_memfd_not_lxc (memfd_file (all)))
            (typeattribute qa_domain_not_lxc)
            (typeattributeset qa_domain_not_lxc (and (all) (not (openwave_lxc_t))))
            (neverallow qa_domain_not_lxc openwave_lxc_memfd_t (memfd_file (all)))
            (neverallow openwave_lxc_t self (process (execmem execstack execheap)))
            (neverallow openwave_lxc_t self
                        (unix_stream_socket (ioctl shutdown setattr)))
        """)
        self.assertEqual(safe.returncode, 0, safe.stderr)
        connect = self.compile('(neverallow openwave_lxc_t self (unix_stream_socket (connect)))')
        self.assertNotEqual(connect.returncode, 0, connect.stderr)

    def test_lxc_monitor_and_android_payload_stay_separate(self):
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_android_t', 'process',
                                      ('transition', 'sigkill', 'signal', 'noatsecure',
                                       'rlimitinh', 'siginh'))
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_android_system_t', 'file',
                                      ('execute', 'open', 'read', 'getattr'))
        self.assert_no_permissions('openwave_lxc_t', 'openwave_android_t',
                                   'binder', 'unix_stream_socket', 'memfd_file', 'file', 'dir')
        self.assert_no_permissions('openwave_android_t', 'openwave_lxc_t',
                                   'process', 'process2', 'unix_stream_socket', 'unix_dgram_socket',
                                   'memfd_file', 'file', 'dir', 'binder')
        self.assert_no_permissions('openwave_android_t', 'openwave_lxc_memfd_t', 'memfd_file')
        self.assert_no_permissions('openwave_android_t', 'openwave_lxc_run_t',
                                   'dir', 'file', 'sock_file')
        self.assert_no_permissions('openwave_android_t', 'openwave_lxc_exec_t', 'file')
        self.assert_no_permissions('openwave_android_t', 'openwave_lxc_start_exec_t', 'file')
        for rule in ('(neverallow openwave_system_t openwave_lxc_t (fd (use)))',
                     '(neverallow openwave_system_t openwave_cgroup_t (dir (ioctl)))'):
            with self.subTest(rule=rule):
                required = self.compile(rule)
                self.assertNotEqual(required.returncode, 0, required.stderr)
        separate = self.compile("""
            (neverallow openwave_lxc_t openwave_android_system_t (file (execute_no_trans entrypoint write)))
            (neverallow openwave_android_t openwave_lxc_t (process (transition dyntransition)))
            (neverallow openwave_lxc_t openwave_android_t (process (dyntransition ptrace)))
        """)
        self.assertEqual(separate.returncode, 0, separate.stderr)

    def test_only_the_attach_domain_enters_android_and_runs_its_own_memfd(self):
        source = POLICY.read_text()
        self.assertIn('(typetransition openwave_system_t openwave_lxc_attach_exec_t process openwave_lxc_attach_t)',
                      source)
        self.assertIn('(typetransition openwave_lxc_attach_t openwave_lxc_attach_t memfd_file openwave_lxc_attach_memfd_t)',
                      source)
        self.assertIn('/usr/bin/lxc\\-attach system_u:object_r:openwave_lxc_attach_exec_t:s0',
                      (POLICY.parent / 'file_contexts').read_text())
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_lxc_attach_memfd_t', 'memfd_file',
                                      ('create', 'write', 'read', 'open', 'getattr', 'setattr',
                                       'map', 'execute', 'execute_no_trans'))
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_lxc_attach_t', 'capability',
                                      ('setgid', 'setpcap', 'sys_chroot', 'sys_admin', 'dac_override'))
        self.assert_no_permissions('openwave_lxc_attach_t', 'openwave_default_t',
                                   'file', 'lnk_file', 'chr_file', 'blk_file', 'filesystem')
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_default_t', 'dir', ('search',))
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_runtime_t', 'dir',
                                      ('search', 'getattr'))
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_waydroid_state_t', 'file',
                                      ('getattr', 'open', 'read'))
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_android_t', 'process',
                                      ('transition', 'getattr', 'signull', 'sigkill', 'signal',
                                       'noatsecure', 'rlimitinh', 'siginh'))
        safe = self.compile("""
            (neverallow openwave_system_t self (memfd_file (execute execute_no_trans)))
            (neverallow openwave_system_t openwave_lxc_attach_memfd_t (memfd_file (all)))
            (neverallow openwave_lxc_attach_t self (memfd_file (execute execute_no_trans)))
            (neverallow openwave_system_t openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_system_t openwave_android_t (process2 (all)))
            (neverallow openwave_waydroid_manager_t openwave_android_t (process (transition dyntransition)))
            (neverallow openwave_app_t openwave_lxc_attach_exec_t (file (execute execute_no_trans entrypoint)))
            (neverallow openwave_native_service openwave_lxc_attach_t (process (transition)))
            (neverallow openwave_android_t openwave_lxc_attach_t (process (all)))
            (neverallow openwave_android_t openwave_lxc_attach_memfd_t (memfd_file (all)))
            (neverallow openwave_lxc_attach_t self (capability2 (mac_admin mac_override)))
            (neverallow openwave_lxc_attach_t openwave_all_types (security (load_policy setenforce setbool)))
        """)
        self.assertEqual(safe.returncode, 0, safe.stderr)

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

    def test_appd_can_control_confined_app_but_not_waydroid_mount_helper(self):
        # appd-to-app control socket is the only new Stage 2H target.
        valid = self.compile('''
            (neverallow openwave_appd_t openwave_waydroid_mount_t
                        (unix_stream_socket (connectto)))
            (neverallow openwave_appd_t openwave_waydroid_net_t
                        (unix_stream_socket (connectto)))
        ''')
        self.assertEqual(valid.returncode, 0, valid.stderr)
        rejected = self.compile('''
            (neverallow openwave_appd_t openwave_app_t
                        (unix_stream_socket (connectto)))
        ''')
        self.assertNotEqual(rejected.returncode, 0, rejected.stderr)
        self.assertIn('neverallow', rejected.stderr)

    def test_mount_helper_is_the_only_waydroid_mount_authority(self):
        result = self.compile('''
            (neverallow openwave_waydroid_mount_t openwave_all_types
                        (security (load_policy setenforce)))
            (neverallow openwave_waydroid_mount_t openwave_default_t
                        (filesystem (mount remount unmount)))
            (neverallow openwave_app_t openwave_waydroid_mount_exec_t
                        (file (execute entrypoint execute_no_trans)))
            (neverallow openwave_waydroid_mount_t self (capability2 (all)))
            (neverallow openwave_waydroid_mount_t openwave_android_t
                        (process (transition dyntransition)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'self', 'capability',
                                      ('sys_admin', 'dac_override', 'dac_read_search'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_default_t',
                                      'filesystem', ('getattr', 'relabelfrom'))
        self.assert_no_permissions('openwave_waydroid_mount_t', 'openwave_unlabeled_t',
                                   'filesystem')
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_android_system_t',
                                      'filesystem', ('getattr', 'mount', 'unmount',
                                                     'relabelfrom', 'relabelto'))
        only_manager = self.compile('''
            (typeattribute qa_not_manager)
            (typeattributeset qa_not_manager (and (all) (not (openwave_waydroid_manager_t))))
            (neverallow qa_not_manager openwave_waydroid_mount_t (process (transition dyntransition)))
            (typeattribute qa_not_manager_or_helper)
            (typeattributeset qa_not_manager_or_helper (and (all)
                (not (openwave_waydroid_manager_t openwave_waydroid_mount_t))))
            (neverallow qa_not_manager_or_helper openwave_waydroid_mount_exec_t
                        (file (execute execute_no_trans entrypoint)))
        ''')
        self.assertEqual(only_manager.returncode, 0, only_manager.stderr)
        for rule in (
            '(allow openwave_waydroid_mount_t openwave_default_t (filesystem (mount)))',
            '(allow openwave_waydroid_mount_t openwave_default_t (filesystem (remount)))',
            '(allow openwave_waydroid_mount_t openwave_default_t (filesystem (unmount)))',
            '(allow openwave_waydroid_mount_t self (capability (sys_module)))',
            '(allow openwave_waydroid_mount_t self (capability (net_admin)))',
            '(allow openwave_waydroid_mount_t openwave_private_t (security (setenforce)))',
            '(allow openwave_app_t openwave_waydroid_mount_exec_t (file (execute)))',
            '(allow openwave_waydroid_mount_t openwave_android_system_t (file (execute_no_trans)))',
            '(neverallow openwave_waydroid_manager_t openwave_waydroid_mount_exec_t (file (execute)))',
        ):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

    def test_waydroid_manager_has_no_mount_authority(self):
        result = self.compile('''
            (neverallow openwave_waydroid_manager_t self (capability (sys_admin)))
            (neverallow openwave_waydroid_manager_t openwave_all_types
                        (filesystem (mount remount unmount relabelfrom relabelto)))
            (neverallow openwave_waydroid_manager_t openwave_all_types (dir (mounton)))
            (neverallow openwave_waydroid_manager_t openwave_all_types (file (mounton)))
            (neverallow openwave_waydroid_manager_t openwave_loop_control_t (chr_file (all)))
            (neverallow openwave_waydroid_manager_t openwave_loop_block_t (blk_file (all)))
            (neverallow openwave_waydroid_manager_t openwave_android_image
                        (file (execute execute_no_trans map write)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(allow openwave_waydroid_manager_t self (capability (sys_admin)))',
            '(allow openwave_waydroid_manager_t openwave_default_t (filesystem (mount)))',
            '(allow openwave_waydroid_manager_t openwave_android_system_t (filesystem (mount)))',
            '(allow openwave_waydroid_manager_t openwave_waydroid_state_t (dir (mounton)))',
            '(allow openwave_waydroid_manager_t openwave_loop_control_t (chr_file (open)))',
        ):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

    def test_android_network_sysctls_are_its_own_and_not_forwarding(self):
        self.assert_exact_permissions('openwave_android_t', 'openwave_interface_sysctl_t', 'file',
                                      {'getattr', 'open', 'read', 'write'})
        self.assert_exact_permissions('openwave_android_t', 'openwave_waydroid_forwarding_t', 'file', {'write'})
        self.assert_no_permissions('openwave_android_t', 'openwave_waydroid_bridge_sysctl_t', 'file')
        mutated = self.compile('(allow openwave_app_t openwave_interface_sysctl_t (file (write)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        self.assertIn('neverallow', mutated.stderr)

    def test_android_dhcp_needs_only_its_own_packet_socket(self):
        self.assert_exact_permissions('openwave_android_t', 'openwave_android_t', 'packet_socket',
                                      {'bind', 'create', 'getopt', 'read', 'setopt', 'write'})
        self.assert_no_permissions('openwave_app_t', 'openwave_app_t', 'packet_socket')

    def test_android_outbound_sockets_are_its_own_and_unprivileged_apps_gain_nothing(self):
        for cls, perms in (('icmp_socket', {'bind', 'connect', 'create', 'getattr', 'getopt', 'read', 'setopt', 'write'}),):
            self.assert_exact_permissions('openwave_android_t', 'openwave_android_t', cls, perms)
        self.assert_no_permissions('openwave_android_t', 'openwave_app_t', 'tcp_socket')
        self.assert_no_permissions('openwave_android_t', 'openwave_appd_t', 'tcp_socket')
        self.assert_no_permissions('openwave_android_t', 'openwave_system_t', 'tcp_socket')

    def test_android_shared_storage_is_confined_to_android_and_files(self):
        share = 'openwave_android_share_t'
        self.assert_exact_permissions('openwave_android_t', share, 'dir', {
            'add_name', 'create', 'getattr', 'ioctl', 'mounton', 'open', 'read', 'remove_name', 'rename',
            'reparent', 'rmdir', 'search', 'setattr', 'watch', 'write'})
        self.assert_exact_permissions('openwave_android_t', share, 'file', {
            'append', 'create', 'getattr', 'ioctl', 'lock', 'map', 'open', 'read', 'rename',
            'setattr', 'unlink', 'write'})
        self.assert_exact_permissions('openwave_android_t', share, 'lnk_file', {'getattr', 'read', 'unlink'})
        self.assert_no_permissions('openwave_android_t', 'openwave_user_data_t', 'file')
        for domain in ('openwave_shell_t', 'openwave_portal_t', 'openwave_appd_t', 'openwave_app_t',
                       'openwave_settings_t', 'openwave_waydroid_apps_t', 'openwave_waydroid_manager_t',
                       'openwave_lxc_t'):
            with self.subTest(domain=domain):
                self.assert_no_permissions(domain, share, 'dir', 'file', 'lnk_file')
        self.assert_exact_permissions('openwave_system_t', share, 'dir', {
            'create', 'getattr', 'mounton', 'open', 'read', 'relabelfrom', 'relabelto', 'search', 'setattr'})
        self.assert_exact_permissions('openwave_system_t', share, 'file', {
            'getattr', 'relabelfrom', 'relabelto', 'setattr'})
        for rule in ('(allow openwave_android_t openwave_android_share_t (file (execute)))',
                     '(allow openwave_android_t openwave_android_share_t (lnk_file (create)))',
                     '(allow openwave_android_t openwave_android_share_t (chr_file (create)))',
                     '(allow openwave_system_t openwave_android_share_t (file (read)))',
                     '(allow openwave_system_t openwave_android_share_t (file (write)))'):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

    def test_libraries_of_installed_applications_can_be_loaded_but_never_changed_or_started(self):
        library = 'openwave_user_shared_lib_t'
        self.assert_exact_permissions('openwave_app_t', library, 'file',
                                      {'read', 'open', 'getattr', 'map', 'execute', 'watch'})
        for permission in ('write', 'append', 'create', 'unlink', 'rename', 'setattr', 'relabelto', 'relabelfrom'):
            with self.subTest(permission=permission):
                mutated = self.compile(f'(allow openwave_app_t {library} (file ({permission})))')
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)
        mutated = self.compile(f'(allow openwave_appdeploy_t {library} (file (execute)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        self.assertIn('neverallow', mutated.stderr)

    def test_settings_can_read_but_never_change_the_appearance_store(self):
        store = 'openwave_wallpaper_state_t'
        self.assert_exact_permissions('openwave_settings_t', store, 'file', {'read', 'open', 'getattr', 'map'})
        self.assert_exact_permissions('openwave_settings_t', store, 'dir', {'read', 'open', 'search', 'getattr'})
        self.assert_exact_permissions('openwave_shell_t', store, 'file', {'read', 'open', 'getattr', 'map'})
        self.assert_no_permissions('openwave_app_t', store, 'file', 'dir')
        self.assert_no_permissions('openwave_files_t', store, 'file', 'dir')
        for permission in ('write', 'append', 'create', 'unlink', 'rename', 'setattr'):
            with self.subTest(permission=permission):
                mutated = self.compile(f'(neverallow openwave_settings_t {store} (file ({permission})))')
                self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_appd_alone_writes_the_private_preferences_directory(self):
        state = 'openwave_shell_state_t'
        self.assert_exact_permissions('openwave_appd_t', state, 'file',
                                      {'read', 'open', 'getattr', 'create', 'write', 'unlink', 'rename', 'setattr'})
        self.assert_exact_permissions('openwave_appd_t', state, 'dir',
                                      {'search', 'getattr', 'read', 'open', 'write', 'add_name', 'remove_name'})
        for domain in ('openwave_settings_t', 'openwave_app_t', 'openwave_files_t', 'openwave_xdg_portal_t'):
            for permission in ('write', 'create', 'unlink', 'rename'):
                with self.subTest(domain=domain, permission=permission):
                    mutated = self.compile(f'(neverallow {domain} {state} (file ({permission})))')
                    self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_only_android_may_change_proc_cmdline_attributes(self):
        cmdline = 'openwave_proc_cmdline_t'
        self.assert_exact_permissions('openwave_android_t', cmdline, 'file',
                                      {'getattr', 'open', 'read', 'setattr'})
        self.assert_exact_permissions('openwave_android_t', 'openwave_proc_t', 'file',
                                      {'getattr', 'open', 'read'})
        self.assert_exact_permissions('openwave_lxc_t', cmdline, 'file', {'open', 'read'})
        for domain in ('openwave_lxc_t', 'openwave_app_t', 'openwave_settings_t', 'openwave_files_t',
                       'openwave_system_t', 'openwave_waydroid_manager_t', 'openwave_waydroid_net_t'):
            for permission in ('write', 'append', 'setattr'):
                with self.subTest(domain=domain, permission=permission):
                    mutated = self.compile(f'(neverallow {domain} {cmdline} (file ({permission})))')
                    self.assertEqual(mutated.returncode, 0, mutated.stderr)
        mutated = self.compile(f'(neverallow openwave_android_t {cmdline} (file (write append create unlink rename)))')
        self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_only_android_may_write_the_memory_pressure_trigger(self):
        pressure = 'openwave_proc_pressure_memory_t'
        self.assert_exact_permissions('openwave_android_t', pressure, 'file',
                                      {'getattr', 'open', 'read', 'write'})
        self.assert_exact_permissions('openwave_android_t', 'openwave_proc_t', 'file',
                                      {'getattr', 'open', 'read'})
        self.assert_exact_permissions('openwave_lxc_t', pressure, 'file', {'open', 'read'})
        for domain in ('openwave_lxc_t', 'openwave_app_t', 'openwave_settings_t', 'openwave_files_t',
                       'openwave_system_t', 'openwave_waydroid_manager_t', 'openwave_waydroid_net_t'):
            for permission in ('write', 'append', 'setattr'):
                with self.subTest(domain=domain, permission=permission):
                    mutated = self.compile(f'(neverallow {domain} {pressure} (file ({permission})))')
                    self.assertEqual(mutated.returncode, 0, mutated.stderr)
        mutated = self.compile(f'(neverallow openwave_android_t {pressure} (file (append create unlink rename setattr)))')
        self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_android_may_mount_over_its_image_files_but_never_modify_them(self):
        image = 'openwave_android_system_t'
        self.assert_exact_permissions('openwave_android_t', image, 'file',
                                      {'entrypoint', 'getattr', 'open', 'read', 'map', 'execute',
                                       'execute_no_trans', 'lock', 'mounton'})
        for domain in ('openwave_app_t', 'openwave_settings_t', 'openwave_files_t',
                       'openwave_system_t', 'openwave_waydroid_manager_t', 'openwave_waydroid_net_t'):
            with self.subTest(domain=domain):
                mutated = self.compile(f'(neverallow {domain} {image} (file (mounton)))')
                self.assertEqual(mutated.returncode, 0, mutated.stderr)
        mutated = self.compile(f'(neverallow openwave_android_t {image} (file (write append create unlink rename setattr)))')
        self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_android_may_use_the_gpu_render_node_but_not_reconfigure_it(self):
        gpu = 'openwave_gpu_t'
        self.assert_exact_permissions('openwave_android_t', gpu, 'chr_file',
                                      {'getattr', 'read', 'write', 'open', 'ioctl', 'map'})
        self.assert_no_permissions('openwave_android_t', gpu, 'dir', 'file', 'lnk_file', 'sock_file')
        mutated = self.compile(f'(neverallow openwave_android_t {gpu} (chr_file (setattr append create unlink rename link)))')
        self.assertEqual(mutated.returncode, 0, mutated.stderr)
        for domain in ('openwave_waydroid_manager_t', 'openwave_waydroid_net_t', 'openwave_lxc_t'):
            for permission in ('read', 'write', 'open', 'ioctl', 'map'):
                with self.subTest(domain=domain, permission=permission):
                    mutated = self.compile(f'(neverallow {domain} {gpu} (chr_file ({permission})))')
                    self.assertEqual(mutated.returncode, 0, mutated.stderr)

    def test_appdeploy_helper_is_started_only_by_appd_and_cannot_run_package_code(self):
        helper = 'openwave_appdeploy_t'
        self.assert_exact_permissions('openwave_appd_t', 'openwave_appdeploy_exec_t', 'file',
                                      {'getattr', 'open', 'read', 'map', 'execute'})
        self.assert_exact_permissions(helper, 'openwave_appdeploy_exec_t', 'file',
                                      {'entrypoint', 'getattr', 'open', 'read', 'map', 'execute'})
        for domain in ('openwave_app_t', 'openwave_shell_t', 'openwave_android_t', 'openwave_waydroid_apps_t'):
            with self.subTest(domain=domain):
                self.assert_no_permissions(domain, 'openwave_appdeploy_exec_t', 'file')
        for target in ('openwave_user_bundle_t', 'openwave_user_app_exec_t', 'openwave_user_shared_lib_t', 'openwave_package_system_t',
                       'openwave_appdeploy_state_t', 'openwave_appdeploy_package_t', 'openwave_appdeploy_key_t'):
            with self.subTest(target=target):
                mutated = self.compile(f'(allow {helper} {target} (file (execute)))')
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)
        mutated = self.compile(f'(allow {helper} openwave_default_t (process (transition)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)

    def test_appdeploy_helper_reaches_only_package_locations_and_its_own_state(self):
        helper = 'openwave_appdeploy_t'
        self.assert_exact_permissions(helper, 'openwave_appdeploy_key_t', 'file', {'getattr', 'open', 'read'})
        self.assert_exact_permissions(helper, 'openwave_appdeploy_key_t', 'dir', {'getattr', 'open', 'read', 'search'})
        self.assert_exact_permissions(helper, 'openwave_appdeploy_package_t', 'file', {'getattr', 'open', 'read'})
        for target in ('openwave_runtime_t', 'openwave_shared_lib_t', 'openwave_elf_loader_t', 'openwave_security_t',
                       'openwave_label_config_t', 'openwave_app_exec_t', 'openwave_private_t', 'openwave_default_t',
                       'openwave_device_t', 'openwave_proc_t', 'openwave_waydroid_state_t', 'openwave_android_share_t',
                       'openwave_permission_state_t', 'openwave_appd_state_t'):
            with self.subTest(target=target):
                mutated = self.compile(f'(neverallow {helper} {target} (file (write append create unlink rename setattr relabelto)))')
                self.assertEqual(mutated.returncode, 0, f'{helper} may write {target}\n{mutated.stderr}')
                mutated = self.compile(f'(neverallow {helper} {target} (dir (write add_name remove_name create rmdir rename)))')
                self.assertEqual(mutated.returncode, 0, f'{helper} may change directories of {target}\n{mutated.stderr}')
        for target in ('openwave_user_bundle_t', 'openwave_user_app_exec_t', 'openwave_package_system_t'):
            with self.subTest(target=target):
                self.assert_exact_permissions(helper, target, 'file', {
                    'append', 'create', 'getattr', 'ioctl', 'link', 'lock', 'map', 'open', 'read', 'rename',
                    'setattr', 'unlink', 'write', 'relabelfrom', 'relabelto'})
        self.assert_exact_permissions(helper, helper, 'capability',
                                      {'chown', 'fowner', 'fsetid', 'dac_override', 'dac_read_search'})
        self.assert_exact_permissions(helper, 'openwave_user_data_t', 'dir', {'getattr', 'open', 'read', 'search'})
        self.assert_no_permissions(helper, 'openwave_user_data_t', 'file', 'lnk_file')
        for domain in ('openwave_app_t', 'openwave_appd_t', 'openwave_shell_t', 'openwave_android_t', 'openwave_appdeploy_t'):
            with self.subTest(domain=domain):
                mutated = self.compile(f'(neverallow {domain} openwave_appdeploy_key_t (file (write append create unlink rename setattr)))')
                self.assertEqual(mutated.returncode, 0, f'{domain} may modify signing keys\n{mutated.stderr}')
        mutated = self.compile('(allow openwave_app_t openwave_package_system_t (file (write)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)

    def test_appdeploy_package_transport_and_lock_directory_are_narrow(self):
        helper = 'openwave_appdeploy_t'
        self.assert_exact_permissions(helper, 'openwave_default_t', 'dir', {'search', 'getattr', 'open', 'read'})
        self.assert_no_permissions(helper, 'openwave_default_t', 'file')
        self.assert_exact_permissions(helper, 'openwave_run_t', 'dir', {'add_name', 'write', 'search', 'getattr'})
        self.assert_no_permissions(helper, 'openwave_run_t', 'file', 'lnk_file', 'sock_file')
        for domain in ('openwave_dbus_t', 'openwave_appd_t', helper):
            with self.subTest(domain=domain):
                self.assert_exact_permissions(domain, 'openwave_system_t', 'memfd_file', {'read', 'getattr'})
        mutated = self.compile(f'(allow {helper} openwave_system_t (memfd_file (write)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        mutated = self.compile(f'(allow {helper} openwave_system_t (memfd_file (execute)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        for domain in ('openwave_app_t', 'openwave_waydroid_manager_t'):
            with self.subTest(domain=domain):
                self.assert_exact_permissions(domain, 'openwave_package_system_t', 'dir', {'search', 'getattr'})
                self.assert_no_permissions(domain, 'openwave_package_system_t', 'file')

    def test_sysrq_trigger_is_mount_target_only(self):
        for domain in ('openwave_lxc_t', 'openwave_android_t'):
            with self.subTest(domain=domain):
                self.assert_exact_permissions(domain, 'openwave_sysrq_t', 'file', {'getattr', 'mounton'})
        for domain in ('openwave_system_t', 'openwave_app_t', 'openwave_appd_t', 'openwave_waydroid_manager_t'):
            with self.subTest(domain=domain):
                self.assert_no_permissions(domain, 'openwave_sysrq_t', 'file')
        mutated = self.compile('(allow openwave_system_t openwave_sysrq_t (file (write)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        self.assertIn('neverallow', mutated.stderr)

    def test_root_cgroup_control_file_writers(self):
        self.assert_exact_permissions('openwave_lxc_attach_t', 'openwave_app_cgroup_control_t', 'file', {'write'})
        self.assert_exact_permissions('openwave_lxc_t', 'openwave_app_cgroup_control_t', 'file', {'write'})
        for domain in ('openwave_app_t', 'openwave_android_t', 'openwave_waydroid_apps_t'):
            with self.subTest(domain=domain):
                self.assert_no_permissions(domain, 'openwave_app_cgroup_control_t', 'file')

    def test_overlay_files_are_mappable_but_never_executable(self):
        result = self.compile('''
            (neverallow openwave_waydroid_mount_t openwave_waydroid_state_t
                        (file (not (read open getattr map create write rename unlink))))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        missing = self.compile('(neverallow openwave_waydroid_mount_t openwave_waydroid_state_t (file (map)))')
        self.assertNotEqual(missing.returncode, 0)
        mutated = self.compile('(allow openwave_waydroid_mount_t openwave_waydroid_state_t (file (execute)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        self.assertIn('neverallow', mutated.stderr)

    def test_dma_heap_is_never_opened_on_the_host(self):
        self.assert_exact_permissions('openwave_system_t', 'openwave_dma_heap_t', 'chr_file',
                                      {'getattr', 'relabelto'})
        for domain in ('openwave_android_t', 'openwave_udev_t'):
            with self.subTest(domain=domain):
                self.assert_exact_permissions(domain, 'openwave_dma_heap_t', 'chr_file', {'getattr'})
        for domain in ('openwave_waydroid_manager_t', 'openwave_lxc_t', 'openwave_app_t'):
            with self.subTest(domain=domain):
                self.assert_no_permissions(domain, 'openwave_dma_heap_t', 'chr_file')
        mutated = self.compile('(allow openwave_android_t openwave_dma_heap_t (chr_file (open)))')
        self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
        self.assertIn('neverallow', mutated.stderr)

    def test_android_apps_helper_binder_is_android_only(self):
        self.assert_exact_permissions('openwave_waydroid_apps_t', 'openwave_android_t', 'binder',
                                      {'call', 'transfer'})
        self.assert_exact_permissions('openwave_android_t', 'openwave_waydroid_apps_t', 'binder',
                                      {'call', 'transfer'})
        self.assert_no_permissions('openwave_appd_t', 'openwave_android_t', 'binder')
        self.assert_no_permissions('openwave_waydroid_apps_t', 'openwave_waydroid_manager_t', 'binder')
        self.assert_no_permissions('openwave_waydroid_apps_t', 'openwave_android_t', 'process')
        for rule in ('(allow openwave_appd_t openwave_android_t (binder (call)))',
                     '(allow openwave_waydroid_apps_t openwave_waydroid_user_t (file (write)))'):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

    def test_mount_helper_loop_ioctls_are_the_three_it_issues(self):
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_loop_control_t',
                                      'chr_file', ('open', 'read', 'write', 'ioctl'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_loop_block_t',
                                      'blk_file', ('open', 'read', 'write', 'ioctl'))
        result = self.compile('''
            (neverallowx openwave_waydroid_mount_t openwave_loop_control_t
                         (ioctl chr_file (not (0x4c82))))
            (neverallowx openwave_waydroid_mount_t openwave_loop_block_t
                         (ioctl blk_file (not (0x4c0a 0x4c01))))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(allowx openwave_waydroid_mount_t openwave_loop_control_t (ioctl chr_file (0x4c81)))',
            '(allowx openwave_waydroid_mount_t openwave_loop_block_t (ioctl blk_file (0x4c04)))',
        ):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

    def test_android_writes_only_its_isolated_property_copy(self):
        self.assert_exact_permissions('openwave_android_t', 'openwave_android_prop_t', 'file',
                                      ('getattr', 'open', 'read', 'write'))
        self.assert_exact_permissions('openwave_android_t', 'openwave_waydroid_shared_t', 'file',
                                      ('getattr', 'open', 'read'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_android_prop_t',
                                      'file', ('create', 'getattr', 'open', 'read', 'write'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_android_prop_t',
                                      'dir', ('search', 'write', 'add_name'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_android_prop_t',
                                      'filesystem', ('getattr', 'mount', 'relabelfrom',
                                                     'relabelto', 'unmount'))
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_runtime_t',
                                      'filesystem', ('relabelfrom',))
        only_helper_labels = self.compile('''
            (typeattribute qa_not_helper)
            (typeattributeset qa_not_helper (and (all) (not (openwave_waydroid_mount_t))))
            (neverallow qa_not_helper openwave_android_prop_t
                        (filesystem (relabelto mount remount unmount)))
        ''')
        self.assertEqual(only_helper_labels.returncode, 0, only_helper_labels.stderr)
        self.assert_exact_permissions('openwave_waydroid_mount_t', 'openwave_waydroid_shared_t',
                                      'file', ('getattr', 'open', 'read'))
        self.assert_exact_permissions('openwave_waydroid_manager_t', 'openwave_android_prop_t',
                                      'file', ('getattr',))
        result = self.compile('''
            (typeattribute qa_prop_writers)
            (typeattributeset qa_prop_writers (and (all)
                (not (openwave_waydroid_mount_t openwave_android_t))))
            (neverallow qa_prop_writers openwave_android_prop_t
                        (file (write append create setattr unlink rename relabelto)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)
        for rule in (
            '(allow openwave_android_t openwave_waydroid_shared_t (file (write)))',
            '(allow openwave_android_t openwave_android_prop_t (file (execute)))',
            '(allow openwave_waydroid_net_t openwave_android_prop_t (file (write)))',
            '(allow openwave_android_t openwave_android_prop_t (filesystem (relabelto)))',
            '(allow openwave_waydroid_mount_t openwave_runtime_t (filesystem (mount)))',
        ):
            with self.subTest(rule=rule):
                mutated = self.compile(rule)
                self.assertNotEqual(mutated.returncode, 0, mutated.stderr)
                self.assertIn('neverallow', mutated.stderr)

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
            '(allow openwave_lxc_t openwave_shared_lib_t (file (execute_no_trans)))',
            '(allow openwave_app_t openwave_elf_loader_t (file (entrypoint)))',
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
