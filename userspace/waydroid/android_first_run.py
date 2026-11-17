#!/usr/bin/python3.14 -I
"""Complete Android's first-run setup once Android has booted.

Started in the background by the OpenRC `waydroid` service. OpenWave never
shows Android's first-run setup: the GApps image's setup wizard otherwise owns
Android's screen and returns on top of every launched application (Stage 8),
and its boot switch (ro.setupwizard.mode=DISABLED) is ignored by this
Android 17 build. After sys.boot_completed=1 this marks the device set up and
disables the setup wizard packages for user 0, which is what finishing setup
by hand achieves.

It runs in OpenRC's domain and reaches Android only through lxc-attach (the
administrative attach domain), with fixed argument vectors: no host shell, no
caller input. It is idempotent and exits when done or after 30 minutes.
"""
import os
import subprocess
import sys
import time

LXC = ['/usr/bin/lxc-attach', '-P', '/var/lib/waydroid/lxc', '-n', 'waydroid', '--clear-env',
       '--set-var', 'PATH=/system/bin:/system/xbin:/vendor/bin', '--']
INFO = ['/usr/bin/lxc-info', '-P', '/var/lib/waydroid/lxc', '-n', 'waydroid', '-sH']
SETUP_WIZARDS = ('com.google.android.setupwizard', 'org.lineageos.setupwizard')
DISABLED_PACKAGES = SETUP_WIZARDS
# Ocean is the home screen; Android's home is OpenWave's empty TINH screen,
# which returns a still-running app when Android would show home, so Back
# never ends in Trebuchet or on an Android home. TINH is a privileged system
# app from Waydroid's overlay (install_home.py); this step only makes it the
# default home. Trebuchet stays enabled: on this Android 17 build disabling
# it also removes the navigation bar (runs 8z/8za).
HOME_PACKAGE = 'com.openwave.tinh'
HOME_ACTIVITY = HOME_PACKAGE + '/.MainActivity'
# Android's navigation bar with Back alone, centred: a runtime resource
# overlay (cmd overlay fabricate) on SystemUI's navigation-bar layout. It is
# stored in Android's data; SystemUI reads the layout when it builds the bar,
# so it is restarted once, when the overlay is first enabled.
# One group with equal spacers either side: this SystemUI places Back at the
# start when it is given its own centre group (Stage 8 run 8y).
NAVBAR_OVERLAY = 'com.android.shell:OpenWaveNavBackV2'
NAVBAR_FABRICATE = ('/system/bin/cmd', 'overlay', 'fabricate', '--target', 'com.android.systemui',
                    '--name', 'OpenWaveNavBackV2', 'com.android.systemui:string/config_navBarLayout',
                    '0x03', 'space[1W],back[1WC],space[1W];;')
OLD_NAVBAR_OVERLAYS = ('com.android.shell:OpenWaveBackOnly',)
DEADLINE_SECONDS = 30 * 60
SCREEN_OFF_NEVER = 2147483647
BACK_ONLY = ('/system/bin/cmd', 'statusbar', 'send-disable-flag', 'home', 'recents')
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
    """Fixed commands that complete setup; `enabled` lists installed, enabled wizards."""
    commands = [('/system/bin/settings', 'put', 'global', 'device_provisioned', '1'),
                ('/system/bin/settings', 'put', 'secure', 'user_setup_complete', '1'),
                # Ocean owns the display and power. Android's own screen
                # timeout would switch its display off after a minute without
                # Android input, and Waydroid then freezes the whole container
                # (IHardware.suspend): the window stops responding, and Android
                # shows "System UI isn't responding" once thawed (runs 8zd-8zg).
                ('/system/bin/settings', 'put', 'system', 'screen_off_timeout', str(SCREEN_OFF_NEVER)),
                # Button navigation, always. Some boots come up in gesture
                # navigation (runs 8zc, 8zf, 8zj): a thin pill and no Back
                # button at all. This is Android's own switch for the mode.
                NAVIGATION_BUTTONS,
                # Back only. Android's own switch for hiding Home and Recents
                # (the one its setup wizard and kiosk mode use), honoured by
                # whichever component draws the bar. The layout overlay below
                # is not enough: Trebuchet draws the bar on this build and
                # showed all three buttons again in runs 8zk/8zl.
                BACK_ONLY]
    for package in DISABLED_PACKAGES:
        if package in enabled:
            commands.append(('/system/bin/pm', 'disable-user', '--user', '0', package))
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
        # A frozen container would block lxc-attach; wait for it to run.
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
            print('android-first-run: done; still enabled:', enabled_wizards() or 'none')
            return 0
        time.sleep(POLL_SECONDS)
    print('android-first-run: Android did not finish booting in time', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
