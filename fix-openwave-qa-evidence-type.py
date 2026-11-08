#!/usr/bin/env python3
"""Add the missing OpenWave SELinux QA evidence type, without replacing policy."""

import argparse
from datetime import datetime, timezone
from pathlib import Path
import os
import shutil
import stat
import tempfile

DECLARATION = '(type openwave_qa_evidence_t)'
ANCHOR = '(type openwave_cgroup_t)'
TRANSITION = '(typetransition openwave_system_t openwave_runtime_t dir "openwave-logs" openwave_qa_evidence_t)'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, default=Path.cwd(), help='OpenWave repository root')
    p.add_argument('--apply', action='store_true', help='Apply the single-line fix, saving a backup')
    args = p.parse_args()
    repo = args.repo.resolve()
    source = repo / 'userspace/security/selinux/openwave.cil'
    if source.is_symlink() or not source.is_file():
        p.error(f'Expected a regular non-symlink policy source at {source}')
    contents = source.read_text(encoding='utf-8')
    lines = contents.splitlines(keepends=True)
    active = [line.strip() for line in lines if not line.lstrip().startswith(';')]
    if active.count(DECLARATION) == 1:
        print('ALREADY FIXED: QA evidence type is declared. No changes made.')
        return
    if active.count(DECLARATION) > 1:
        p.error('Duplicate QA evidence type declarations found. No changes made.')
    if active.count(ANCHOR) != 1:
        p.error('Could not find a unique cgroup type declaration. No changes made.')
    if active.count(TRANSITION) != 1:
        p.error('Expected QA evidence directory transition is missing or duplicated. No changes made.')

    new_lines = []
    for line in lines:
        new_lines.append(line)
        if line.strip() == ANCHOR:
            linebreak = '\r\n' if line.endswith('\r\n') else '\n'
            new_lines.append(DECLARATION + linebreak)
    updated = ''.join(new_lines)
    if not args.apply:
        print(f'PREVIEW: add one line immediately after {ANCHOR} in {source}')
        print(f'+{DECLARATION}')
        print('No files modified; run again with --apply to save the fix.')
        return

    backup_dir = repo / 'out/backups'
    backup_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup = backup_dir / f'openwave.cil-before-qa-type-{stamp}.bak'
    shutil.copy2(source, backup)

    tmp = None
    try:
        with tempfile.NamedTemporaryFile('w', encoding='utf-8', newline='', dir=source.parent,
                                         prefix='.openwave.cil.qa-', delete=False) as f:
            tmp = Path(f.name)
            f.write(updated)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, stat.S_IMODE(source.stat().st_mode))
        os.replace(tmp, source)
    finally:
        if tmp is not None and tmp.exists():
            tmp.unlink()

    print('FIXED: added exactly one type declaration. Other source lines unchanged.')
    print(f'Policy: {source}')
    print(f'Backup: {backup}')


if __name__ == '__main__':
    main()
