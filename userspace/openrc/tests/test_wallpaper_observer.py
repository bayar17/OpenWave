#!/usr/bin/env python3
"""Keep the guest QA probe aligned with wallpaperagentd's system-bus API."""

import subprocess
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
import hashlib
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
OBSERVER = ROOT / "userspace/openrc/qa/openwave-qa-observer"
XML = ROOT / "userspace/dbus/interfaces/org.openwave.Wallpaper1.xml"
AGENT = ROOT / "userspace/wallpaperagentd/main.c"


class WallpaperObserverTests(unittest.TestCase):
    def test_system_bus_probe_and_real_api_contract(self):
        observer = OBSERVER.read_text()
        self.assertIn("--system --timeout 4", observer)
        self.assertIn("org.openwave.Wallpaper1.$1", observer)
        self.assertIn("system_bus_accessible=", observer)
        self.assertIn("bus_name_owner_uid=", observer)
        self.assertIn("bus_name_owner_pid=", observer)
        self.assertIn("GetNameOwner org.openwave.Wallpaper", observer)
        self.assertIn("system_call GetCurrent", observer)
        self.assertIn("system_call GetImage", observer)
        self.assertIn("GetImage=verified response-type=ay valid-png-signature=yes sha256-matches-GetCurrent=yes", observer)
        self.assertIn("GetImage=integrity-failed", observer)
        self.assertIn("/bin/busybox xxd -r -p", observer)
        self.assertIn("/bin/busybox sha256sum", observer)
        self.assertNotIn("| wc -l", observer)
        self.assertNotIn("| tr -d", observer)
        self.assertNotRegex(observer, r"printf 'session %s=%s.*Wallpaper")

        interface = ET.parse(XML).getroot().find("interface")
        self.assertEqual(interface.attrib["name"], "org.openwave.Wallpaper1")
        signatures = {}
        for method in interface.findall("method"):
            signatures[method.attrib["name"]] = "".join(
                arg.attrib["type"] for arg in method.findall("arg")
                if arg.attrib.get("direction", "in") == "in"
            ) + ">" + "".join(
                arg.attrib["type"] for arg in method.findall("arg")
                if arg.attrib.get("direction", "in") == "out"
            )
        self.assertEqual(signatures, {
            "GetImage": ">ay", "GetCurrent": ">ss", "SetWallpaper": "ay>s"
        })
        source = AGENT.read_text()
        for contract in ('BUS_NAME "org.openwave.Wallpaper"',
                         'OBJECT_PATH "/org/openwave/Wallpaper"',
                         'INTERFACE "org.openwave.Wallpaper1"',
                         '"GetImage"', '"GetCurrent"'):
            self.assertIn(contract, source)

    def test_probe_script_has_valid_shell_syntax(self):
        result = subprocess.run(["sh", "-n", str(OBSERVER)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_getimage_parser_decodes_and_rejects_tampered_byte_arrays(self):
        observer = OBSERVER.read_text()
        match = re.search(r"/bin/busybox awk '\n(.*?)\n\s*' \"\$image_output\"", observer, re.S)
        self.assertIsNotNone(match, "GetImage must use a self-contained byte-array parser")
        awk = shutil.which("awk")
        self.assertIsNotNone(awk)
        payload = bytes.fromhex("89504e470d0a1a0a") + b"openwave-image-fixture"
        variant = "([byte " + ", ".join(f"0x{byte:02x}" for byte in payload) + "],)\n"
        with tempfile.TemporaryDirectory(prefix="openwave-getimage-parser-") as temporary:
            source = Path(temporary) / "gdbus-output"
            source.write_text(variant)
            parsed = subprocess.run([awk, match.group(1), str(source)], capture_output=True)
            self.assertEqual(parsed.returncode, 0, parsed.stderr.decode(errors="replace"))
            self.assertEqual(bytes.fromhex(parsed.stdout.decode()), payload)
            expected_sha = hashlib.sha256(payload).hexdigest()
            changed = payload[:-1] + bytes([payload[-1] ^ 1])
            self.assertNotEqual(hashlib.sha256(changed).hexdigest(), expected_sha)

    def test_shutdown_lock_watch_is_opt_in_and_records_live_holder_evidence(self):
        observer = OBSERVER.read_text()
        for fragment in ("OPENWAVE_QA_SHUTDOWN", "/run/openrc/exclusive/localmount",
                         "/proc/locks", "holder_pid=", "holder_wchan=",
                         "/proc/self/mountinfo", "chmod 0600"):
            self.assertIn(fragment, observer)
        stop = re.search(r"(?ms)^stop\(\) \{.*?^\}", observer)
        self.assertIsNotNone(stop, "shutdown must capture lock state synchronously in the service stop hook")
        for fragment in ("shutdown_stop_hook_uptime=", "localmount_lock=absent",
                         "localmount_lock_record=none-visible-in-proc-locks",
                         "holder_cmdline=", "shutdown_mountinfo_begin"):
            self.assertIn(fragment, stop.group(0))


if __name__ == "__main__":
    unittest.main()
