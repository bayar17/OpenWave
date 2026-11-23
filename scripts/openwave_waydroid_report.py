"""Evaluate bounded guest probes without equating CLI success with Android boot."""
import re


# With printk.time=1 every kernel-console record (audit, binder, and Android
# init's /dev/kmsg output) starts with "[ seconds.micros] ". Those records share
# the serial line with guest probes and may begin mid-line. Remove them so an
# exact probe value such as "RUNNING" is not misread as a failure.
KERNEL_RECORD = re.compile(r'\[\s*\d+\.\d{6}\] [^\r\n]*')


_SHOT_BEGIN = re.compile(r'WAYDROID_QA_SHOT_BEGIN name=(\S+) OCEAN_QA_SHOT_BEGIN seq=(\d+) '
                         r'size=(\d+)x(\d+) bytes=(\d+) chunks=(\d+)')
_SHOT_DATA = re.compile(r'WAYDROID_QA_SHOT_DATA (\d+) ([0-9a-f]{8}) ([A-Za-z0-9+/=]+)$')
_SHOT_END = re.compile(r'WAYDROID_QA_SHOT_END name=(\S+)')


def extract_screenshots(serial):
    """Decode Ocean's QA screen captures from the serial log.

    Each chunk carries its index and an MD5 prefix. An image is returned only
    if every chunk is present and intact and the decoded size matches; a
    capture damaged by interleaved console output is reported, never guessed.
    """
    import base64
    import hashlib
    shots, current = [], None
    for line in serial.splitlines():
        line = line.rstrip('\r')
        begin = _SHOT_BEGIN.search(line)
        if begin:
            name, _, width, height, size, chunks = begin.groups()
            current = {'name': name, 'width': int(width), 'height': int(height),
                       'bytes': int(size), 'chunks': int(chunks), 'data': {}, 'bad': 0}
            continue
        if current is None:
            continue
        data = _SHOT_DATA.search(line)
        if data:
            index, digest, chunk = int(data.group(1)), data.group(2), data.group(3)
            if hashlib.md5(chunk.encode()).hexdigest()[:8] == digest:
                current['data'][index] = chunk
            else:
                current['bad'] += 1
            continue
        end = _SHOT_END.search(line)
        if end and end.group(1) == current['name']:
            missing = [i for i in range(current['chunks']) if i not in current['data']]
            jpeg = b''
            if not missing:
                jpeg = base64.b64decode(''.join(current['data'][i] for i in range(current['chunks'])))
            ok = not missing and len(jpeg) == current['bytes'] and jpeg[:2] == b'\xff\xd8'
            shots.append({'name': current['name'], 'ok': ok, 'width': current['width'],
                          'height': current['height'], 'bytes': len(jpeg),
                          'missing_chunks': len(missing), 'damaged_chunks': current['bad'],
                          'jpeg': jpeg if ok else None})
            current = None
    return shots


def probe_text(output):
    lines = (KERNEL_RECORD.sub('', line).rstrip() for line in output.splitlines())
    return '\n'.join(line for line in lines if line).strip()


