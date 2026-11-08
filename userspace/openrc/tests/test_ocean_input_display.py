#!/usr/bin/env python3
"""Host-side regression coverage for Ocean's OpenRC input and display setup."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "userspace/openrc/etc/init.d/openwave-ocean"
OBSERVER = ROOT / "userspace/openrc/qa/openwave-qa-observer"


def tablet_function(sys_input: Path, dev_input: Path) -> str:
    service = SERVICE.read_text()
    match = re.search(r"(?ms)^configure_qemu_tablet_pointer\(\) \{.*?^\}", service)
    if not match:
        raise AssertionError("Ocean service has no QEMU tablet pointer setup")
    body = match.group(0)
    body = body.replace("/sys/class/input", str(sys_input))
    body = body.replace("/dev/input", str(dev_input))
    return "einfo() { :; }\n" + body + "\nconfigure_qemu_tablet_pointer\nprintf '%s\\n' \"${QT_QPA_EVDEV_MOUSE_PARAMETERS-}\"\n"


class OceanInputDisplayTests(unittest.TestCase):
    def test_qemu_tablet_is_selected_as_absolute_evdev_pointer(self):
        with tempfile.TemporaryDirectory(prefix="openwave-ocean-input-") as temporary:
            base = Path(temporary)
            sys_input = base / "sys/class/input"
            dev_input = base / "dev/input"
            for event, name in (("event2", "AT Translated Set 2 keyboard"),
                                ("event7", "QEMU QEMU USB Tablet")):
                name_path = sys_input / event / "device/name"
                name_path.parent.mkdir(parents=True)
                name_path.write_text(name + "\n")
                (dev_input / event).parent.mkdir(parents=True, exist_ok=True)
                (dev_input / event).touch()
            result = subprocess.run(["/bin/sh", "-c", tablet_function(sys_input, dev_input)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), f"{dev_input}/event7:abs")

    def test_no_qemu_tablet_leaves_normal_input_discovery_unchanged(self):
        with tempfile.TemporaryDirectory(prefix="openwave-ocean-input-") as temporary:
            base = Path(temporary)
            sys_input = base / "sys/class/input"
            dev_input = base / "dev/input"
            name_path = sys_input / "event2/device/name"
            name_path.parent.mkdir(parents=True)
            name_path.write_text("USB Mouse\n")
            (dev_input / "event2").parent.mkdir(parents=True)
            (dev_input / "event2").touch()
            result = subprocess.run(["/bin/sh", "-c", tablet_function(sys_input, dev_input)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "\n")

    def test_pointer_configuration_runs_after_log_safety_checks_and_qa_records_mode(self):
        service = SERVICE.read_text()
        start_pre = re.search(r"(?ms)^start_pre\(\) \{.*?^\}", service)
        self.assertIsNotNone(start_pre)
        self.assertLess(start_pre.group(0).index("for path"),
                        start_pre.group(0).index("configure_qemu_tablet_pointer"))
        self.assertIn("QT_QPA_EVDEV_MOUSE_PARAMETERS", OBSERVER.read_text())
        self.assertIn("/sys/class/drm/card*-Virtual-1", OBSERVER.read_text())
        self.assertIn("/sys/class/graphics/fb0/virtual_size", OBSERVER.read_text())


if __name__ == "__main__":
    unittest.main()
