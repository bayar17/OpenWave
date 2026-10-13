"""Private D-Bus fixture exercising the real asynchronous Qt connectivity client."""
from pathlib import Path
import os
import subprocess
import sys
import threading
from gi.repository import Gio, GLib

xml = (Path(__file__).resolve().parents[4] / "userspace/dbus/interfaces/org.openwave.Connectivity1.xml").read_text()

bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'],
                       stdout=subprocess.PIPE, text=True)
address = bus.stdout.readline().strip()
os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = address
connection = Gio.DBusConnection.new_for_address_sync(address,
    Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
    None, None)
connection.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
    'org.freedesktop.DBus', 'RequestName', GLib.Variant('(su)', ('org.openwave.Connectivity', 0)),
    None, Gio.DBusCallFlags.NONE, 5000, None)
devices = {}
for index in range(1, 4):
    path = f'/org/bluez/hci0/dev_0{index}_02_03_04_05_06'
    devices[path] = {'path': GLib.Variant('o', path), 'name': GLib.Variant('s', f'Device {index}'),
                     'address': GLib.Variant('s', f'0{index}:02:03:04:05:06'),
                     'paired': GLib.Variant('b', False), 'connected': GLib.Variant('b', False)}
enabled = True
operations = []

def handle(conn, sender, path, interface, method, parameters, invocation):
    global enabled
    args = parameters.unpack()
    if method == 'GetState':
        invocation.return_value(GLib.Variant('(a{sv})', ({
            'bluetoothAdapterAvailable': GLib.Variant('b', True),
            'bluetoothEnabled': GLib.Variant('b', enabled)},)))
    elif method == 'ListBluetoothDevices':
        invocation.return_value(GLib.Variant('(aa{sv})', (list(devices.values()),)))
    elif method == 'SetBluetoothEnabled':
        enabled = args[0]
        operations.append((method, enabled))
        invocation.return_value(None)
    else:
        device_path = args[0]
        operations.append((method, device_path))
        if method == 'PairDevice':
            def finish_pair():
                if 'dev_02_' in device_path:
                    invocation.return_dbus_error('org.openwave.Connectivity.OperationFailed', 'Pair rejected')
                else:
                    devices[device_path]['paired'] = GLib.Variant('b', True)
                    invocation.return_value(None)
                return GLib.SOURCE_REMOVE
            GLib.timeout_add(100, finish_pair)
        elif method == 'ConnectDevice' and 'dev_03_' in device_path:
            invocation.return_dbus_error('org.openwave.Connectivity.OperationFailed', 'Connection rejected')
        else:
            if method == 'ConnectDevice': devices[device_path]['connected'] = GLib.Variant('b', True)
            elif method == 'DisconnectDevice': devices[device_path]['connected'] = GLib.Variant('b', False)
            elif method == 'SetDeviceAlias': devices[device_path]['alias'] = GLib.Variant('s', args[1])
            elif method == 'RemoveDevice': del devices[device_path]
            invocation.return_value(None)

node = Gio.DBusNodeInfo.new_for_xml(xml)
registration = connection.register_object('/org/openwave/Connectivity', node.interfaces[0], handle, None, None)
loop = GLib.MainLoop()
probe = subprocess.Popen([sys.argv[1]])
result = []
def wait_probe():
    try: result.append(probe.wait(timeout=30))
    except subprocess.TimeoutExpired:
        probe.kill(); probe.wait(); result.append(1)
    GLib.idle_add(loop.quit)
thread = threading.Thread(target=wait_probe)
thread.start()
try:
    loop.run()
    thread.join()
    normal, failed, connection_failed = [f'/org/bluez/hci0/dev_0{i}_02_03_04_05_06' for i in range(1, 4)]
    assert ('ConnectDevice', failed) not in operations, 'Pair failure triggered Connect'
    assert operations.count(('ConnectDevice', normal)) == 1
    assert operations.count(('ConnectDevice', connection_failed)) == 1, 'Queued power-off failed to suppress Connect'
    assert ('SetDeviceAlias', normal) in operations
    print('PASS: private D-Bus operation sequence', flush=True)
finally:
    connection.unregister_object(registration)
    connection.close_sync(None)
    bus.terminate(); bus.wait()
sys.exit(result[0])
