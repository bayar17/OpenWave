#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

PUBLIC = 'org.freedesktop.portal.Desktop'
BACKEND = 'org.freedesktop.impl.portal.desktop.openwave'
PATH = '/org/freedesktop/portal/desktop'
APPEARANCE = 'org.freedesktop.appearance'
DESKTOP = 'org.gnome.desktop.interface'
INTERFACES = Path(__file__).resolve().parents[2] / 'dbus/interfaces'


def wait(predicate, message):
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError(message)


def expect_error(action, text):
    try:
        action()
    except GLib.Error as error:
        assert text in str(error), error
        return
    raise AssertionError('Expected ' + text)


def check_name_policy(directory):
    if os.geteuid() == 0:
        return
    source = Path(__file__).resolve().parents[2] / 'config/openwave-session-bus.conf'
    config = Path(directory) / 'session.conf'
    config.write_text(source.read_text().replace('unix:path=/run/ocean/bus',
                                                'unix:path=' + directory + '/policy-bus'))
    daemon = subprocess.Popen(['dbus-daemon', '--config-file=' + str(config), '--nofork', '--print-address'],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    connection = None
    try:
        address = daemon.stdout.readline().strip()
        assert address, 'Session policy failed to load'
        connection = Gio.DBusConnection.new_for_address_sync(address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
        for name in (PUBLIC, BACKEND, 'org.openwave.Prompt'):
            expect_error(lambda: connection.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
                'org.freedesktop.DBus', 'RequestName', GLib.Variant('(su)', (name, 4)),
                None, Gio.DBusCallFlags.NONE, 3000, None), 'AccessDenied')
    finally:
        if connection: connection.close_sync(None)
        daemon.terminate(); daemon.wait(timeout=5)


def main():
    command = sys.argv[1:]
    assert command, 'Pass the executable (or target loader and executable)'
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    processes = []
    subscriptions = []

    def iface(name):
        return 'org.freedesktop.impl.portal.Settings' if name == BACKEND else 'org.freedesktop.portal.Settings'

    def call(name, method, parameters=None, interface=None):
        return bus.call_sync(name, PATH, interface or iface(name), method, parameters,
                             None, Gio.DBusCallFlags.NONE, 3000, None)

    def owned(name):
        reply = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', 'NameHasOwner', GLib.Variant('(s)', (name,)),
            None, Gio.DBusCallFlags.NONE, 1000, None)
        return reply[0]

    with tempfile.TemporaryDirectory(prefix='openwave-xdg-settings-') as directory:
        preferences = Path(directory) / 'new/config/settings.ini'
        log = open(Path(directory) / 'portal.log', 'w+')
        args = ['--preferences', str(preferences), '--interfaces', str(INTERFACES)]

        def start(backend=False):
            process = subprocess.Popen(command + args + (['--backend'] if backend else []), stdout=log, stderr=log)
            processes.append(process)
            name = BACKEND if backend else PUBLIC
            wait(lambda: owned(name) or process.poll() is not None, 'Portal did not start')
            assert process.poll() is None, 'Portal exited; inspect log'
            return process

        def write(reduced, large=True):
            preferences.parent.mkdir(parents=True, exist_ok=True)
            temporary = preferences.with_suffix('.new')
            temporary.write_text('[accessibility]\nreduceMotion=' + str(reduced).lower()
                + '\nlargeText=' + str(large).lower()
                + '\n[device]\nname=PRIVATE DEVICE\npassword=PRIVATE SECRET\n')
            temporary.replace(preferences)

        try:
            backend = start()
            assert owned(BACKEND)
            for name in (PUBLIC, BACKEND):
                version = call(name, 'Get', GLib.Variant('(ss)', (iface(name), 'version')),
                               'org.freedesktop.DBus.Properties')
                assert version[0] == 2
                all_values = call(name, 'ReadAll', GLib.Variant('(as)', ([],)))[0]
                assert set(all_values) == {APPEARANCE, DESKTOP}
                assert all_values[APPEARANCE] == {'color-scheme': 0, 'contrast': 0, 'reduced-motion': 0}
                assert all_values[DESKTOP]['font-name'] == 'Source Sans Pro'
                assert all_values[DESKTOP]['enable-animations'] is True
                for patterns, wanted in [([''], 2), (['org.*'], 2), (['org.gnome.*'], 1),
                                         ([APPEARANCE], 1), (['org.'], 0), (['missing'], 0),
                                         (['org*'], 0), (['missing', ''], 2)]:
                    assert len(call(name, 'ReadAll', GLib.Variant('(as)', (patterns,)))[0]) == wanted
                for ns, key in [('missing', 'color-scheme'), (APPEARANCE, 'password')]:
                    expect_error(lambda: call(name, 'Read', GLib.Variant('(ss)', (ns, key))), 'NotFound')
                expect_error(lambda: call(name, 'Read', GLib.Variant('(s)', (APPEARANCE,))), 'InvalidArgs')
                expect_error(lambda: call(name, 'Set', GLib.Variant('(ssv)',
                    (iface(name), 'version', GLib.Variant('u', 99))), 'org.freedesktop.DBus.Properties'), 'InvalidArgs')

            old = call(PUBLIC, 'Read', GLib.Variant('(ss)', (APPEARANCE, 'color-scheme'))).get_child_value(0)
            assert old.get_type_string() == 'v' and old.get_variant().get_type_string() == 'v'
            modern = call(PUBLIC, 'ReadOne', GLib.Variant('(ss)', (APPEARANCE, 'color-scheme'))).get_child_value(0)
            direct = call(BACKEND, 'Read', GLib.Variant('(ss)', (APPEARANCE, 'color-scheme'))).get_child_value(0)
            assert modern.get_variant().get_type_string() == direct.get_variant().get_type_string() == 'u'
            events = {PUBLIC: [], BACKEND: []}
            for name in events:
                subscriptions.append(bus.signal_subscribe(name, iface(name), 'SettingChanged', PATH,
                    None, Gio.DBusSignalFlags.NONE,
                    lambda connection, sender, path, interface, signal, values, owner=name:
                        events[owner].append(values.unpack())))
            write(True)
            wait(lambda: all(len(e) == 3 for e in events.values()), 'Missing changes after creating preference directory')
            for name in events:
                values = call(name, 'ReadAll', GLib.Variant('(as)', ([],)))[0]
                assert values[APPEARANCE]['reduced-motion'] == 1
                assert values[DESKTOP]['enable-animations'] is False
                assert values[DESKTOP]['text-scaling-factor'] == 1.2
                assert 'PRIVATE' not in str(values)
            write(True)
            time.sleep(.3)
            while GLib.MainContext.default().pending(): GLib.MainContext.default().iteration(False)
            assert all(len(e) == 3 for e in events.values()), 'Unchanged values emitted duplicate signals'
            preferences.write_text('[malformed\n')
            time.sleep(.3)
            assert call(PUBLIC, 'ReadOne', GLib.Variant('(ss)', (APPEARANCE, 'reduced-motion')))[0] == 1
            preferences.unlink()
            wait(lambda: all(len(e) == 6 for e in events.values()), 'Deleting preferences did not restore defaults')
            write(True, False)
            wait(lambda: all(len(e) == 8 for e in events.values()), 'Atomic replacement did not update settings')
            duplicate = subprocess.run(command + args + ['--backend'], stdout=log, stderr=log, timeout=5)
            assert duplicate.returncode == 1 and backend.poll() is None
            backend.terminate(); backend.wait(timeout=5)
            wait(lambda: not owned(BACKEND), 'Backend name survived process exit')
            start(True)
            assert not owned(PUBLIC)
            assert call(BACKEND, 'Read', GLib.Variant('(ss)', (APPEARANCE, 'reduced-motion')))[0] == 1
            check_name_policy(directory)
            print('PASS: public/backend protocol, variants, filters, privacy, read-only API, live changes, malformed settings, restart, name ownership')
        except Exception:
            log.flush(); log.seek(0); sys.stderr.write(log.read())
            raise
        finally:
            for subscription in subscriptions: bus.signal_unsubscribe(subscription)
            for process in processes:
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=5)
            log.close()


if __name__ == '__main__':
    main()
