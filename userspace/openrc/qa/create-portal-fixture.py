#!/usr/bin/env python3
"""Create the deterministic PNG used by the opt-in Settings portal guest test."""
import struct
import sys
import zlib
from pathlib import Path

width, height = 768, 1366
pixels = b''.join(b'\0' + bytes((32 + y * 120 // height, 90, 150)) * width for y in range(height))
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
image = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
if output.exists():
    if output.read_bytes() != image:
        raise SystemExit('Refusing to replace an existing image')
else:
    with output.open('xb') as stream:
        stream.write(image)
