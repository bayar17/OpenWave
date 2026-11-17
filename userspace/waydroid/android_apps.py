#!/usr/bin/python3.14 -I
"""appd's fixed-operation bridge to Android's Waydroid platform service.

Installed as /usr/libexec/openwave-waydroid/android-apps and executed only by
appd (SELinux: openwave_appd_t -> openwave_waydroid_apps_t). Operations:

  list               JSON catalogue of launchable Android applications
  launch PACKAGE     start PACKAGE's launcher activity
  close PACKAGE      close one running Android application (its card was
                     closed in Ocean): one fixed request to OpenWave's Android
                     home (TINH), which only Android's system can send
  close-all          the same for every running Android application
  watch              host Waydroid's user monitor service: one JSON line per
                     Android event (user unlocked, package added/removed/
                     updated) until stopped

`watch` is the part of upstream's per-user session (tools/services/
user_manager.py) that Android requires: without a registered
"waydroidusermonitor" Android reports that Waydroid integration is not
functional. Upstream answers by writing host .desktop files; OpenWave instead
tells appd, which refreshes its own catalogue at once. Nothing is written.

It talks to exactly one Android binder service, "waydroidplatform" (Waydroid's
IPlatform), using the transactions pinned upstream Waydroid's CLI uses for the
same purpose (tools/interfaces/IPlatform.py, tools/actions/app_manager.py).
It never runs a shell, never accepts a path or intent, and reads no
environment. The container must already be running and thawed; appd asks the
Waydroid manager for that over D-Bus before calling this helper.

Everything read back from Android is untrusted: names are bounded and stripped
of control characters, package names must be valid Android package names, and
icons are re-validated PNG files of bounded size and dimensions.
"""
import base64
from pathlib import Path
import configparser
import json
import os
import re
import signal
import stat
import struct
import sys
import time

SERVICE = 'waydroidplatform'
# Newer Waydroid images renamed the interface; the transaction codes are the
# same (Stage 8 run 8d: code 7 started the requested activity).
INTERFACES = ('id.waydro.waydroid.IPlatform', 'lineageos.waydroid.IPlatform')
TRANSACTION_GETPROP = 1
TRANSACTION_SETPROP = 2
TRANSACTION_GET_APPS_INFO = 3
TRANSACTION_LAUNCH_APP = 7
TRANSACTION_SETTINGS_PUT_STRING = 9
TRANSACTION_SETTINGS_PUT_INT = 11
TRANSACTION_SETTINGS_GET_STRING = 10
TRANSACTION_SETTINGS_GET_INT = 12
TRANSACTION_LAUNCH_INTENT = 13
# Handled by TINH's CloseAppsActivity; no caller-supplied data.
CLOSE_APPS_ACTION = 'com.openwave.tinh.action.CLOSE_APPS'
SETTINGS_SECURE = 1
SETTINGS_GLOBAL = 2

