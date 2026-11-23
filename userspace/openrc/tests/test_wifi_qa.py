#!/usr/bin/env python3
"""Run the guest scan checker with real guest utilities and isolated IPC fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class WifiQaTests(unittest.TestCase):
    def test_counts_access_points_without_wc_and_rejects_incomplete_runs(self):
        for mode in ('pass', 'no-aps', 'native-unavailable', 'permissive'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='openwave-wifi-qa-') as temporary:
                directory = Path(temporary)
                commands = directory / 'bin'; commands.mkdir()
                busybox = ROOT / 'rootfs/bin/busybox'
                for command in ('cat', 'grep', 'sed', 'awk', 'readlink', 'tail'):
                    (commands / command).symlink_to(busybox)
                # Deliberately no wc: it is compiled but not installed in the guest.
                (commands / 'sleep').write_text('#!/bin/sh\nexit 0\n')
                (commands / 'sleep').chmod(0o755)
                fixture = directory / 'gdbus'
                fixture.write_text('''#!/bin/sh
case "$*" in
 *GetDevices*) printf "([objectpath '/org/freedesktop/NetworkManager/Devices/2'],)\\n" ;;
 *DeviceType*) printf '(<uint32 2>,)\\n' ;;
 *GetAccessPoints*)
   if [ "$WIFI_TEST_MODE" = no-aps ]; then printf '(@ao [],)\\n'; else
     printf "([objectpath '/org/freedesktop/NetworkManager/AccessPoint/1', '/org/freedesktop/NetworkManager/AccessPoint/2'],)\\n"
   fi ;;
 *GetState*)
   if [ "$WIFI_TEST_MODE" = native-unavailable ]; then printf "({'wifiDeviceAvailable': <false>},)\\n"; else
     printf "({'wifiDeviceAvailable': <true>, 'wifiEnabled': <true>},)\\n"
   fi ;;
 *) printf '()\\n' ;;
esac
''')
                fixture.chmod(0o755)
                (directory / 'cmdline').write_text('openwave.qa=wifi\n')
                (directory / 'enforce').write_text('0\n' if mode == 'permissive' else '1\n')
                network = directory / 'net/wlan0'; (network / 'phy80211').mkdir(parents=True)
                (network / 'device').mkdir(); (network / 'device/driver').symlink_to('/drivers/rt2800usb')
                source = (ROOT / 'userspace/openrc/qa/wifi.start').read_text()
                source = source.replace('/proc/cmdline', str(directory / 'cmdline'))
                source = source.replace('/sys/fs/selinux/enforce', str(directory / 'enforce'))
                source = source.replace('/sys/class/net', str(directory / 'net'))
                source = source.replace('/run/openwave', str(directory / 'logs'))
                source = source.replace('/usr/bin/gdbus', str(fixture)).replace('exec >/dev/console 2>&1', ':')
                script = directory / 'wifi.start'; script.write_text(source)
                result = subprocess.run([str(busybox), 'ash', str(script)],
                    env=os.environ | {'PATH': str(commands), 'WIFI_TEST_MODE': mode},
                    capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn('not found', result.stderr)
                if mode == 'pass':
                    self.assertIn('scan access_points=2', result.stdout)
                    self.assertIn('WIFI_QA PASS', result.stdout)
                else:
                    self.assertIn('WIFI_QA FAIL', result.stdout)
                    self.assertNotIn('WIFI_QA PASS', result.stdout)

if __name__ == '__main__': unittest.main()
