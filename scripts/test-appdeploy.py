#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 544, 960
REQUEST = re.compile(r'ADQA UI (shot) (\S+) seq=(\d+)|ADQA UI (tap) (\d+) (\d+) seq=(\d+)|ADQA UI (key) (\S+) seq=(\d+)'
                     r'|ADQA UI (hold) (\d+) (\d+) seq=(\d+)|ADQA UI (swipe) (\d+) (\d+) (\d+) (\d+) seq=(\d+)')


def qmp(path, command, arguments=None):
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(10)
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


def tap(path, x, y):
    ax, ay = min(32767, int(x * 32767 / (WIDTH - 1))), min(32767, int(y * 32767 / (HEIGHT - 1)))
    qmp(path, 'input-send-event', {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': ax}},
                                              {'type': 'abs', 'data': {'axis': 'y', 'value': ay}}]})
    qmp(path, 'input-send-event', {'events': [{'type': 'btn', 'data': {'down': True, 'button': 'left'}}]})
    time.sleep(0.08)
    qmp(path, 'input-send-event', {'events': [{'type': 'btn', 'data': {'down': False, 'button': 'left'}}]})


def pointer(path, x, y):
    ax, ay = min(32767, int(x * 32767 / (WIDTH - 1))), min(32767, int(y * 32767 / (HEIGHT - 1)))
    qmp(path, 'input-send-event', {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': ax}},
                                              {'type': 'abs', 'data': {'axis': 'y', 'value': ay}}]})


