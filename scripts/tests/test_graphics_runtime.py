from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from openwave_graphics_runtime import LIBRARIES, MESA_RECORD, verify_mesa_runtime

class GraphicsRuntimeTests(unittest.TestCase):
    def test_complete_target_install_matches_and_rejects_stale_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            record=dict(platforms=['wayland'],drivers=['virgl','softpipe'],bind_wayland_display=True,libraries={})
            for relative in LIBRARIES:
                path=root/relative; path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(relative.encode());record['libraries'][relative]=hashlib.sha256(path.read_bytes()).hexdigest()
            marker=root/MESA_RECORD;marker.parent.mkdir(parents=True)
            marker.write_text(json.dumps(record))
            self.assertEqual(verify_mesa_runtime(root),record)
            for key,value in [('platforms',[]),('drivers',['softpipe']),('bind_wayland_display',False),('libraries',{})]:
                with self.subTest(key=key):
                    bad=record|{key:value};marker.write_text(json.dumps(bad))
                    with self.assertRaises(RuntimeError):verify_mesa_runtime(root)
            marker.write_text(json.dumps(record))
            path=root/sorted(LIBRARIES)[0];path.write_bytes(b'stale install')
            with self.assertRaisesRegex(RuntimeError,'differs'):verify_mesa_runtime(root)
            path.unlink();path.symlink_to('/bin/sh')
            with self.assertRaisesRegex(RuntimeError,'escapes'):verify_mesa_runtime(root)
            marker.unlink()
            with self.assertRaisesRegex(RuntimeError,'Missing'):verify_mesa_runtime(root)

if __name__=='__main__':unittest.main()
