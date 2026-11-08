#!/usr/bin/env python3
"""Staged OpenWave LXC domain migration for the local OpenWave source tree.

Commands:
  python3 openwave_lxc_domain_migration.py stage [--root PATH]
  python3 openwave_lxc_domain_migration.py activate [--root PATH]
  python3 openwave_lxc_domain_migration.py status [--root PATH]

This tool intentionally does NOT compile policy, regenerate rootfs labels,
install a kernel, change enforcement, or start containers.
"""

from __future__ import annotations
import argparse
from pathlib import Path
import re
import shutil
import sys
from datetime import datetime

POLICY_REL = Path('userspace/security/selinux/openwave.cil')
CONTEXTS_REL = Path('userspace/security/selinux/file_contexts')
STAGE_BEGIN = ';; OPENWAVE_LXC_DOMAIN_STAGE_BEGIN'
STAGE_END = ';; OPENWAVE_LXC_DOMAIN_STAGE_END'
ACTIVE_BEGIN = ';; OPENWAVE_LXC_DOMAIN_ACTIVATE_BEGIN'
ACTIVE_END = ';; OPENWAVE_LXC_DOMAIN_ACTIVATE_END'

STAGE_CIL = r'''(type openwave_lxc_t)
(type openwave_lxc_start_exec_t)
(roletype system_r openwave_lxc_t)

;; Authorization for LXC process transition (enabled in activation step).
(allow openwave_waydroid_manager_t openwave_lxc_start_exec_t
       (file (read open getattr execute)))
(allow openwave_waydroid_manager_t openwave_lxc_t (process (transition)))
(allow openwave_lxc_t openwave_lxc_start_exec_t
       (file (entrypoint read open getattr execute map)))

;; LXC server's abstract Unix command socket.
(allow openwave_lxc_t self
       (unix_stream_socket (create bind listen accept read write connectto)))
(allow openwave_waydroid_manager_t openwave_lxc_t
       (unix_stream_socket (connectto)))
;; Existing OpenWave QA/system lifecycle clients need the same audited access.
(allow openwave_system_t openwave_lxc_t
       (unix_stream_socket (connectto)))

;; LXC metadata and runtime area (not a general /run authorization).
(allow openwave_lxc_t openwave_lxc_run_t
       (dir (search read open getattr write add_name remove_name create)))
(allow openwave_lxc_t openwave_lxc_run_t
       (file (read open getattr write create unlink lock setattr)))

;; Waydroid's LXC state/configuration area (audit further rights before release).
(allow openwave_lxc_t openwave_waydroid_state_t
       (dir (search read open getattr write add_name remove_name)))
(allow openwave_lxc_t openwave_waydroid_state_t
       (file (read open getattr write create setattr lock unlink)))
(allow openwave_lxc_t openwave_waydroid_state_t
       (sock_file (create read write getattr unlink)))

;; LXC's known namespace and cgroup discovery reads.
(allow openwave_lxc_t openwave_namespace_t (file (getattr read open)))
(allow openwave_lxc_t openwave_cgroup_t (filesystem (getattr)))
(allow openwave_lxc_t openwave_cgroup_t (dir (search read open getattr)))

;; Diagnostic only: /proc/<pid>/cgroup commonly carries process domain type.
;; Narrow these after runtime inspection instead of making them production defaults.
(allow openwave_lxc_t openwave_system_t (dir (search)))
(allow openwave_lxc_t openwave_system_t (file (read open getattr)))
'''

ACTIVE_CIL = ''';; Transition only lxc-start invoked from Waydroid manager.
;; lxc-info, lxc-stop, etc retain their existing execution behavior.
(typetransition openwave_waydroid_manager_t openwave_lxc_start_exec_t
                process openwave_lxc_t)
'''

def block(begin: str, end: str, content: str) -> str:
    return begin + '\n' + content.strip() + '\n' + end + '\n'

def backup(p: Path) -> None:
    date = datetime.now().strftime('%Y%m%d-%H%M%S')
    q = p.with_name(p.name + f'.lxc-migration-{date}.bak')
    n = 0
    while q.exists():
        n += 1
        q = p.with_name(p.name + f'.lxc-migration-{date}-{n}.bak')
    shutil.copy2(p, q)
    print(f'BACKUP: {q}')

