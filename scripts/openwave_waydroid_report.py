"""Evaluate bounded guest probes without equating CLI success with Android boot."""
import re


def evaluate(serial):
    probes = {}
    for match in re.finditer(
            r'WAYDROID_PROBE_BEGIN (\w+)\r?\n(.*?)\r?\nWAYDROID_PROBE_END \1 exit=(\d+)',
            serial, re.S):
        name, output, status = match.groups()
        probes[name] = {'exit_code': int(status), 'output': output.strip()}

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
    # A live surface, identity binding and input require a separate Ocean test.
    stages['graphics'] = 'NOT TESTED'
    stages['back_navigation'] = 'NOT TESTED'
    complete = ('WAYDROID_QA END enforcing=1' in serial
                and 'WAYDROID_QA BEGIN enforcing=1' in serial)
    framework = complete and all(stages[n] == 'PASS' for n in (
        'container', 'servicemanager', 'zygote', 'system_server', 'boot_completed', 'package_manager'))
    return {'diagnostics_complete': complete, 'framework_passed': framework,
            'integration_passed': False, 'stages': stages, 'probes': probes,
            'lxc_failed_library': loader.group(1) if loader else None,
            'lxc_denied_config_path': denied_config.group(1) if denied_config else None},
            'lxc_search_denial_types': search_denials