CONFIG = '/var/lib/waydroid/waydroid.cfg'
ICONS = '/var/lib/waydroid-user/data/icons'
PACKAGE = re.compile(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+')
MAX_PACKAGE = 128 - len('org.openwave.android.')
MAX_NAME = 64
MAX_APPS = 512
MAX_ICON_BYTES = 256 * 1024
MAX_ICON_SIDE = 512
LAUNCHER = 'android.intent.category.LAUNCHER'
# Never offered as OpenWave applications: Android's own home screen and
# first-run setup, and Waydroid's updater (OpenWave updates images itself).
HIDDEN = frozenset({'com.android.launcher3', 'org.lineageos.setupwizard',
                    'org.lineageos.waydroidupdater', 'com.openwave.tinh'})
TIME_LIMIT = 20
USER_MONITOR = 'waydroidusermonitor'
# Android 17 Waydroid images renamed the interfaces (see INTERFACES); answer
# to both names. libgbinder rejects calls for an interface not listed here.
USER_MONITOR_INTERFACES = ['id.waydro.waydroid.IUserMonitor', 'lineageos.waydroid.IUserMonitor']
TRANSACTION_USER_UNLOCKED = 1
TRANSACTION_PACKAGE_STATE_CHANGED = 2
PACKAGE_MODES = {0: 'added', 1: 'removed', 2: 'updated'}
# Written by TINH (Android's home in OpenWave): 0 while an Android app is in
# front, else a stamp that is new each time Android was left with no app to
# show. `watch` reads it once a second over the same platform service.
IDLE_SETTING = 'openwave_android_idle'
IDLE_POLL_MS = 1000
# Also written by TINH: the packages that have a running task (most recent
# first, comma separated) and a stamp that changes with the list. appd turns
# the list into per-application state, so Ocean shows a card per running app.
TASKS_SETTING = 'openwave_android_tasks'
TASKS_SEQ_SETTING = 'openwave_android_tasks_seq'
MAX_TASKS_TEXT = 4096
LOOP_TICK_SECONDS = 0.1


class Failure(Exception):
    pass


def valid_package(value):
    return isinstance(value, str) and len(value) <= MAX_PACKAGE and bool(PACKAGE.fullmatch(value))


def clean_name(value):
    if not isinstance(value, str):
        return ''
    value = ''.join(ch for ch in value if ch.isprintable()).strip()
    return value[:MAX_NAME]


def png_dimensions(data):
    """(width, height) of a PNG, or None if it is not a plausible PNG."""
    if len(data) < 33 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
        return None
    width, height = struct.unpack('>II', data[16:24])
    return (width, height) if width and height else None


def read_icon(package, directory=ICONS):
    """Android-written icon for package as bytes, or b'' if absent or invalid."""
    try:
        fd = os.open(os.path.join(directory, package + '.png'),
                     os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    except OSError:
        return b''
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or not 0 < st.st_size <= MAX_ICON_BYTES:
            return b''
        data = os.read(fd, MAX_ICON_BYTES + 1)
    finally:
        os.close(fd)
    size = png_dimensions(data)
    if len(data) > MAX_ICON_BYTES or not size or max(size) > MAX_ICON_SIDE:
        return b''
    return data


def catalogue(apps, icon_reader=read_icon):
    """Validated, de-duplicated catalogue entries from IPlatform.getAppsInfo."""
    entries = {}
    for app in apps[:MAX_APPS]:
        package = app.get('packageName')
        if not valid_package(package) or package in HIDDEN or package in entries:
            continue
        if LAUNCHER not in app.get('categories', ()):
            continue
        name = clean_name(app.get('name')) or package
        icon = icon_reader(package)
        entries[package] = {'package': package, 'name': name,
                            'icon': base64.b64encode(icon).decode() if icon else ''}
    return [entries[key] for key in sorted(entries)]


def monitor_event(code, mode=None, package=None):
    """The JSON event appd receives for one IUserMonitor call, or None."""
    if code == TRANSACTION_USER_UNLOCKED:
        return {'event': 'unlocked'}
    if code == TRANSACTION_PACKAGE_STATE_CHANGED and mode in PACKAGE_MODES and valid_package(package):
        return {'event': 'package', 'change': PACKAGE_MODES[mode], 'package': package}
    return None


def tasks_event(text):
    """The event for one reading of TASKS_SETTING: valid package names only."""
    packages = []
    if isinstance(text, str) and len(text) <= MAX_TASKS_TEXT:
        for name in text.split(','):
            if valid_package(name) and name not in HIDDEN and name not in packages:
                packages.append(name)
    return {'event': 'tasks', 'packages': packages[:MAX_APPS]}


def idle_event(value, reported):
    """(event or None, stamp now reported) for one reading of IDLE_SETTING.

    Each non-zero stamp is reported once, so a reading that is merely still
    idle (for example just after a launch was requested) says nothing new.
    """
    if not isinstance(value, int) or value <= 0 or value == reported:
        return None, reported
    return {'event': 'idle'}, value


def emit(event, stream=None):
    stream = stream or sys.stdout
    stream.write(json.dumps(event, separators=(',', ':')) + '\n')
    stream.flush()


def binder_device():
    config = configparser.ConfigParser()
    if not config.read(CONFIG) or not config.has_section('waydroid'):
        raise Failure('Waydroid is not initialized')
    section = config['waydroid']
    driver = section.get('binder', '')
    if not re.fullmatch(r'[a-z][a-z0-9-]{0,31}', driver):
        raise Failure('unexpected Binder driver name')
    path = '/dev/' + driver
    if not stat.S_ISCHR(os.stat(path).st_mode):
        raise Failure('Binder device is not a character device')
    return path, section.get('service_manager_protocol', ''), section.get('binder_protocol', '')


class Platform:
    def __init__(self):
        import gbinder
        path, manager_protocol, binder_protocol = binder_device()
        try:
            manager = gbinder.ServiceManager(path, manager_protocol, binder_protocol)
        except TypeError:
            manager = gbinder.ServiceManager(path)
        if not manager.is_present():
            raise Failure('Android service manager is not running')
        remote, _ = manager.get_service_sync(SERVICE)
        if not remote:
            raise Failure('Android platform service is unavailable')
        for interface in INTERFACES:
            self.client = gbinder.Client(remote, interface)
            try:
                self.getprop('ro.build.version.sdk')
                return
            except Failure:
                continue
        raise Failure('Android platform service rejected every known interface')

    def call(self, code, *strings, integer=None, trailing_integer=None):
        request = self.client.new_request()
        if integer is not None:
            request.append_int32(integer)
        for value in strings:
            request.append_string16(value)
        if trailing_integer is not None:
            request.append_int32(trailing_integer)
        reply, status = self.client.transact_sync_reply(code, request)
        if status:
            raise Failure(f'Binder transaction {code} failed ({status})')
        reader = reply.init_reader()
        _, exception = reader.read_int32()
        if exception:
            raise Failure(f'Android rejected transaction {code} ({exception})')
        return reader

    def getprop(self, name):
        return self.call(TRANSACTION_GETPROP, name, '').read_string16()

    def apps(self):
        reader = self.call(TRANSACTION_GET_APPS_INFO)
        _, count = reader.read_int32()
        apps = []
        for _ in range(min(max(count, 0), MAX_APPS)):
            _, present = reader.read_int32()
            if present != 1:
                continue
            app = {key: reader.read_string16() for key in (
                'name', 'packageName', 'action', 'launchIntent',
                'componentPackageName', 'componentClassName')}
            _, categories = reader.read_int32()
            app['categories'] = [reader.read_string16() for _ in range(min(max(categories, 0), 32))]
            apps.append(app)
        return apps

    def launch(self, package):
        # OpenWave shows Android as one window (Waydroid's full-UI mode, as
        # upstream's show-full-ui sets it) with Android's own navigation bar
        # and without its status bar, which Ocean replaces. Per-application
        # windows need the host android_wlegl protocol (Stage 8 runs 8k-8q).
        self.call(TRANSACTION_SETPROP, 'waydroid.active_apps', 'Waydroid')
        # Android's first-run setup is never shown in OpenWave: mark the
        # device set up (idempotent; IPlatform.settingsPutInt).
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'device_provisioned', integer=SETTINGS_GLOBAL, trailing_integer=1)
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'user_setup_complete', integer=SETTINGS_SECURE, trailing_integer=1)
        self.call(TRANSACTION_LAUNCH_APP, package)
        self.call(TRANSACTION_SETTINGS_PUT_STRING, 'policy_control', 'immersive.status=*',
                  integer=SETTINGS_GLOBAL)


