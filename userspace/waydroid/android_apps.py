#!/usr/bin/python3.14 -I
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
CLOSE_APPS_ACTION = 'com.openwave.tinh.action.CLOSE_APPS'
REMOVE_APP_ACTION = 'com.openwave.tinh.action.REMOVE_APP'
SETTINGS_SECURE = 1
SETTINGS_GLOBAL = 2

CONFIG = '/var/lib/waydroid/waydroid.cfg'
ICONS = '/var/lib/waydroid-user/data/icons'
PACKAGE = re.compile(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+')
MAX_PACKAGE = 128 - len('org.openwave.android.')
MAX_NAME = 64
MAX_APPS = 512
MAX_ICON_BYTES = 256 * 1024
ICON_WAIT_SECONDS = 1.5
ICON_WAIT_STEP = 0.1
MAX_ICON_SIDE = 512
LAUNCHER = 'android.intent.category.LAUNCHER'
HIDDEN = frozenset({'com.android.launcher3', 'org.lineageos.setupwizard',
                    'org.lineageos.waydroidupdater', 'com.openwave.tinh'})
TIME_LIMIT = 20
USER_MONITOR = 'waydroidusermonitor'
USER_MONITOR_INTERFACES = ['id.waydro.waydroid.IUserMonitor', 'lineageos.waydroid.IUserMonitor']
TRANSACTION_USER_UNLOCKED = 1
TRANSACTION_PACKAGE_STATE_CHANGED = 2
PACKAGE_MODES = {0: 'added', 1: 'removed', 2: 'updated'}
IDLE_SETTING = 'openwave_android_idle'
IDLE_POLL_MS = 1000
TASKS_SETTING = 'openwave_android_tasks'
TASKS_SEQ_SETTING = 'openwave_android_tasks_seq'
MAX_TASKS_TEXT = 4096
LOOP_TICK_SECONDS = 0.1
HANDLERS_SETTING = 'openwave_android_handlers'
HANDLERS_SEQ_SETTING = 'openwave_android_handlers_seq'
REMOVABLE_SETTING = 'openwave_android_removable'
REMOVE_SETTING = 'openwave_android_remove'
REMOVE_SUCCEEDED = 0
REMOVE_ABORTED = 3
EXIT_CANCELLED = 4
MAX_HANDLERS_TEXT = 65536
HANDLER_TOKENS = frozenset({'http', 'https', 'mailto', 'tel', 'sms'})
VIEW_ACTION = 'com.openwave.tinh.action.VIEW_IN'
VIEW_SCHEME = 'openwave-view'
MAX_LINK = 2048
REMOVE_WAIT_SECONDS = 110
REMOVE_LIMIT = 125
REMOVE_SETTLE_SECONDS = 4
REMOVE_POLL_SECONDS = 0.5
LINK = re.compile(r'[A-Za-z][A-Za-z0-9+.-]{0,31}:[\x21-\x7e]{1,2040}')


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
    if len(data) < 33 or data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
        return None
    width, height = struct.unpack('>II', data[16:24])
    return (width, height) if width and height else None


def read_icon(package, directory=ICONS):
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


def patient_icon_reader(wait=ICON_WAIT_SECONDS, step=ICON_WAIT_STEP, reader=read_icon,
                        sleep=time.sleep, clock=time.monotonic):
    deadline = clock() + wait

    def read(package):
        while True:
            data = reader(package)
            if data or clock() >= deadline:
                return data
            sleep(step)
    return read


def parse_handlers(text):
    result = {}
    if not isinstance(text, str) or len(text) > MAX_HANDLERS_TEXT:
        return result
    for entry in text.split(';')[:MAX_APPS]:
        package, _, tokens = entry.partition('=')
        if not valid_package(package) or package in HIDDEN:
            continue
        kept = sorted({token for token in tokens.split(',') if token in HANDLER_TOKENS})
        if kept:
            result[package] = kept
    return result


def parse_removable(text):
    if not isinstance(text, str) or len(text) > MAX_HANDLERS_TEXT:
        return set()
    return {name for name in text.split(',')[:MAX_APPS * 4] if valid_package(name) and name not in HIDDEN}


def parse_remove_report(text):
    if not isinstance(text, str) or len(text) > 200:
        return None
    parts = text.split(',')
    if len(parts) != 3 or not valid_package(parts[0]):
        return None
    try:
        return parts[0], int(parts[1]), int(parts[2])
    except ValueError:
        return None


def remove_package(platform, package, sleep=time.sleep, clock=time.monotonic):
    try:
        baseline = platform.remove_report()
        code = platform.remove(package)
    except Failure as error:
        return {'removed': False, 'detail': str(error)[:120]}
    deadline = clock() + REMOVE_WAIT_SECONDS
    settle = None
    while True:
        if package not in {app.get('packageName') for app in platform.apps()}:
            return {'removed': True, 'code': code}
        try:
            current = platform.remove_report()
        except Failure:
            current = baseline
        report = parse_remove_report(current) if current != baseline else None
        if report and report[0] == package:
            if report[1] == REMOVE_ABORTED:
                return {'removed': False, 'cancelled': True}
            if report[1] != REMOVE_SUCCEEDED:
                return {'removed': False, 'code': report[1], 'detail': 'Android refused to remove the application'}
            if settle is None:
                settle = clock() + REMOVE_SETTLE_SECONDS
        if settle is not None and clock() >= settle:
            return {'removed': False, 'code': code, 'detail': 'the application is still installed'}
        if clock() >= deadline:
            return {'removed': False, 'code': code, 'detail': 'Android did not answer in time'}
        sleep(REMOVE_POLL_SECONDS)


def remove_timed_out(signum, frame):
    raise Failure('the removal helper ran out of time')


def remove_crash_detail(error):
    import traceback
    frames = traceback.extract_tb(error.__traceback__)[-2:]
    where = ' <- '.join(f'{Path(frame.filename).name}:{frame.lineno}:{frame.name}' for frame in reversed(frames))
    return f'{type(error).__name__}: {error} at {where}'[:120]


def link_token(url):
    if not isinstance(url, str) or len(url) > MAX_LINK or not LINK.fullmatch(url):
        return None
    scheme = url.split(':', 1)[0].lower()
    if scheme == 'smsto':
        scheme = 'sms'
    return scheme if scheme in HANDLER_TOKENS else None


def view_target(package, url):
    from urllib.parse import quote
    return f'{VIEW_SCHEME}://{package}/?u={quote(url, safe="")}'


def catalogue(apps, icon_reader=read_icon, handlers=None, removable=None):
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
        if handlers and handlers.get(package):
            entries[package]['schemes'] = list(handlers[package])
        if removable and package in removable:
            entries[package]['removable'] = True
    return [entries[key] for key in sorted(entries)]


def monitor_event(code, mode=None, package=None):
    if code == TRANSACTION_USER_UNLOCKED:
        return {'event': 'unlocked'}
    if code == TRANSACTION_PACKAGE_STATE_CHANGED and mode in PACKAGE_MODES and valid_package(package):
        return {'event': 'package', 'change': PACKAGE_MODES[mode], 'package': package}
    return None


def tasks_event(text):
    packages = []
    if isinstance(text, str) and len(text) <= MAX_TASKS_TEXT:
        for name in text.split(','):
            if valid_package(name) and name not in HIDDEN and name not in packages:
                packages.append(name)
    return {'event': 'tasks', 'packages': packages[:MAX_APPS]}


def idle_event(value, reported):
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

    def transact(self, code, *strings, integer=None, trailing_integer=None):
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
        return reply, reader

    def call(self, code, *strings, integer=None, trailing_integer=None):
        return self.transact(code, *strings, integer=integer, trailing_integer=trailing_integer)[1]

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

    def handlers(self):
        _, stamp = self.call(TRANSACTION_SETTINGS_GET_INT, HANDLERS_SEQ_SETTING,
                             integer=SETTINGS_GLOBAL).read_int32()
        if not isinstance(stamp, int) or stamp <= 0:
            return {}
        text = self.call(TRANSACTION_SETTINGS_GET_STRING, HANDLERS_SETTING,
                         integer=SETTINGS_GLOBAL).read_string16()
        return parse_handlers(text)

    def removable(self):
        _, stamp = self.call(TRANSACTION_SETTINGS_GET_INT, HANDLERS_SEQ_SETTING,
                             integer=SETTINGS_GLOBAL).read_int32()
        if not isinstance(stamp, int) or stamp <= 0:
            return set()
        text = self.call(TRANSACTION_SETTINGS_GET_STRING, REMOVABLE_SETTING,
                         integer=SETTINGS_GLOBAL).read_string16()
        return parse_removable(text)

    def remove_report(self):
        reply, reader = self.transact(TRANSACTION_SETTINGS_GET_STRING, REMOVE_SETTING, integer=SETTINGS_GLOBAL)
        ok, length = reader.read_int32()
        if not ok or length < 0:
            return ''
        again = reply.init_reader()
        again.read_int32()
        return again.read_string16()

    def remove(self, package):
        self.call(TRANSACTION_LAUNCH_INTENT, REMOVE_APP_ACTION, 'package:' + package)
        return 0

    def prepare(self):
        self.call(TRANSACTION_SETPROP, 'waydroid.active_apps', 'Waydroid')
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'device_provisioned', integer=SETTINGS_GLOBAL, trailing_integer=1)
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'user_setup_complete', integer=SETTINGS_SECURE, trailing_integer=1)

    def view(self, package, url):
        self.prepare()
        self.call(TRANSACTION_LAUNCH_INTENT, VIEW_ACTION, view_target(package, url))
        self.call(TRANSACTION_SETTINGS_PUT_STRING, 'policy_control', 'immersive.status=*',
                  integer=SETTINGS_GLOBAL)

    def launch(self, package):
        self.call(TRANSACTION_SETPROP, 'waydroid.active_apps', 'Waydroid')
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'device_provisioned', integer=SETTINGS_GLOBAL, trailing_integer=1)
        self.call(TRANSACTION_SETTINGS_PUT_INT, 'user_setup_complete', integer=SETTINGS_SECURE, trailing_integer=1)
        self.call(TRANSACTION_LAUNCH_APP, package)
        self.call(TRANSACTION_SETTINGS_PUT_STRING, 'policy_control', 'immersive.status=*',
                  integer=SETTINGS_GLOBAL)


