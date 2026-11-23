import unittest
import test_policy


class AndroidWebViewPolicyTests(unittest.TestCase):
    compile = test_policy.PolicyTests.compile

    def test_android_gets_executable_memory_for_the_web_engine_and_nothing_wider(self):
        result = self.compile('''
            (neverallow openwave_android_t self (process (execstack execheap)))
            (neverallow openwave_android_t openwave_waydroid_user_t (file (execute_no_trans entrypoint)))
            (neverallow openwave_android_t openwave_default_t (file (execute execute_no_trans entrypoint)))
        ''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_executable_memory_is_granted_to_android_alone(self):
        result = self.compile('(neverallow openwave_android_t self (process (execmem)))')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('neverallow', result.stderr)
        for domain in ('openwave_lxc_t', 'openwave_waydroid_manager_t', 'openwave_android_install_t'):
            with self.subTest(domain=domain):
                result = self.compile(f'(neverallow {domain} self (process (execmem)))')
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
