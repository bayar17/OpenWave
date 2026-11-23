#!/usr/bin/python3.14 -I
"""appd-only APK deployment. fd 3 is copied before inspection/authorization.

Q/Y/N is the same private pipe protocol as appdeploy-helper. Android's package
manager validates APK signatures and package policy; only a zero install result
is success. No shell, caller-supplied commands, or confirmation bypass exists.
"""
import fcntl
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import re
import secrets
import signal
import stat
import struct
import sys
import zipfile

STAGING = '/var/lib/waydroid-user/data/openwave-install'
MAX_PACKAGE = 1024 * 1024 * 1024
MAX_MANIFEST = 1024 * 1024
ANDROID_NAMESPACE = 'http://schemas.android.com/apk/res/android'
PACKAGE = re.compile(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+')


def clean(value):
    return ''.join(c for c in value if c.isprintable() and c not in '\t\r\n')[:128]


def manifest_info(data):
    """Read bounded Android binary XML string pools and start-element attributes."""
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    if len(data) < 8 or u16(0) != 3 or u16(2) != 8 or u32(4) != len(data):
        raise ValueError('Invalid Android binary manifest')
    strings, package, version, permissions = [], '', '', []
    def string(i):
        if i == 0xffffffff: return ''
        if i >= len(strings): raise ValueError('Invalid manifest string index')
        return strings[i]
    offset = 8
    while offset < len(data):
        if offset + 8 > len(data): raise ValueError('Truncated manifest chunk')
        kind, header, size = u16(offset), u16(offset + 2), u32(offset + 4)
        end = offset + size
        if header < 8 or size < header or end > len(data): raise ValueError('Invalid manifest chunk')
        if kind == 1:
            if header < 28: raise ValueError('Invalid string pool')
            count, flags, start = u32(offset + 8), u32(offset + 16), u32(offset + 20)
            if count > 65536 or header + count * 4 > size or start >= size: raise ValueError('Invalid string pool size')
            def length(p, utf8):
                if p >= end: raise ValueError('Invalid string length')
                n = data[p] if utf8 else u16(p)
                step, high = (1, 0x80) if utf8 else (2, 0x8000)
                if n & high:
                    if p + 2 * step > end: raise ValueError('Invalid string length')
                    n = ((n & (high - 1)) << (8 if utf8 else 16)) | (data[p + step] if utf8 else u16(p + step))
                    return n, p + 2 * step
                return n, p + step
            strings = []
            for i in range(count):
                p = offset + start + u32(offset + header + i * 4)
                utf8 = bool(flags & 0x100)
                n, p = length(p, utf8)
                if utf8: n, p = length(p, True)
                n *= 1 if utf8 else 2
                if p + n > end: raise ValueError('Manifest string exceeds pool')
                strings.append(data[p:p+n].decode('utf-8' if utf8 else 'utf-16-le'))
        elif kind == 0x102:
            if header < 16 or header + 20 > size: raise ValueError('Invalid manifest element')
            ext = offset + header
            name = string(u32(ext + 4))
            start, stride, count = u16(ext + 8), u16(ext + 10), u16(ext + 12)
            if start < 20 or stride < 20 or ext + start + stride * count > end: raise ValueError('Invalid attributes')
            attrs = {}
            for i in range(count):
                p = ext + start + stride * i
                key, raw = (string(u32(p)), string(u32(p + 4))), u32(p + 8)
                if key in attrs: raise ValueError('Duplicate manifest attribute')
                attrs[key] = string(raw) if raw != 0xffffffff else string(u32(p + 16)) if data[p + 15] == 3 else str(u32(p + 16))
            if name == 'manifest':
                if package: raise ValueError('Multiple manifest elements')
                package = attrs.get(('', 'package'), '')
                version = attrs.get((ANDROID_NAMESPACE, 'versionName'), attrs.get((ANDROID_NAMESPACE, 'versionCode'), ''))
            elif name.startswith('uses-permission') and len(permissions) < 64:
                permissions.append(attrs.get((ANDROID_NAMESPACE, 'name'), ''))
        offset = end
    if len(package) > 255 or not PACKAGE.fullmatch(package): raise ValueError('Invalid Android package identifier')
    return package, clean(version), [clean(p) for p in permissions if p]


def inspect_apk(stream):
    with zipfile.ZipFile(stream) as archive:
        entries = archive.infolist()
        if len(entries) > 100000 or len({e.filename for e in entries}) != len(entries):
            raise ValueError('Too many or duplicate APK entries')
        if sum(e.file_size for e in entries) > 2 * MAX_PACKAGE or any(e.flag_bits & 1 for e in entries):
            raise ValueError('APK is too large or encrypted')
        manifest = archive.getinfo('AndroidManifest.xml')
        if not 8 <= manifest.file_size <= MAX_MANIFEST: raise ValueError('Invalid Android manifest size')
        return manifest_info(archive.read(manifest))


def platform():
    path = '/usr/libexec/openwave-waydroid/android-apps'
    spec = importlib.util.spec_from_loader('openwave_android_apps', importlib.machinery.SourceFileLoader('openwave_android_apps', path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.Platform()


def authorize_and_install(stream, android_path, name, answer, emit, connect=platform):
    package, version, permissions = inspect_apk(stream)
    emit('\t'.join(['Q', 'install', clean(name), version, '', package, 'android', 'untrusted',
                    'APK from a local file; Android verifies its signature', ','.join(permissions[:12])[:1000]]))
    if answer() != 'Y\n': raise ValueError('Installation was not authorized')
    service = connect()
    # Same fixed IPlatform transaction used by the pinned Waydroid install CLI.
    _, result = service.call(5, android_path).read_int32()
    if result != 0: raise ValueError(f'Android package manager rejected installation ({result})')
    emit('R\tInstalled ' + package)


def main():
    if os.geteuid() != 0 or len(sys.argv) != 3 or sys.argv[1] != 'install-file':
        raise ValueError('Only appd may invoke APK installation')
    def interrupted(signum, frame):
        raise TimeoutError('Android installation was interrupted or timed out')
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGALRM, interrupted)
    signal.alarm(240)
    st = os.fstat(3)
    if not stat.S_ISREG(st.st_mode) or not 0 < st.st_size <= MAX_PACKAGE or fcntl.fcntl(3, fcntl.F_GETFL) & os.O_ACCMODE != os.O_RDONLY:
        raise ValueError('A bounded read-only regular APK descriptor is required')
    # The separately labelled directory is provisioned by the image assembler.
    # Walk all parents without following links, and pin the final directory fd.
    directory = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for component in Path(STAGING).parts[1:]:
            child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
            os.close(directory); directory = child
        info = os.fstat(directory)
        if info.st_uid != 0 or info.st_mode & 0o022: raise ValueError('Unsafe APK staging directory')
        name = secrets.token_hex(16) + '.apk'
        fd = os.open(name, os.O_CREAT | os.O_EXCL | os.O_RDWR | os.O_NOFOLLOW, 0o644, dir_fd=directory)
        try:
            with os.fdopen(fd, 'w+b') as snapshot:
                total, offset = 0, 0
                while True:
                    chunk = os.pread(3, 1024 * 1024, offset)
                    if not chunk: break
                    total += len(chunk); offset += len(chunk)
                    if total > MAX_PACKAGE: raise ValueError('APK exceeds size limit')
                    snapshot.write(chunk)
                snapshot.flush(); os.fsync(snapshot.fileno()); os.fchmod(snapshot.fileno(), 0o644)
                snapshot.seek(0)
                authorize_and_install(snapshot, '/data/openwave-install/' + name, sys.argv[2],
                                      sys.stdin.readline, lambda line: print(line, flush=True))
        finally:
            os.unlink(name, dir_fd=directory)
    finally:
        os.close(directory)


if __name__ == '__main__':
    try: main()
    except Exception as error:
        print('E\t' + clean(str(error)), flush=True)
        sys.exit(1)
