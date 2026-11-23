"""Verify the graphics runtime against the successful target Mesa build record."""
import hashlib
import json
from pathlib import Path

MESA_RECORD = Path('etc/openwave/mesa-build.json')
LIBRARIES = {f'usr/lib64/{name}' for name in ('libEGL.so.1', 'libGLESv2.so.2', 'libgbm.so.1')}


def verify_mesa_runtime(root, record_path=None):
    root = Path(root).resolve(strict=True)
    record_path = Path(record_path) if record_path is not None else root / MESA_RECORD
    try:
        record = json.loads(record_path.read_text())
    except (OSError, ValueError) as error:
        raise RuntimeError('Missing or invalid target Mesa build record; build Mesa and assemble rootfs before packing') from error
    if 'wayland' not in record.get('platforms', []) or 'virgl' not in record.get('drivers', []) \
            or record.get('bind_wayland_display') is not True:
        raise RuntimeError('Mesa runtime lacks required Wayland, VirGL or compositor display binding support')
    if set(record.get('libraries', {})) != LIBRARIES:
        raise RuntimeError('Mesa build record is missing a required runtime library')
    for relative, expected in record['libraries'].items():
        library = (root / relative).resolve(strict=True)
        if not library.is_relative_to(root):
            raise RuntimeError(f'Mesa library escapes the runtime tree: {relative}')
        if hashlib.sha256(library.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Mesa runtime differs from its verified target install: {relative}')
    return record