def watch():
    """Serve waydroidusermonitor until Android's service manager goes away."""
    import gbinder
    from gi.repository import GLib
    path, manager_protocol, binder_protocol = binder_device()
    try:
        manager = gbinder.ServiceManager(path, manager_protocol, binder_protocol)
    except TypeError:
        manager = gbinder.ServiceManager(path)
    # Iterate the default context directly: GLib.MainLoop.run() in PyGObject
    # creates a socketpair for Ctrl-C handling, and this domain may not
    # create sockets (run 8ze: PermissionError right after registering).
    context = GLib.MainContext.default()
    outcome = {'status': 1, 'running': True}

    def stop():
        outcome['running'] = False
        context.wakeup()

    def handler(request, code, flags):
        reader = request.init_reader()
        reply = local.new_reply()
        if code == TRANSACTION_USER_UNLOCKED:
            event = monitor_event(code)
        elif code == TRANSACTION_PACKAGE_STATE_CHANGED:
            _, mode = reader.read_int32()
            package = reader.read_string16()
            event = monitor_event(code, mode, package)
        else:
            return reply, -99999   # unknown transaction: Android sees a RemoteException
        try:
            if event:
                emit(event)
        except (BrokenPipeError, OSError):
            stop()                 # appd has gone away
        reply.append_int32(0)
        return reply, 0

    local = manager.new_local_object(USER_MONITOR_INTERFACES, handler)

    def presence():
        if not manager.is_present():
            # Android stopped: exit, appd starts a new watcher with the next session.
            outcome['status'] = 0
            stop()
            return
        status = manager.add_service_sync(USER_MONITOR, local)
        if status:
            emit({'event': 'error', 'message': f'cannot register {USER_MONITOR} ({status})'})
            stop()
            return
        emit({'event': 'registered'})

    state = {'platform': None, 'reported': None}

    def poll_idle():
        try:
            if state['platform'] is None:
                state['platform'] = Platform()
            # The integer call: the string call returns a null string while
            # the setting is unset, which crashes the binding (run 8zp).
            _, value = state['platform'].call(TRANSACTION_SETTINGS_GET_INT, IDLE_SETTING,
                                              integer=SETTINGS_GLOBAL).read_int32()
        except Exception as error:   # noqa: BLE001 - Android not ready or gone: retry next tick
            state['platform'] = None
            if not state.get('warned'):
                state['warned'] = True
                try:
                    emit({'event': 'note', 'message': f'idle reading failed: {type(error).__name__}: {error}'[:200]})
                except (BrokenPipeError, OSError):
                    stop()
            return
        try:
            _, stamp = state['platform'].call(TRANSACTION_SETTINGS_GET_INT, TASKS_SEQ_SETTING,
                                              integer=SETTINGS_GLOBAL).read_int32()
            # The list is read only once its stamp exists: a string setting
            # that is unset comes back as a null string, which crashes the
            # binding (run 8zp). The first reading is reported, so appd
            # starts from Android's real state.
            if isinstance(stamp, int) and stamp > 0 and stamp != state.get('tasks'):
                text = state['platform'].call(TRANSACTION_SETTINGS_GET_STRING, TASKS_SETTING,
                                              integer=SETTINGS_GLOBAL).read_string16()
                state['tasks'] = stamp
                emit(tasks_event(text))
        except (BrokenPipeError, OSError):
            stop()
            return
        except Exception:   # noqa: BLE001 - retry next tick
            state['platform'] = None
            return
        if state['reported'] is None:
            # What was there when the watcher started is not news.
            state['reported'] = value if isinstance(value, int) else 0
            return
        event, state['reported'] = idle_event(value, state['reported'])
        try:
            if event:
                emit(event)
        except (BrokenPipeError, OSError):
            stop()

    if not manager.is_present():
        raise Failure('Android service manager is not running')
    presence()
    handle = manager.add_presence_handler(presence)
    # No GLib timer: a Python callback registered with GLib needs an
    # executable trampoline from libffi, which this domain (no execmem, no
    # writable directory) cannot allocate (run 8zo). Serve binder events in
    # short non-blocking turns and read the idle setting once a second.
    next_poll = time.monotonic() + IDLE_POLL_MS / 1000
    while outcome['running']:
        while context.pending():
            context.iteration(False)
        if time.monotonic() >= next_poll:
            poll_idle()
            next_poll = time.monotonic() + IDLE_POLL_MS / 1000
        time.sleep(LOOP_TICK_SECONDS)
    manager.remove_handler(handle)
    return outcome['status']


