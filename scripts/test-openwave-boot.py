#!/usr/bin/env python3
"""Bounded raw-QEMU GTK/VirGL boot with immutable base image and serial evidence.

This collects evidence, not an automatic claim that every service is healthy.
No VNC or persistent guest writes are used. Host USB Wi-Fi passthrough is
available only through the explicit --usb-wifi hardware test option.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def qmp_command(path, command, arguments=None):
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(5)
        client.connect(str(path))
        stream = client.makefile('rb')
        json.loads(stream.readline())
        for request in ('qmp_capabilities', command):
            payload = {'execute': request}
            if request == command and arguments is not None:
                payload['arguments'] = arguments
            client.sendall(json.dumps(payload).encode() + b'\n')
            while True:
                reply = json.loads(stream.readline())
                if 'error' in reply:
                    raise RuntimeError(reply)
                if 'return' in reply:
                    break


def drive_keyboard(process, qmp, output, deadline):
    """Inject hardware events only after each guest field is ready."""
    injected = []
    sequence = (('a',), ('b',), ('shift', 'c'), ('backspace',), ('shift', 'd'))
    try:
        for target in ('shell', 'client'):
            marker = 'KEYBOARD_QA_READY ' + target
            while True:
                serial_path = output / 'serial.log'
                serial = serial_path.read_text(errors='replace') if serial_path.exists() else ''
                if 'KEYBOARD_QA_FAIL' in serial:
                    raise RuntimeError('Guest keyboard check failed')
                if marker in serial:
                    break
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Timed out waiting for ' + marker)
                time.sleep(0.1)
            for chord in sequence:
                qmp_command(qmp, 'send-key', {'keys': [
                    {'type': 'qcode', 'data': code} for code in chord], 'hold-time': 50})
                injected.append({'target': target, 'qcodes': chord})
                time.sleep(0.15)
    finally:
        (output / 'keyboard-input.json').write_text(json.dumps(injected, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, default=ROOT / 'out/linux/arch/x86/boot/bzImage')
    parser.add_argument('--output', type=Path, required=True, help='New evidence directory')
    parser.add_argument('--mode', choices=('enforcing', 'permissive'), default='enforcing')
    parser.add_argument('--seconds', type=int, default=65)
    parser.add_argument('--drawer', action='store_true', help='Open Ocean using its existing --drawer option')
    parser.add_argument('--app-launch', action='store_true',
                        help='Request an ordinary confined Gallery launch through Ocean')
    parser.add_argument('--portal', action='store_true', help='Exercise Settings Wallpaper through the real portal services')
    parser.add_argument('--keyboard', action='store_true', help='Inject QEMU hardware keys into Ocean and the confined Settings search field')
    parser.add_argument('--browser-actions', action='store_true', help='Exercise Browser downloads and web-app installation using a local HTTP fixture')
    parser.add_argument('--waydroid', action='store_true', help='Collect bounded Waydroid/framework diagnostics under enforcing SELinux')
    parser.add_argument('--mount-preflight', action='store_true',
                        help='Read-only Waydroid mount-helper prerequisites; no Android startup or mounts')
    parser.add_argument('--selinux-debug', action='store_true',
                        help='Allow permissive SELinux ONLY with --mount-preflight; capture AVC evidence in disposable QEMU')
    parser.add_argument('--dns', action='store_true', help='With --app-launch, verify confined app access to live DNS and profile isolation')
    parser.add_argument('--power', choices=('poweroff', 'reboot'), help='Exercise confirmed Ocean power actions; reboot must boot again before powering off')
    parser.add_argument('--usb-wifi', help='Exercise a real host USB Wi-Fi adapter (VID:PID); temporarily detaches it from the host')
    parser.add_argument('--wifi-associate', action='store_true', help='With USB Wi-Fi, retry an existing saved guest profile and require DHCP/DNS')
    args = parser.parse_args()
    if args.dns and not args.app_launch:
        parser.error('--dns requires --app-launch')
    if args.wifi_associate and not args.usb_wifi:
        parser.error('--wifi-associate requires --usb-wifi')
    if sum(bool(value) for value in (args.portal, args.app_launch, args.power, args.usb_wifi, args.keyboard, args.browser_actions, args.waydroid, args.mount_preflight)) > 1:
        parser.error('Run the portal, application, power, Wi-Fi and keyboard suites separately')
    if not 10 <= args.seconds <= 600:
        parser.error('--seconds must be 10..600')
    debug_allowed = args.selinux_debug and args.mount_preflight and args.mode == 'permissive'
    if args.selinux_debug and not debug_allowed:
        parser.error('--selinux-debug requires --mount-preflight --mode permissive')
    if args.mount_preflight and args.mode == 'permissive' and not args.selinux_debug:
        parser.error('Permissive mount preflight requires explicit --selinux-debug')
    if (args.app_launch or args.portal or args.power or args.usb_wifi or
        args.keyboard or args.browser_actions or args.waydroid or
        args.mount_preflight) and args.mode != 'enforcing' and not debug_allowed:
        parser.error('Guest integration suites require enforcing mode')
    usb = None
    if args.usb_wifi:
        if not re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{4}', args.usb_wifi):
            parser.error('--usb-wifi requires VID:PID')
        vendor, product = args.usb_wifi.lower().split(':')
        for candidate in Path('/sys/bus/usb/devices').glob('*'):
            if (candidate / 'idVendor').is_file() and (candidate / 'idProduct').is_file() \
                and (candidate / 'idVendor').read_text().strip() == vendor \
                and (candidate / 'idProduct').read_text().strip() == product:
                node = Path('/dev/bus/usb') / f'{int((candidate / "busnum").read_text()):03d}' / f'{int((candidate / "devnum").read_text()):03d}'
                if not os.access(node, os.R_OK | os.W_OK):
                    parser.error(f'USB passthrough requires read/write access to {node}')
                usb = f'usb-host,bus=openwave-usb.0,vendorid=0x{vendor},productid=0x{product}'
                break
        if not usb:
            parser.error('The requested USB adapter is not attached')
    image, kernel = args.image.resolve(strict=True), args.kernel.resolve(strict=True)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with (output / 'fsck.log').open('w') as log:
        subprocess.run(['e2fsck', '-fn', str(image)], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    hashes = {'image': digest(image), 'kernel': digest(kernel)}
    fixture = None
    if args.browser_actions:
        from openwave_browser_fixture import BrowserFixture
        fixture = BrowserFixture()
    acceleration = ['-accel', 'kvm', '-cpu', 'host'] if os.access('/dev/kvm', os.R_OK | os.W_OK) \
        else ['-accel', 'tcg,thread=multi', '-cpu', 'max']
    # A short /tmp path avoids AF_UNIX's pathname limit for nested evidence dirs.
    qmp = Path('/tmp') / f'openwave-boot-{os.getpid()}.qmp'
    if qmp.exists():
        raise RuntimeError(f'QMP path already exists: {qmp}')
    append = ('root=/dev/vda rw rootfstype=ext4 init=/init console=tty0 '
              'console=ttyS0,115200 loglevel=7 ignore_loglevel audit_backlog_limit=32768 '
              'openwave.audit=verbose openwave.qa=boot OPENWAVE_QEMU_POINTER=1 '
              'video=Virtual-1:544x960@60e OPENWAVE_DISPLAY_MODE=544x960')
    if args.mode == 'permissive':
        append += ' openwave.selinux=permissive'
    if args.selinux_debug:
        # Diagnostic-only kernel logging. Never relax security via image edits.
        append += ' audit=1 log_buf_len=16M printk.time=1'
    if args.app_launch:
        append += ' openwave.qa=app-launch'
    if args.dns:
        append += ' openwave.qa=dns'
    if args.drawer:
        append += ' openwave.qa=drawer'
    if args.portal:
        append += ' openwave.qa=portal'
    if args.keyboard:
        append += ' openwave.qa=keyboard'
    if args.waydroid:
        append += ' openwave.qa=waydroid'
    if args.mount_preflight:
        append += ' openwave.qa=mount-preflight'
    if fixture:
        append += ' openwave.qa=browser-actions openwave.browser-fixture=' + fixture.url
    if args.power:
        append += ' openwave.qa=' + args.power
    if usb:
        append += ' openwave.qa=wifi-associate' if args.wifi_associate else ' openwave.qa=wifi'
    command = ['qemu-system-x86_64', '-name', 'OpenWave boot validation', '-machine', 'q35',
        *acceleration, '-smp', '4', '-m', '4096' if args.waydroid else '2048', '-kernel', str(kernel), '-append', append,
        '-drive', f'if=none,id=openwave-root,file={image},format=raw',
        '-device', 'virtio-blk-pci,drive=openwave-root', '-snapshot', '-nic',
        'user,model=virtio-net-pci' if fixture or args.waydroid else 'none',
        '-audiodev', 'none,id=openwave-audio', '-device', 'virtio-sound-pci,audiodev=openwave-audio',
        '-device', 'virtio-vga-gl,xres=544,yres=960', '-display', 'gtk,gl=on,zoom-to-fit=off',
        '-device', 'virtio-keyboard-pci', '-device', 'qemu-xhci,id=openwave-usb',
        '-device', 'usb-tablet,bus=openwave-usb.0', '-serial', f'file:{output}/serial.log',
        '-monitor', 'none', '-qmp', f'unix:{qmp},server=on,wait=off']
    if usb:
        command += ['-device', usb]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    shutdown = 'exited before deadline'
    with (output / 'host.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.seconds
            if args.keyboard:
                try:
                    drive_keyboard(process, qmp, output, deadline)
                except (OSError, ValueError, RuntimeError) as error:
                    (output / 'keyboard-driver-error.txt').write_text(str(error) + '\n')
            try:
                if args.browser_actions or args.waydroid or args.mount_preflight:
                    while process.poll() is None and time.monotonic() < deadline:
                        serial_path = output / 'serial.log'
                        progress = serial_path.read_text(errors='replace') if serial_path.exists() else ''
                        if ((args.waydroid and 'WAYDROID_QA END enforcing=1' in progress)
                            or (args.mount_preflight and
                                'MOUNT_QA_END prerequisites=' in progress)
                            or 'BROWSER_QA_FAIL end-to-end actions' in progress
                            or 'BROWSER_QA PASS download bytes, owner, prompts and web app enforcing=1' in progress):
                            deadline = time.monotonic()
                            break
                        time.sleep(0.25)
                process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                try:
                    qmp_command(qmp, 'screendump', {'filename': str(output / 'screen.ppm')})
                except (OSError, ValueError, RuntimeError) as error:
                    (output / 'screenshot-error.txt').write_text(str(error) + '\n')
                qmp_command(qmp, 'system_powerdown')
                try:
                    process.wait(timeout=55)
                    shutdown = 'QEMU exited after ACPI powerdown request'
                except subprocess.TimeoutExpired:
                    qmp_command(qmp, 'quit')
                    process.wait(timeout=5)
                    shutdown = 'snapshot discarded after shutdown timeout'
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            qmp.unlink(missing_ok=True)
            if fixture:
                fixture.close()
                (output / 'http-fixture.json').write_text(json.dumps({'url': fixture.url, 'reports': fixture.reports}, indent=2) + '\n')
    unchanged = hashes == {'image': digest(image), 'kernel': digest(kernel)}
    serial = (output / 'serial.log').read_text(errors='replace')
    power_passed = ('POWER_QA confirmation action=poweroff' in serial
        and 'POWER_QA cancellation' in serial
        and 'PID1: Received "poweroff" from FIFO' in serial and 'Power down' in serial
        and 'POWER_QA unauthorized-root-rejected' in serial
        and 'POWER_QA FAIL' not in serial and not 'Kernel panic' in serial) if args.power else None
    if args.power == 'reboot':
        boots = set(re.findall(r'POWER_QA BOOT id=([a-f0-9-]+) action=', serial))
        power_passed = power_passed and len(boots) >= 2 and 'Restarting system' in serial \
            and 'PID1: Received "reboot" from FIFO' in serial \
            and serial.count('OPENWAVE_BOOT_REPORT_END') >= 2
    if args.power:
        states = re.findall(r'POWER_QA BOOT id=[a-f0-9-]+ action=\w+ enforcing=(\d)', serial)
        power_passed = power_passed and bool(states) and all(state == '1' for state in states)
    report = {'inputs': hashes, 'inputs_unchanged': unchanged,
              'exit_code': process.returncode, 'shutdown': shutdown, 'mode': args.mode,
              'guest_powerdown_reported': 'Power down' in serial,
              'kernel_panic': 'Kernel panic' in serial,
              'boot_report_complete': 'OPENWAVE_BOOT_REPORT_END' in serial,
              'portal_requested': args.portal,
              'keyboard_requested': args.keyboard,
              'browser_actions_requested': args.browser_actions,
              'browser_actions_passed': ('BROWSER_QA PASS download bytes, owner, prompts and web app enforcing=1' in serial
                  and 'BROWSER_QA_FAIL' not in serial
                  and len(fixture.reports) >= 2
                  and fixture.reports[1].get('stored') == ['null']) if fixture else None,
              'keyboard_passed': ('KEYBOARD_QA PASS hardware keyboard in Ocean and Settings enforcing=1' in serial
                  and 'KEYBOARD_QA_PASS shell hardware text/editing' in serial
                  and 'KEYBOARD_QA_PASS client hardware text/editing' in serial
                  and 'KEYBOARD_QA_FAIL' not in serial
                  and 'failed to create compose table' not in serial
                  and not (output / 'keyboard-driver-error.txt').exists()) if args.keyboard else None,
              'dns_requested': args.dns,
              'dns_passed': ('DNS_QA_PASS' in serial and 'DNS_QA_FAIL' not in serial) if args.dns else None,
              'power_requested': args.power,
              'power_passed': power_passed,
              'usb_wifi_requested': args.usb_wifi,
              'usb_wifi_passed': ('WIFI_QA PASS real USB adapter, NetworkManager and scan' in serial
                  and 'WIFI_SETTINGS_QA_PASS' in serial
                  and 'WIFI_SETTINGS_QA unregistered session process rejected' in serial
                  and 'WIFI_SETTINGS_QA_FAIL' not in serial) if usb else None,
              'portal_passed': ('PORTAL_SMOKE PASS Settings wallpaper end-to-end' in serial) if args.portal else None,
              'app_launch_requested': args.app_launch,
              'app_launch_passed': ('APP_SMOKE PASS confined live app launched through Ocean' in serial)
                  if args.app_launch else None}
    if args.wifi_associate:
        report['usb_wifi_passed'] = 'WIFI_ASSOC PASS associated, DHCP and DNS' in serial and 'WIFI_ASSOC FAIL' not in serial
        report['wifi_association_requested'] = True
    if args.mount_preflight:
        from openwave_mount_preflight_report import evaluate as evaluate_mount
        mount_preflight = evaluate_mount(serial)
        report['mount_preflight_requested'] = True
        report['mount_preflight_passed'] = mount_preflight['preflight_passed']
        (output / 'mount-preflight.json').write_text(
            json.dumps(mount_preflight, indent=2) + '\n')
    if args.selinux_debug:
        from openwave_selinux_avc_report import analyze_avcs
        avcs = analyze_avcs(serial)
        debug_complete = (
            mount_preflight['diagnostics_complete']
            and not mount_preflight['enforcing']
            and 'MOUNT_QA_BEGIN enforcing=0' in serial
            and 'MOUNT_QA_END prerequisites=' in serial
            and unchanged and 'Kernel panic' not in serial
        )
        report['selinux_debug_requested'] = True
        report['selinux_debug_capture_complete'] = debug_complete
        report['release_ready'] = False
        report['mount_operations_passed'] = False
        (output / 'selinux-avc-debug.json').write_text(
            json.dumps({'schema': 1, 'mode': 'permissive-diagnostic',
                        'capture_complete': debug_complete,
                        'release_ready': False,
                        'avc_summary': avcs}, indent=2) + '\n')
    if args.waydroid:
        from openwave_waydroid_report import evaluate
        waydroid = evaluate(serial)
        report['waydroid_requested'] = True
        report['waydroid_framework_passed'] = waydroid['framework_passed']
        (output / 'waydroid.json').write_text(json.dumps(waydroid, indent=2) + '\n')
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if not unchanged or process.returncode != 0 or (args.app_launch and not report['app_launch_passed']) or (args.portal and not report['portal_passed']) \
        or (args.dns and not report['dns_passed']) \
        or (args.keyboard and not report['keyboard_passed']) \
        or (args.browser_actions and not report['browser_actions_passed']) \
        or (args.waydroid and not report['waydroid_framework_passed']) \
        or (args.mount_preflight and not args.selinux_debug and not report['mount_preflight_passed']) \
        or (args.selinux_debug and not report['selinux_debug_capture_complete']) \
        or (args.power and not report['power_passed']) or (usb and not report['usb_wifi_passed']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
