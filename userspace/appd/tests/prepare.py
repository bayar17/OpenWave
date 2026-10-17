"""Create the bitmap/manifest fixture used only by the appd test image."""
import json
from pathlib import Path
import random
import struct
import sys
import zlib

runtime, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
rng = random.Random(0)
pixels = b''.join(b'\0' + rng.randbytes(256 * 3) for _ in range(256))


def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data
            + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))


image = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 256, 8, 2, 0, 0, 0))
         + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))
assert len(image) > 65536
(output / 'settings-icon.png').write_bytes(image)
manifest = json.loads((runtime / 'System/Applications/com.openwave.settings/manifest.json').read_text())
manifest['icon'] = 'resources/icon.png'
(output / 'settings-manifest.json').write_text(json.dumps(manifest) + '\n')