def evaluate(serial):
    probes = {}
    for match in re.finditer(
            r'WAYDROID_PROBE_BEGIN (\w+)(?:\[\s*\d+\.\d{6}\] [^\r\n]*)?\r?\n(.*?)\r?\nWAYDROID_PROBE_END \1 exit=(\d+)',
            serial, re.S):
        name, output, status = match.groups()
        probes[name] = {'exit_code': int(status), 'output': probe_text(output)}

    def exact(name, value):
        p = probes.get(name, {})
        return p.get('exit_code') == 0 and p.get('output') == value

    container = exact('container', 'RUNNING')
    stages = {'container': 'PASS' if container else 'FAIL'}
    manager = probes.get('startup', {})
    # OpenRC starting the manager is not equivalent to the manager's D-Bus
    # Start(session) call succeeding or LXC entering RUNNING.
    stages['manager'] = 'PASS' if (manager.get('exit_code') == 0
        and 'could not allocate closure' not in serial) else 'FAIL'
    request = probes.get('request_session')
    stages['session_start'] = (
        ('PASS' if request.get('exit_code') == 0 else 'FAIL')
        if request else 'NOT TESTED')
    # The manager's D-Bus exception alone hides the underlying early LXC
    # linker failure. Surface exact library name from the guest's existing
    # Waydroid log, but never infer "PASS" from an absent error line.
    loader = re.search(
        r'lxc-start: error while loading shared libraries: '
        r'([A-Za-z0-9_+.\-]+): cannot open shared object file: Permission denied',
        serial)
    stages['lxc_loader'] = 'FAIL' if loader else 'UNVERIFIED'
    # Record the next real LXC failure separately from the dynamic loader:
    # this message originates from lxc_start_main after libraries loaded.
    # No error alone is not sufficient; only a RUNNING container is PASS.
    denied_config = re.search(
        r'lxc-start: waydroid: [^\r\n]*?You lack access to '
        r'(/var/lib/waydroid/lxc)(?=\s|$)',
        serial)
    stages['lxc_config_access'] = (
        'FAIL' if denied_config else ('PASS' if container else 'UNVERIFIED'))
    # Report the exact types still preventing LXC directory traversal.
    # This is diagnostic only: an empty set does NOT demonstrate that
    # the container can actually open its configuration or start Android.
    search_denials = sorted(set(re.findall(
        r'avc:\s+denied\s+\{\s*search\s*\}[^\n]*'
        r'comm="lxc-start"[^\n]*'
        r'scontext=\S*:openwave_lxc_t:s0[^\n]*'
        r'tcontext=\S*:(openwave_[A-Za-z0-9_]+):s0\s+tclass=dir',
        serial)))
    # Exhaustive typed diagnostics for LXC AVCs, not only { search }.
    # In Stage 2N the search list was empty while create/getattr/read
    # denials still blocked lxc-start. Do not imply any missing denial
    # is proof of success; the container and framework probes remain
    # authoritative.
    lxc_denials = []
    avc_pattern = (
        r'avc:\s+denied\s+\{\s*([^}]*?)\s*\}'
        r'[^\n]*?comm="lxc-start"[^\n]*?'
        r'scontext=\S*:openwave_lxc_t:s0[^\n]*?'
        r'tcontext=\S*:([a-zA-Z_0-9]+):s0\s+tclass=([a-z_]+)'
    )
    for match in re.finditer(avc_pattern, serial):
        permissions, target_type, target_class = match.groups()
        record = {
            'permissions': sorted(set(permissions.split())),
            'target_type': target_type,
            'target_class': target_class,
        }
        if record not in lxc_denials:
            lxc_denials.append(record)
    lxc_denials.sort(key=lambda item: (
        item['target_type'], item['target_class'], item['permissions']))
    for name, value in [('servicemanager', 'running'), ('zygote', 'running'),
                        ('boot_completed', '1')]:
        stages[name] = ('PASS' if exact(name, value) else 'FAIL') if container else 'BLOCKED'
    packages = probes.get('packages', {})
    package_ready = packages.get('exit_code') == 0 and bool(re.search(
        r'^package:[a-zA-Z][a-zA-Z0-9_]*(?:\.[a-zA-Z0-9_]+)+$',
        packages.get('output', ''), re.M))
    stages['package_manager'] = ('PASS' if package_ready else 'FAIL') if container else 'BLOCKED'
    server = probes.get('system_server', {})
    server_ready = server.get('exit_code') == 0 and bool(re.fullmatch(
        r'[1-9][0-9]*(?:\s+[1-9][0-9]*)*', server.get('output', '')))
    stages['system_server'] = ('PASS' if server_ready else 'FAIL') if container else 'BLOCKED'
    # Host device isolation is reported separately from framework readiness.
    isolation = probes.get('device_isolation', {})
    isolated = isolation.get('exit_code') == 0 and bool(re.search(
        r'^(host-disk-blocked \d+:\d+|mknod-refused)$', isolation.get('output', ''), re.M)) \
        and 'HOST-DISK-READABLE' not in isolation.get('output', '')
    stages['device_isolation'] = ('PASS' if isolated else 'FAIL') if container else 'BLOCKED'
    # A live surface, identity binding and input require a separate Ocean test.
    stages['graphics'] = 'NOT TESTED'
    stages['back_navigation'] = 'NOT TESTED'
    complete = ('WAYDROID_QA END enforcing=1' in serial
                and 'WAYDROID_QA BEGIN enforcing=1' in serial)
    framework = complete and all(stages[n] == 'PASS' for n in (
        'container', 'servicemanager', 'zygote', 'system_server', 'boot_completed', 'package_manager'))
    # Pre-logd evidence from Android init via the QA-only /dev/kmsg bind.
    # Informational: these never turn a stage into PASS.
    reboot = re.search(r'init: Reboot start, reason: ([^\s,]+(?:,[^\s,]+)*)', serial)
    exec_errors = sorted(set(re.findall(
        r"init: cannot execv\('([^']+)'\)[^\r\n]*?: ([A-Za-z ]+error|[A-Za-z ]+denied|No such file or directory)",
        serial)))
    android_init = {
        'kmsg_visible': bool(re.search(r'\binit: (?:Parsing file|processing action|starting service)', serial)),
        'reboot_reason': reboot.group(1) if reboot else None,
        'exec_failures': [{'path': path, 'error': error} for path, error in exec_errors],
        'reboot_on_failure_services': sorted(set(re.findall(
            r"init: Service (\S+) has 'reboot_on_failure' option and failed", serial))),
    }
    return {'diagnostics_complete': complete, 'framework_passed': framework,
            'android_init': android_init,
            'integration_passed': False, 'stages': stages, 'probes': probes,
            'lxc_failed_library': loader.group(1) if loader else None,
            'lxc_denied_config_path': denied_config.group(1) if denied_config else None,
            'lxc_search_denial_types': search_denials,
            'lxc_avc_denials': lxc_denials}