def button(path, down):
    qmp(path, 'input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})


def hold(path, x, y, seconds=1.3):
    pointer(path, x, y)
    button(path, True)
    time.sleep(seconds)
    button(path, False)


def swipe(path, x1, y1, x2, y2, steps=14):
    pointer(path, x1, y1)
    button(path, True)
    for step in range(1, steps + 1):
        pointer(path, x1 + (x2 - x1) * step / steps, y1 + (y2 - y1) * step / steps)
        time.sleep(0.04)
    button(path, False)


def screenshot(path, name, output):
    ppm = output / f'{name}.ppm'
    qmp(path, 'screendump', {'filename': str(ppm)})
    try:
        from PIL import Image
        Image.open(ppm).save(output / f'{name}.png')
        ppm.unlink()
    except Exception as error:
        (output / f'{name}.png.error').write_text(str(error) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, default=ROOT / 'out/linux/arch/x86/boot/bzImage')
    parser.add_argument('--output', type=Path, required=True, help='New evidence directory')
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--cancel', default='315,565', help='screen position of the Cancel button')
    parser.add_argument('--only', choices=('menu', 'browser', 'webapp', 'prompt', 'userpkg', 'pinning'), help='run only one section of the guest script')
    parser.add_argument('--audit', action='store_true', help='boot with audit=1 so denials carry their syscall')
    parser.add_argument('--accept', default='425,565', help='screen position of the Install/Update/Remove button')
    args = parser.parse_args()
    image, kernel = args.image.resolve(strict=True), args.kernel.resolve(strict=True)
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sock = Path('/tmp') / f'openwave-appdeploy-{os.getpid()}.qmp'
    acceleration = ['-accel', 'kvm', '-cpu', 'host'] if os.access('/dev/kvm', os.R_OK | os.W_OK) \
        else ['-accel', 'tcg,thread=multi', '-cpu', 'max']
    append = ('root=/dev/vda rw rootfstype=ext4 init=/init console=tty0 console=ttyS0,115200 loglevel=7 '
              'ignore_loglevel audit_backlog_limit=32768 openwave.audit=verbose openwave.qa=appdeploy '
              'OPENWAVE_QEMU_POINTER=1 video=Virtual-1:544x960@60e OPENWAVE_DISPLAY_MODE=544x960 '
              f'adqa.cancel={args.cancel} adqa.accept={args.accept}' + (' audit=1' if args.audit else '') + (f' adqa.only={args.only}' if args.only else ''))
    command = ['qemu-system-x86_64', '-name', 'OpenWave appdeploy validation', '-machine', 'q35', *acceleration,
               '-smp', '4', '-m', '2048', '-kernel', str(kernel), '-append', append,
               '-drive', f'if=none,id=openwave-root,file={image},format=raw',
               '-device', 'virtio-blk-pci,drive=openwave-root', '-snapshot', '-nic', 'none',
               '-audiodev', 'none,id=openwave-audio', '-device', 'virtio-sound-pci,audiodev=openwave-audio',
               '-device', f'virtio-vga-gl,xres={WIDTH},yres={HEIGHT}', '-display', 'gtk,gl=on,zoom-to-fit=off',
               '-device', 'virtio-keyboard-pci', '-device', 'qemu-xhci,id=openwave-usb',
               '-device', 'usb-tablet,bus=openwave-usb.0', '-serial', f'file:{output}/serial.log',
               '-monitor', 'none', '-qmp', f'unix:{sock},server=on,wait=off']
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    serial_path = output / 'serial.log'
    done, log = set(), []
    status = 'timeout'
    with (output / 'host.log').open('w') as host_log:
        process = subprocess.Popen(command, stdout=host_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + args.seconds
        try:
            while process.poll() is None and time.monotonic() < deadline:
                text = serial_path.read_text(errors='replace') if serial_path.exists() else ''
                for match in REQUEST.finditer(text):
                    groups = match.groups()
                    kind = next(g for g in (groups[0], groups[3], groups[7], groups[10], groups[14]) if g)
                    seq = next(g for g in (groups[2], groups[6], groups[9], groups[13], groups[19]) if g)
                    if seq in done:
                        continue
                    done.add(seq)
                    entry = {'seq': int(seq), 'kind': kind}
                    try:
                        if kind == 'shot':
                            entry['name'] = groups[1]
                            screenshot(sock, groups[1], output)
                        elif kind == 'hold':
                            entry['x'], entry['y'] = int(groups[11]), int(groups[12])
                            hold(sock, entry['x'], entry['y'])
                        elif kind == 'swipe':
                            entry['from'] = [int(groups[15]), int(groups[16])]
                            entry['to'] = [int(groups[17]), int(groups[18])]
                            swipe(sock, int(groups[15]), int(groups[16]), int(groups[17]), int(groups[18]))
                        elif kind == 'tap':
                            entry['x'], entry['y'] = int(groups[4]), int(groups[5])
                            tap(sock, entry['x'], entry['y'])
                        else:
                            entry['key'] = groups[8]
                            qmp(sock, 'send-key', {'keys': [{'type': 'qcode', 'data': key} for key in groups[8].split('+')], 'hold-time': 80})
                        entry['done'] = True
                    except (OSError, RuntimeError, ValueError) as error:
                        entry.update(done=False, error=str(error))
                    log.append(entry)
                    (output / 'ui.json').write_text(json.dumps(log, indent=2) + '\n')
                if 'ADQA_DONE' in text:
                    status = 'finished'
                    break
                time.sleep(0.25)
            if status == 'finished':
                try:
                    screenshot(sock, 'final', output)
                    qmp(sock, 'system_powerdown')
                    process.wait(timeout=60)
                except (OSError, RuntimeError, subprocess.TimeoutExpired):
                    pass
        finally:
            if process.poll() is None:
                try:
                    qmp(sock, 'quit')
                    process.wait(timeout=10)
                except (OSError, RuntimeError, subprocess.TimeoutExpired):
                    process.terminate()
                    process.wait(timeout=10)
            sock.unlink(missing_ok=True)
    serial = serial_path.read_text(errors='replace')
    sys.path.insert(0, str(ROOT / 'scripts'))
    from openwave_waydroid_report import extract_screenshots
    shots = []
    (output / 'screenshots').mkdir(exist_ok=True)
    for shot in extract_screenshots(serial):
        if shot['ok']:
            (output / 'screenshots' / f"{shot['name']}.jpg").write_bytes(shot['jpeg'])
        shots.append({k: v for k, v in shot.items() if k != 'jpeg'})
    passes = re.findall(r'^ADQA PASS (\S+)', serial, re.M)
    fails = re.findall(r'^ADQA FAIL (.*)$', serial, re.M)
    avcs = [line for line in serial.splitlines() if 'avc:' in line and 'denied' in line]
    (output / 'avc.txt').write_text('\n'.join(avcs) + ('\n' if avcs else ''))
    report = {'screenshots': shots, 'status': status, 'pass': len(passes), 'fail': len(fails), 'failures': fails, 'avc_denials': len(avcs),
              'selinux': re.findall(r'ADQA_BEGIN selinux=(\S+)', serial)}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if status == 'finished' and not fails else 1


if __name__ == '__main__':
    sys.exit(main())
