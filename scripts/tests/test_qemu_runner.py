"""Verify display selection and VNC port forwarding without launching a VM."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class QemuRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        self.log = self.root / 'qemu-args.json'
        qemu = self.tools / 'qemu-system-x86_64'
        qemu.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
args = sys.argv[1:]
if args == ['-device', 'help']:
    print('name "virtio-vga-gl"')
elif args == ['-display', 'help']:
    print('gtk\\nsdl\\negl-headless')
elif args == ['-accel', 'help']:
    print('tcg')
else:
    with open(os.environ['QEMU_ARGS_LOG'], 'w') as log:
        json.dump(args, log)
''')
        qemu.chmod(0o755)
        qemu_img = self.tools / 'qemu-img'
        qemu_img.write_text('#!/bin/sh\nexit 0\n')
        qemu_img.chmod(0o755)
        self.kernel = self.root / 'bzImage'
        self.image = self.root / 'disk.img'
        self.kernel.touch()
        self.image.touch()
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith('OPENWAVE_QEMU_')}
        self.env.update(PATH=str(self.tools) + os.pathsep + self.env['PATH'],
                        QEMU_BIN=str(qemu), QEMU_ARGS_LOG=str(self.log),
                        OPENWAVE_QEMU_VNC_HOST='127.0.0.1')

    def command(self, *args):
        return subprocess.run([str(ROOT / 'run-openwave'), '--root', str(self.root),
                               '--kernel', str(self.kernel), '--image', str(self.image),
                               '--usb-wifi', 'none', '--usb-bluetooth', 'none',
                               '--audio-backend', 'none', *args, 'run'],
                              env=self.env, capture_output=True, text=True)

    def test_window_and_alias_disable_vnc(self):
        for flag in ('--window', '--qemu-window'):
            with self.subTest(flag=flag):
                result = self.command(flag)
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads(self.log.read_text())
                self.assertNotIn('-vnc', args)
                self.assertEqual(args[args.index('-display') + 1], 'gtk,gl=on,zoom-to-fit=off')

    def test_vnc_ports_translate_to_display_numbers(self):
        for port in (5900, 5901, 65535):
            with self.subTest(port=port):
                result = self.command('--vnc', '--vnc-port', str(port))
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads(self.log.read_text())
                self.assertEqual(args[args.index('-vnc') + 1], f'127.0.0.1:{port - 5900}')
                self.assertEqual(args[args.index('-display') + 1], 'egl-headless')

    def test_default_and_legacy_modes_remain_supported(self):
        result = self.command()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('-vnc', json.loads(self.log.read_text()))
        result = self.command('--vnc-host', 'none')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('-vnc', json.loads(self.log.read_text()))

    def test_guest_can_restart_without_exiting_qemu(self):
        result = self.command('--vnc')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('-no-reboot', json.loads(self.log.read_text()))

    def test_conflicting_modes_fail_before_launch(self):
        for flags in (('--window', '--vnc'), ('--vnc', '--qemu-window')):
            with self.subTest(flags=flags):
                result = self.command(*flags)
                self.assertEqual(result.returncode, 2)
                self.assertIn('Choose either', result.stderr)
                self.assertFalse(self.log.exists())

    def test_invalid_or_missing_port_fails_before_launch(self):
        for value in ('5899', '65536', '0', 'abc', ''):
            with self.subTest(value=value):
                result = self.command('--vnc', '--vnc-port', value)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(self.log.exists())
        result = self.command('--vnc-port')
        self.assertEqual(result.returncode, 2)
        self.assertIn('Missing value', result.stderr)

    def test_incompatible_backend_fails_before_launch(self):
        for flags in (('--window', '--display', 'egl-headless'),
                      ('--vnc', '--display', 'gtk,gl=on')):
            with self.subTest(flags=flags):
                result = self.command(*flags)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
