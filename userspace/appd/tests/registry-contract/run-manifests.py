#!/usr/bin/env python3
"""Run actual manifest validation in a disposable user/mount namespace."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SYSROOT = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'
BINARY = ROOT / 'out/boot-recovery/registry-contract/registry-manifests'
LOADER = SYSROOT / 'lib64/ld-linux-x86-64.so.2'
LIBS = ':'.join(str(SYSROOT / part) for part in ('usr/lib64', 'usr/lib', 'lib64'))
with tempfile.TemporaryDirectory(prefix='openwave-manifest-contract-') as temporary:
    fixture = Path(temporary)
    (fixture / 'lib64').mkdir()
    shutil.copy2(LOADER, fixture / 'lib64/ld-linux-x86-64.so.2')
    trace = subprocess.check_output([str(LOADER), '--library-path', LIBS, '--list', str(BINARY)], text=True)
    for match in re.finditer(r'=> (/\S+) \(', trace):
        library = Path(match.group(1))
        assert library.is_relative_to(SYSROOT), library
        shutil.copy2(library, fixture / 'lib64' / library.name)
    shutil.copy2(BINARY, fixture / 'registry-manifests')
    def bundle(base, identity, **changes):
        directory = fixture / base / identity
        (directory / 'bin').mkdir(parents=True)
        executable = directory / 'bin/app'
        executable.write_bytes(b'fixture')
        executable.chmod(0o755)
        metadata = dict(schemaVersion=1, id=identity, name='Fixture', version='1',
                        runtime='openwave-qt', executable='bin/app', permissions=[],
                        sandbox=dict(enabled=True, profile='standard'))
        metadata.update(changes)
        (directory / 'manifest.json').write_text(json.dumps(metadata))
    bundle('System/Applications', 'org.example.system')
    bundle('Applications', 'org.example.user')
    for base in ('System/Applications', 'Applications'):
        bundle(base, 'org.example.duplicate')
    bundle('Applications', 'org.example.invalidschema', schemaVersion=2)
    bundle('Applications', 'org.example.unsafe', sandbox=dict(enabled=False, profile='standard'))
    bundle('Applications', 'org.example.traversal', executable='../outside')
    bundle('Applications', 'org.example.component', role='background')
    bundle('Applications', 'com.openwave.impostor')
    bundle('System/Applications', 'com.openwave.shell', role='compositor')
    for path in fixture.rglob('*'):
        path.chmod(0o755 if path.is_dir() else path.stat().st_mode & 0o755)
    subprocess.run(['bwrap', '--unshare-user', '--uid', '0', '--gid', '0',
                    '--unshare-pid', '--die-with-parent', '--bind', str(fixture), '/',
                    '--proc', '/proc', '--dev', '/dev',
                    '/lib64/ld-linux-x86-64.so.2', '--library-path', '/lib64', '/registry-manifests'], check=True)