def close_all(platform):
    """Ask Android (IPlatform.launchIntent, as `waydroid app intent`) to close its apps."""
    platform.call(TRANSACTION_LAUNCH_INTENT, CLOSE_APPS_ACTION, '')


def close_one(platform, package):
    """Ask Android to close one application; `package` is already validated."""
    platform.call(TRANSACTION_LAUNCH_INTENT, CLOSE_APPS_ACTION, 'package:' + package)


def main(argv):
    if os.getuid() or os.geteuid():
        print('android-apps: requires the appd root identity', file=sys.stderr)
        return 126
    try:
        if argv == ['watch']:
            # appd discards helper stderr; report why the watcher stopped
            # on the channel appd reads.
            try:
                return watch()
            except Exception as error:   # noqa: BLE001 - reported, then exit
                import traceback
                where = ' <- '.join(f'{Path(frame.filename).name}:{frame.lineno}:{frame.name}'
                                    for frame in reversed(traceback.extract_tb(error.__traceback__)[-3:]))
                emit({'event': 'error', 'message': f'{type(error).__name__}: {error} at {where}'[:300]})
                return 1
        signal.alarm(TIME_LIMIT)
        if argv == ['list']:
            print(json.dumps({'apps': catalogue(Platform().apps())}, separators=(',', ':')))
            return 0
        if len(argv) == 2 and argv[0] == 'launch' and valid_package(argv[1]) and argv[1] not in HIDDEN:
            Platform().launch(argv[1])
            print(json.dumps({'launched': argv[1]}))
            return 0
        if len(argv) == 2 and argv[0] == 'close' and valid_package(argv[1]) and argv[1] not in HIDDEN:
            close_one(Platform(), argv[1])
            print(json.dumps({'closed': argv[1]}))
            return 0
        if argv == ['close-all']:
            close_all(Platform())
            print(json.dumps({'closed': 'all'}))
            return 0
        print('android-apps: usage: list | launch PACKAGE | close PACKAGE | close-all | watch', file=sys.stderr)
        return 2
    except (Failure, OSError) as error:
        print(f'android-apps: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
