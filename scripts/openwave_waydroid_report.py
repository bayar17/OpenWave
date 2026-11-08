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
            'integration_passed': False, 'stages': stages, 'probes': probes}
