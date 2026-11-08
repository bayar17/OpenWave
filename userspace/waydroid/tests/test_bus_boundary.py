"""Exercise effective D-Bus routing, including unique-name bypass attempts."""
from pathlib import Path
import json
import os
import selectors
import subprocess
import sys
import tempfile
import unittest

import dbus

SOURCE = Path(__file__).resolve().parents[1] / 'zz-openwave-waydroid.conf'
FIXTURE = '''
import dbus, dbus.service, json, sys
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
DBusGMainLoop(set_as_default=True)
bus = dbus.bus.BusConnection(sys.argv[1])
name = dbus.service.BusName('id.waydro.Container', bus)
class Manager(dbus.service.Object):
    @dbus.service.method('id.waydro.ContainerManager', in_signature='a{ss}', out_signature='s')
    def Start(self, session):
        return 'routed'
    @dbus.service.method('id.waydro.Initializer', in_signature='', out_signature='s')
    def Initialize(self):
        return 'must not be routed'
manager = Manager(bus, '/ContainerManager')
print(json.dumps(bus.get_unique_name()), flush=True)
GLib.MainLoop().run()
'''


def line(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(5):
            raise RuntimeError('D-Bus fixture did not become ready')
    value = process.stdout.readline().strip()
    if not value:
        raise RuntimeError(process.stderr.read())
    return value


class BusBoundaryTests(unittest.TestCase):
    def test_native_clients_denied_and_session_account_limited(self):
        self.assertNotEqual(os.getuid(), 0, 'Run this negative client test as an ordinary host user')
        for session_account in (False, True):
            with self.subTest(session_account=session_account), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                # Map only the dedicated account to this test process. The
                # unauthorized case uses an unrelated numeric UID.
                boundary = SOURCE.read_text().replace('user="waydroid"',
                    f'user="{os.getuid() if session_account else 65533}"')
                (directory / 'boundary.conf').write_text(boundary)
                (directory / 'bus.conf').write_text('''<busconfig>
<type>system</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow user="*"/><allow own="*"/>
<allow send_destination="*"/><allow receive_sender="*"/></policy>
<include>boundary.conf</include></busconfig>''')
                daemon = subprocess.Popen(['dbus-daemon', '--nofork', '--print-address=1',
                    '--config-file=' + str(directory / 'bus.conf')],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                fixture = None
                connection = None
                try:
                    address = line(daemon)
                    fixture = subprocess.Popen([sys.executable, '-c', FIXTURE, address],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    unique = json.loads(line(fixture))
                    connection = dbus.bus.BusConnection(address)
                    for destination in ('id.waydro.Container', unique):
                        if session_account:
                            result = connection.call_blocking(destination, '/ContainerManager',
                                'id.waydro.ContainerManager', 'Start', 'a{ss}', [{}], timeout=2)
                            self.assertEqual(result, 'routed')
                        else:
                            with self.assertRaises(dbus.DBusException) as error:
                                connection.call_blocking(destination, '/ContainerManager',
                                    'id.waydro.ContainerManager', 'Start', 'a{ss}', [{}], timeout=2)
                            self.assertEqual(error.exception.get_dbus_name(),
                                'org.freedesktop.DBus.Error.AccessDenied')
                        with self.assertRaises(dbus.DBusException) as error:
                            connection.call_blocking(destination, '/ContainerManager',
                                'id.waydro.Initializer', 'Initialize', '', [], timeout=2)
                        self.assertEqual(error.exception.get_dbus_name(),
                            'org.freedesktop.DBus.Error.AccessDenied')
                finally:
                    if connection is not None:
                        connection.close()
                    for process in (fixture, daemon):
                        if process is not None:
                            process.terminate()
                            process.communicate(timeout=5)