def watch():
    import gbinder
    from gi.repository import GLib
    path, manager_protocol, binder_protocol = binder_device()
    try:
        manager = gbinder.ServiceManager(path, manager_protocol, binder_protocol)
    except TypeError:
        manager = gbinder.ServiceManager(path)
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
            return reply, -99999
        try:
            if event:
                emit(event)
        except (BrokenPipeError, OSError):
            stop()
        reply.append_int32(0)
        return reply, 0

    local = manager.new_local_object(USER_MONITOR_INTERFACES, handler)

    def presence():
        if not manager.is_present():
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
            _, value = state['platform'].call(TRANSACTION_SETTINGS_GET_INT, IDLE_SETTING,
                                              integer=SETTINGS_GLOBAL).read_int32()
        except Exception as error:
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
            if isinstance(stamp, int) and stamp > 0 and stamp != state.get('tasks'):
                text = state['platform'].call(TRANSACTION_SETTINGS_GET_STRING, TASKS_SETTING,
                                              integer=SETTINGS_GLOBAL).read_string16()
                state['tasks'] = stamp
                emit(tasks_event(text))
        except (BrokenPipeError, OSError):
            stop()
            return
        except Exception:
            state['platform'] = None
            return
        try:
            _, handler_stamp = state['platform'].call(TRANSACTION_SETTINGS_GET_INT, HANDLERS_SEQ_SETTING,
                                                      integer=SETTINGS_GLOBAL).read_int32()
            if isinstance(handler_stamp, int) and handler_stamp > 0 and handler_stamp != state.get('handlers'):
                first = state.get('handlers') is None
                state['handlers'] = handler_stamp
                if not first:
                    emit({'event': 'handlers'})
        except (BrokenPipeError, OSError):
            stop()
            return
        except Exception:
            state['platform'] = None
            return
        if state['reported'] is None:
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
    platform.call(TRANSACTION_LAUNCH_INTENT, CLOSE_APPS_ACTION, '')


