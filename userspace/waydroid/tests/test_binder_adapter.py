"""The privileged adapter must reject host/unprivileged invocation first."""
from pathlib import Path
import os
import subprocess
import sys
import unittest


class BinderAdapterTests(unittest.TestCase):
    def test_adapter_initializes_the_real_upstream_command_helpers(self):
        # Exercise the pinned parser/logger/process runner, replacing only the
        # privileged Binder operation and kernel identity/label observations.
        # The real host process remains an ordinary user and runs only true.
        root = Path(__file__).resolve().parents[3]
        harness = r'''
import builtins, importlib.util, io, os, stat, sys
from unittest.mock import patch
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1] + '/third_party/waydroid')
import tools
spec = importlib.util.spec_from_file_location('binder_adapter', sys.argv[1] + '/userspace/waydroid/prepare_binder.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
real_open, real_stat = builtins.open, os.stat
def observed_open(path, *args, **kwargs):
    if path == '/proc/self/attr/current':
        return io.BytesIO(b'system_u:system_r:openwave_system_t:s0\0')
    return real_open(path, *args, **kwargs)
def observed_stat(path, *args, **kwargs):
    if str(path).startswith('/dev/openwave-test-'):
        return os.stat_result((stat.S_IFCHR | 0o600,) + (0,) * 9)
    return real_stat(path, *args, **kwargs)
def prepare(args):
    tools.helpers.run.user(args, ['/usr/bin/true'])
    for name in ('BINDER_DRIVER', 'VNDBINDER_DRIVER', 'HWBINDER_DRIVER'):
        setattr(args, name, 'openwave-test-' + name.lower())
sys.argv = ['adapter']
with patch('os.getuid', return_value=0), patch('os.geteuid', return_value=0), \
     patch('os.getgid', return_value=0), patch('os.getegid', return_value=0), \
     patch('builtins.open', side_effect=observed_open), \
     patch('os.stat', side_effect=observed_stat), \
     patch('os.getxattr', return_value=b'system_u:object_r:openwave_binderfs_t:s0\0'), \
     patch.object(tools.helpers.drivers, 'setupBinderNodes', side_effect=prepare):
    assert adapter.main() == 0
'''
        result = subprocess.run([sys.executable, '-c', harness, str(root)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Upstream Binder nodes prepared', result.stdout)

    def test_unprivileged_invocation_never_imports_upstream_or_touches_devices(self):
        self.assertNotEqual(os.getuid(), 0)
        adapter = Path(__file__).resolve().parents[1] / 'prepare_binder.py'
        for args in ([], ['--force'], ['container', 'start']):
            result = subprocess.run([sys.executable, '-I', str(adapter), *args],
                                    capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 126, result.stderr)
            self.assertIn('root OpenRC identity and no arguments', result.stderr)
            self.assertNotIn('Traceback', result.stderr)
