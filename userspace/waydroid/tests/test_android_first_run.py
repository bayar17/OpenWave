import os
"""First-run completion: fixed commands, idempotent, no shell."""
from pathlib import Path
import importlib.util
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'android_first_run.py'
spec = importlib.util.spec_from_file_location('android_first_run', SOURCE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FirstRunTests(unittest.TestCase):
    def test_marks_setup_complete_and_disables_only_enabled_wizards(self):
        commands = module.steps(['com.google.android.setupwizard'])
        self.assertIn(('/system/bin/settings', 'put', 'global', 'device_provisioned', '1'), commands)
        self.assertIn(('/system/bin/settings', 'put', 'secure', 'user_setup_complete', '1'), commands)
        self.assertIn(('/system/bin/pm', 'disable-user', '--user', '0', 'com.google.android.setupwizard'), commands)
        self.assertNotIn(('/system/bin/pm', 'disable-user', '--user', '0', 'org.lineageos.setupwizard'), commands)

    def test_already_disabled_wizards_are_not_touched_again(self):
        self.assertEqual([c for c in module.steps([]) if c[1] == 'disable-user'], [])

    def test_navbar_overlay_is_fixed_and_back_only(self):
        self.assertEqual(module.NAVBAR_OVERLAY, 'com.android.shell:OpenWaveNavBackV2')
        self.assertEqual(module.NAVBAR_FABRICATE[-1], 'space[1W],back[1WC],space[1W];;')
        self.assertIn('com.android.shell:OpenWaveBackOnly', module.OLD_NAVBAR_OVERLAYS)

    def test_android_launcher_stays_enabled(self):
        commands = module.steps(['com.android.launcher3'])
        self.assertFalse(any('com.android.launcher3' in command for command in commands))
        self.assertEqual(module.HOME_ACTIVITY, 'com.openwave.tinh/.MainActivity')


class ScreenTimeoutTests(unittest.TestCase):
    def test_android_never_switches_its_display_off(self):
        self.assertIn(('/system/bin/settings', 'put', 'system', 'screen_off_timeout', '2147483647'),
                      module.steps([]))


class NavigationModeTests(unittest.TestCase):
    def test_button_navigation_is_forced_every_boot(self):
        self.assertIn(('/system/bin/cmd', 'overlay', 'enable-exclusive', '--user', '0', '--category',
                       'com.android.internal.systemui.navbar.threebutton'), module.steps([]))

    def test_home_recents_and_the_notification_shade_are_disabled_every_boot(self):
        self.assertIn(('/system/bin/cmd', 'statusbar', 'send-disable-flag', 'home', 'recents', 'statusbar-expansion',
                       'notification-peek', 'notification-icons', 'notification-alerts', 'system-icons', 'clock'),
                      module.steps([]))

    def test_status_bar_height_is_forced_to_zero_with_three_fabricated_overlays(self):
        commands = module.statusbar_commands('')
        self.assertEqual(len(commands), 6)
        fabricated = [c for c in commands if c[2] == 'fabricate']
        self.assertEqual(len(fabricated), 3)
        for command in fabricated:
            self.assertEqual(command[3:5], ('--target', 'android'))
            self.assertEqual(command[-2:], ('0x05', '0'))
            self.assertTrue(command[-3].startswith('android:dimen/status_bar_height'))
        self.assertEqual(len([c for c in commands if c[2] == 'enable']), 3)

    def test_status_bar_overlays_are_not_repeated_once_enabled(self):
        listing = '\n'.join('[x] com.android.shell:' + name for name, _ in module.STATUSBAR_OVERLAYS)
        self.assertEqual(module.statusbar_commands(listing), [])

    def test_back_is_never_disabled(self):
        flags = [c for c in module.steps([]) if c[1:3] == ('statusbar', 'send-disable-flag')]
        self.assertEqual(len(flags), 1)
        self.assertNotIn('back', flags[0])