def close_one(platform, package):
    platform.call(TRANSACTION_LAUNCH_INTENT, CLOSE_APPS_ACTION, 'package:' + package)


def main(argv):
    if os.getuid() or os.geteuid():
        print('android-apps: requires the appd root identity', file=sys.stderr)
        return 126
    try:
        if argv == ['watch']:
            try:
                return watch()
            except Exception as error:
                import traceback
                where = ' <- '.join(f'{Path(frame.filename).name}:{frame.lineno}:{frame.name}'
                                    for frame in reversed(traceback.extract_tb(error.__traceback__)[-3:]))
                emit({'event': 'error', 'message': f'{type(error).__name__}: {error} at {where}'[:300]})
                return 1
        signal.alarm(TIME_LIMIT)
        if argv == ['list']:
            platform = Platform()
            apps = platform.apps()
            try:
                handlers = platform.handlers()
            except Exception:
                handlers = {}
            try:
                removable = platform.removable()
            except Exception:
                removable = set()
            print(json.dumps({'apps': catalogue(apps, icon_reader=patient_icon_reader(), handlers=handlers,
                                                removable=removable)}, separators=(',', ':')))
            return 0
        if len(argv) == 2 and argv[0] == 'remove' and valid_package(argv[1]) and argv[1] not in HIDDEN:
            signal.signal(signal.SIGALRM, remove_timed_out)
            signal.alarm(REMOVE_LIMIT)
            try:
                platform = Platform()
                if argv[1] not in platform.removable():
                    print(json.dumps({'removed': False, 'detail': 'this application is part of Android'}))
                    return 2
                outcome = remove_package(platform, argv[1])
            except (Failure, OSError) as error:
                print(json.dumps({'removed': False, 'detail': str(error)[:120]}))
                return 1
            except Exception as error:
                print(json.dumps({'removed': False, 'detail': remove_crash_detail(error)}))
                return 1
            print(json.dumps(outcome, separators=(',', ':')))
            if outcome['removed']:
                return 0
            return EXIT_CANCELLED if outcome.get('cancelled') else 1
        if len(argv) == 3 and argv[0] == 'view' and valid_package(argv[1]) and argv[1] not in HIDDEN:
            token = link_token(argv[2])
            platform = Platform()
            handlers = platform.handlers() if token else {}
            if token is None or token not in handlers.get(argv[1], ()):
                print('android-apps: that application does not handle this link', file=sys.stderr)
                return 2
            platform.view(argv[1], argv[2])
            print(json.dumps({'viewed': argv[1]}))
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
        print('android-apps: usage: list | launch PACKAGE | view PACKAGE URL | remove PACKAGE | close PACKAGE | close-all | watch', file=sys.stderr)
        return 2
    except (Failure, OSError) as error:
        print(f'android-apps: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
