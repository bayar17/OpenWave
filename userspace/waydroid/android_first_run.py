#!/usr/bin/python3.14 -I
import os
import subprocess
import sys
import time

LXC = ['/usr/bin/lxc-attach', '-P', '/var/lib/waydroid/lxc', '-n', 'waydroid', '--clear-env',
       '--set-var', 'PATH=/system/bin:/system/xbin:/vendor/bin', '--']
INFO = ['/usr/bin/lxc-info', '-P', '/var/lib/waydroid/lxc', '-n', 'waydroid', '-sH']
SETUP_WIZARDS = ('com.google.android.setupwizard', 'org.lineageos.setupwizard')
DISABLED_PACKAGES = SETUP_WIZARDS
HOME_PACKAGE = 'com.openwave.tinh'
HOME_ACTIVITY = HOME_PACKAGE + '/.MainActivity'
NAVBAR_OVERLAY = 'com.android.shell:OpenWaveNavBackV2'
NAVBAR_FABRICATE = ('/system/bin/cmd', 'overlay', 'fabricate', '--target', 'com.android.systemui',
                    '--name', 'OpenWaveNavBackV2', 'com.android.systemui:string/config_navBarLayout',
                    '0x03', 'space[1W],back[1WC],space[1W];;')
OLD_NAVBAR_OVERLAYS = ('com.android.shell:OpenWaveBackOnly',)
STATUSBAR_OVERLAYS = (('OpenWaveStatusBarPortrait', 'android:dimen/status_bar_height_portrait'),
                      ('OpenWaveStatusBarLandscape', 'android:dimen/status_bar_height_landscape'),
                      ('OpenWaveStatusBarDefault', 'android:dimen/status_bar_height_default'))
STATUSBAR_DIMENSION = '0x05'
STATUSBAR_HEIGHT = '0'

DEADLINE_SECONDS = 30 * 60
SCREEN_OFF_NEVER = 2147483647
BACK_ONLY = ('/system/bin/cmd', 'statusbar', 'send-disable-flag', 'home', 'recents', 'statusbar-expansion',
             'notification-peek', 'notification-icons', 'notification-alerts', 'system-icons', 'clock')
NAVIGATION_BUTTONS = ('/system/bin/cmd', 'overlay', 'enable-exclusive', '--user', '0', '--category',
                      'com.android.internal.systemui.navbar.threebutton')
POLL_SECONDS = 5


def run(argv, timeout=20):
    try:
        result = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True,
                                text=True, timeout=timeout, env={'PATH': '/usr/bin:/bin'})
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def android(*argv, timeout=20):
    return run(LXC + list(argv), timeout=timeout)


def steps(enabled):
    commands = [('/system/bin/settings', 'put', 'global', 'device_provisioned', '1'),
                ('/system/bin/settings', 'put', 'secure', 'user_setup_complete', '1'),
                ('/system/bin/settings', 'put', 'system', 'screen_off_timeout', str(SCREEN_OFF_NEVER)),
                NAVIGATION_BUTTONS,
                BACK_ONLY]
    for package in DISABLED_PACKAGES:
        if package in enabled:
            commands.append(('/system/bin/pm', 'disable-user', '--user', '0', package))
    return commands


def statusbar_commands(overlays):
    commands = []
    for name, resource in STATUSBAR_OVERLAYS:
        if '[x] com.android.shell:' + name in overlays:
            continue
        commands.append(('/system/bin/cmd', 'overlay', 'fabricate', '--target', 'android', '--name', name,
                         resource, STATUSBAR_DIMENSION, STATUSBAR_HEIGHT))
        commands.append(('/system/bin/cmd', 'overlay', 'enable', '--user', '0', 'com.android.shell:' + name))
    return commands


def enabled_wizards():
    listing = android('/system/bin/pm', 'list', 'packages', '-e') or ''
    installed = {line.split(':', 1)[1] for line in listing.splitlines() if line.startswith('package:')}
    return [package for package in DISABLED_PACKAGES if package in installed]


def install_home():
    packages = android('/system/bin/pm', 'list', 'packages', HOME_PACKAGE) or ''
    if 'package:' + HOME_PACKAGE not in packages.split():
        print('android-first-run:', HOME_PACKAGE, 'is not installed; Android keeps its own home')
        return
    outcome = android('/system/bin/cmd', 'package', 'set-home-activity', '--user', '0', HOME_ACTIVITY)
    print('set-home-activity', HOME_ACTIVITY, '->', outcome if outcome is not None else 'failed')


def main():
    if len(sys.argv) != 1 or os.getuid() or os.geteuid():
        print('android-first-run: requires root and no arguments', file=sys.stderr)
        return 126
    with open('/proc/self/attr/current', 'rb') as stream:
        if stream.read().rstrip(b'\0\n') != b'system_u:system_r:openwave_system_t:s0':
            print('android-first-run: requires the OpenRC supervisor domain', file=sys.stderr)
            return 126
    deadline = time.monotonic() + DEADLINE_SECONDS
    while time.monotonic() < deadline:
        if run(INFO) == 'RUNNING' and android('/system/bin/getprop', 'sys.boot_completed') == '1':
            for command in steps(enabled_wizards()):
                outcome = android(*command)
                print(' '.join(command), '->', 'ok' if outcome is not None else 'failed')
            install_home()
            overlays = android('/system/bin/cmd', 'overlay', 'list') or ''
            if '[x] ' + NAVBAR_OVERLAY not in overlays:
                for old in OLD_NAVBAR_OVERLAYS:
                    if old in overlays:
                        android('/system/bin/cmd', 'overlay', 'disable', '--user', '0', old)
                for command in (NAVBAR_FABRICATE,
                                ('/system/bin/cmd', 'overlay', 'enable', '--user', '0', NAVBAR_OVERLAY),
                                ('/system/bin/am', 'crash', 'com.android.systemui')):
                    outcome = android(*command)
                    print(' '.join(command), '->', 'ok' if outcome is not None else 'failed')
            for command in statusbar_commands(android('/system/bin/cmd', 'overlay', 'list') or ''):
                outcome = android(*command)
                print(' '.join(command), '->', 'ok' if outcome is not None else 'failed')
            print('android-first-run: done; still enabled:', enabled_wizards() or 'none')
            return 0
        time.sleep(POLL_SECONDS)
    print('android-first-run: Android did not finish booting in time', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