def insert_block(p: Path, begin: str, end: str, contents: str) -> bool:
    old = p.read_text()
    if begin in old or end in old:
        if (begin in old) and (end in old):
            print(f'ALREADY INSTALLED: {begin}')
            return False
        raise RuntimeError('Partial block already present, refusing to modify: ' + str(p))
    backup(p)
    p.write_text(old.rstrip() + '\n\n' + block(begin, end, contents))
    print(f'UPDATED: {p}')
    return True

def stage(root: Path) -> None:
    policy = root / POLICY_REL
    contexts = root / CONTEXTS_REL
    for p in (policy, contexts):
        if not p.is_file():
            raise RuntimeError(f'Missing file: {p}')
    exe = root / 'rootfs/usr/bin/lxc-start'
    if not exe.exists():
        raise RuntimeError(f'Cannot verify installed LXC binary: {exe}')
    if exe.is_symlink():
        raise RuntimeError(f'lxc-start is a symlink ({exe}); inspect resolved executable and labeling before migration')
    if exe.stat().st_nlink > 1:
        raise RuntimeError(f'lxc-start has {exe.stat().st_nlink} hardlinks; separate it from other tool inodes before assigning a distinct label')
    data = policy.read_text()
    for required in ('openwave_waydroid_manager_t', 'openwave_lxc_run_t', 'openwave_waydroid_state_t', 'openwave_namespace_t', 'openwave_cgroup_t', 'openwave_system_t'):
        if not re.search(r'\b' + required + r'\b', data):
            raise RuntimeError(f'Missing existing policy identifier {required}; inspect local CIL before migration')
    for new_type in ('openwave_lxc_t', 'openwave_lxc_start_exec_t'):
        if re.search(r'\(type\s+' + new_type + r'\s*\)', data) and STAGE_BEGIN not in data:
            raise RuntimeError(f'{new_type} already declared outside managed block; review before continuing')
    fc = contexts.read_text()
    exact = re.compile(r'(?m)^\s*(/usr/bin/lxc(?:\\-|-|-)start)\s+\S+\s*$')
    matching = list(exact.finditer(fc))
    if len(matching) != 1:
        raise RuntimeError('Expected one exact /usr/bin/lxc-start file_contexts entry, found ' + str(len(matching)) + '. Inspect file_contexts and change it manually.')
    matched = matching[0]
    prior = matched.group(0)
    if 'openwave_lxc_start_exec_t' not in prior:
        if 'openwave_lxc_exec_t' not in prior:
            raise RuntimeError('Unexpected previous /usr/bin/lxc-start label: ' + prior)
        backup(contexts)
        fc = fc[:matched.start()] + prior.replace('openwave_lxc_exec_t', 'openwave_lxc_start_exec_t') + fc[matched.end():]
        contexts.write_text(fc)
        print('RELABELED: /usr/bin/lxc-start only')
    if STAGE_BEGIN not in data:
        insert_block(policy, STAGE_BEGIN, STAGE_END, STAGE_CIL)
    print('Stage ready. Compile with ./build-openwave selinux; do NOT boot this intermediate policy.')

def activate(root: Path) -> None:
    policy = root / POLICY_REL
    contexts = root / CONTEXTS_REL
    s = policy.read_text()
    if STAGE_BEGIN not in s or STAGE_END not in s or 'openwave_lxc_start_exec_t' not in contexts.read_text():
        raise RuntimeError('Run stage first, and successfully compile policy before activating')
    insert_block(policy, ACTIVE_BEGIN, ACTIVE_END, ACTIVE_CIL)
    print('Activation ready. Compile, rebuild the guest image, verify actual contexts and AVCs.')

def status(root: Path) -> None:
    policy = (root / POLICY_REL).read_text()
    contexts = (root / CONTEXTS_REL).read_text()
    print('staged:', STAGE_BEGIN in policy and STAGE_END in policy)
    print('activated:', ACTIVE_BEGIN in policy and ACTIVE_END in policy)
    for line in contexts.splitlines():
        if 'lxc-start' in line or 'lxc\\-start' in line:
            print('file_context:', line)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('stage', 'activate', 'status'))
    parser.add_argument('--root', type=Path, default=Path('/mnt/aosp/ThunderOS'))
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.command == 'stage': stage(root)
        elif args.command == 'activate': activate(root)
        else: status(root)
    except (OSError, RuntimeError) as e:
        print('ERROR:', e, file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
