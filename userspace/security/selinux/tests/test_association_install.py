import unittest
import test_policy

class AssociationInstallPolicyTests(unittest.TestCase):
    compile = test_policy.PolicyTests.compile

    def test_installer_is_separate_and_cannot_modify_host_or_user_documents(self):
        result = self.compile('''
            (neverallow openwave_android_install_t openwave_user_data_t (file (write append create unlink rename)))
            (neverallow openwave_android_install_t openwave_runtime_t (file (write append create unlink rename)))
            (neverallow openwave_files_t openwave_android_install_t (process (transition dyntransition)))
            (neverallow openwave_android_install_t openwave_security_t (security (load_policy setenforce)))
            (neverallow openwave_android_t openwave_android_install_data_t (file (write append unlink rename)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_required_descriptor_and_binder_permissions_exist(self):
        for rule in [
            '(neverallow openwave_android_install_t openwave_files_t (fd (use)))',
            '(neverallow openwave_android_install_t openwave_android_install_data_t (file (create write)))',
            '(neverallow openwave_android_install_t openwave_android_t (binder (call)))',
            '(neverallow openwave_android_install_t openwave_appd_t (fifo_file (read)))']:
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

if __name__ == '__main__': unittest.main()
