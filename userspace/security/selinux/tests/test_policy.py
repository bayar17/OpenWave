"""Compile real policy mutations to verify that security invariants fail closed."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
TRIPLE = os.environ.get('OPENWAVE_TARGET_TRIPLE', 'x86_64-unknown-openwave')
BUILD = ROOT / 'out/sdk-dependencies' / TRIPLE / 'selinux-policy'
POLICY = ROOT / 'userspace/security/selinux/openwave.cil'


class PolicyTests(unittest.TestCase):
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

    def test_privileged_grants_are_rejected(self):
        rules = ['(allow openwave_app_t openwave_private_t (file (read)))',
                 '(allow openwave_app_t openwave_security_t (security (load_policy)))',
                 '(allow openwave_app_t self (capability (sys_admin)))',
                 '(allow openwave_app_t openwave_system_t (process (transition)))']
        for rule in rules:
            with self.subTest(rule=rule):
                result = self.compile(rule)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('neverallow', result.stderr)


if __name__ == '__main__':
    unittest.main()
