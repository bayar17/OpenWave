import unittest
import test_policy


class DesktopDefaultsPolicyTests(unittest.TestCase):
    compile = test_policy.PolicyTests.compile

    def test_confined_applications_cannot_touch_the_default_application_configuration(self):
        for rule in (
                '(allow openwave_app_t openwave_user_config_t (file (write)))',
                '(allow openwave_app_t openwave_user_config_t (file (create)))',
                '(allow openwave_app_t openwave_user_config_t (dir (add_name)))',
                '(allow openwave_android_t openwave_user_config_t (file (read)))'):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)

    def test_appd_alone_manages_the_configuration_directory(self):
        result = self.compile('''
            (neverallow openwave_appd_t openwave_user_config_t (file (execute)))
            (neverallow openwave_appdeploy_t openwave_user_config_t (file (read write append create unlink rename setattr)))
            (neverallow openwave_settings_t openwave_user_config_t (file (write append create unlink rename)))
            (neverallow openwave_files_t openwave_user_config_t (file (write append create unlink rename)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_appd_may_write_android_desktop_entries_and_no_confined_domain_may(self):
        allowed = self.compile('''
            (neverallow openwave_android_t openwave_appd_state_t (file (write append create unlink rename setattr)))
            (neverallow openwave_android_t openwave_appd_state_t (dir (write add_name remove_name create)))
            (neverallow openwave_app_t openwave_appd_state_t (dir (write add_name remove_name create)))
        ''')
        self.assertEqual(allowed.returncode, 0, allowed.stderr)
        for rule in ('(neverallow openwave_appd_t openwave_appd_state_t (file (create write unlink rename)))',
                     '(neverallow openwave_appd_t openwave_appd_state_t (dir (create add_name remove_name write)))'):
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)


if __name__ == '__main__':
    unittest.main()
