#!/usr/bin/env python3
"""Run the production OpenRC start_post against a name-before-ready fixture."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class AppdReadiness(unittest.TestCase):
    def test_rpc_barrier_and_matching_process_marker(self):
        for mode in ('ready', 'wrong-pid', 'rpc-failure'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='openwave-appd-ready-') as temporary:
                directory = Path(temporary)
                marker, pidfile = directory / 'ready', directory / 'pid'
                pidfile.write_text(str(os.getpid()) + '\n')
                success = directory / 'success'; success.write_text('#!/bin/sh\nexit 0\n'); success.chmod(0o755)
                rpc = directory / 'rpc'
                rpc.write_text('#!/bin/sh\n[ "$QA_MODE" != rpc-failure ] || exit 1\nprintf "%s\\n" "$QA_PID" > "$QA_READY"\n')
                rpc.chmod(0o755)
                script = (ROOT / 'userspace/openrc/etc/init.d/appd').read_text()
                script = script.replace('/System/Services/openwave-register-service-identity', str(success))
                script = script.replace('/System/Services/openwave-wait-dbus', str(success))
                script = script.replace('/usr/bin/gdbus', str(rpc))
                script = script.replace('/run/openwave/appd/ready', str(marker)).replace('/run/openwave/appd.pid', str(pidfile))
                service = directory / 'appd'; service.write_text(script)
                env = os.environ | {'QA_MODE': mode, 'QA_READY': str(marker),
                    'QA_PID': str(os.getpid() + (1 if mode == 'wrong-pid' else 0))}
                result = subprocess.run(['/bin/sh', '-c', 'eerror() { echo "$*" >&2; }; . "$1"; start_post', '_', str(service)],
                    env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, mode == 'ready', result.stderr)
                if mode == 'wrong-pid': self.assertIn('does not match', result.stderr)

if __name__ == '__main__': unittest.main()
