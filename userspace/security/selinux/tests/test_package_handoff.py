import unittest
import test_policy


class PackageHandoffPolicyTests(unittest.TestCase):
    compile = test_policy.PolicyTests.compile

    def test_files_can_hand_a_package_descriptor_to_the_installer_and_nothing_more(self):
        result = self.compile('''
            (neverallow openwave_appdeploy_t openwave_user_data_t (file (read getattr write append create unlink rename setattr execute)))
            (neverallow openwave_files_t openwave_appdeploy_t (process (transition dyntransition)))
            (neverallow openwave_files_t openwave_appdeploy_state_t (file (write append create unlink rename)))
            (neverallow openwave_files_t openwave_appdeploy_package_t (file (write append create unlink rename)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_each_hop_of_the_descriptor_is_permitted(self):
        for rule in (
                '(neverallow openwave_dbus_t openwave_files_t (fd (use)))',
                '(neverallow openwave_appd_t openwave_files_t (fd (use)))',
                '(neverallow openwave_appd_t openwave_appd_t (memfd_file (create)))',
                '(neverallow openwave_appdeploy_t openwave_appd_t (memfd_file (read)))'):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)


if __name__ == '__main__':
    unittest.main()
