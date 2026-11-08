"""Host-only guards for Stage 2H app-control IPC evidence and policy scope."""
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from openwave_app_control_report import evaluate


def evidence():
    lines = [
        "OPENWAVE_APP_SMOKE_BEGIN",
        "APP_TEST CONTROL_ACK com.openwave.files pid=1701 authenticated response and normal exit",
        "APP_TEST CONTROL_ACK com.openwave.photos pid=1702 authenticated response and normal exit",
        "APP_TEST CONTROL_ACK com.openwave.ocean pid=1703 authenticated response and normal exit",
        "APP_CONTROL_PASS authenticated quit acknowledgements and clean exits checks=3",
        "APP_SMOKE PASS confined live app launched through Ocean",
        "OPENWAVE_APP_SMOKE_END",
    ]
    return "\n".join(lines)


class AppControlTests(unittest.TestCase):
    def test_accepted_evidence_has_three_app_specific_acknowledgements(self):
        status = evaluate(evidence().replace("\n", "\r\n"))
        self.assertTrue(status["passed"])
        self.assertEqual(status["quit_ack_count"], 3)
        self.assertFalse(status["release_ready"])

    def test_no_ack_no_success_even_with_app_smoke_pass(self):
        self.assertFalse(evaluate(
            "APP_CONTROL_PASS authenticated quit acknowledgements and clean exits checks=3\n"
            "APP_SMOKE PASS confined live app launched through Ocean\n"
            "OPENWAVE_APP_SMOKE_END")["passed"])

    def test_duplicate_or_mismatched_count_fails(self):
        for invalid in (
            evidence().replace("checks=3", "checks=4"),
            evidence() + "\nAPP_CONTROL_PASS authenticated quit acknowledgements and clean exits checks=3",
            evidence().replace("APP_TEST CONTROL_ACK com.openwave.files pid=1701 authenticated response and normal exit",
                               "APP_TEST CONTROL_ACK com.openwave.photos pid=1702 authenticated response and normal exit"),
            evidence().replace("com.openwave.ocean", "com.openwave.waterrouge-gallery"),
            evidence().replace("APP_SMOKE PASS confined live app launched through Ocean", ""),
            evidence().replace("OPENWAVE_APP_SMOKE_END", ""),
            evidence() + "\nAPP_CONTROL_FAIL checks=3 passed=2",
            evidence() + "\nAPP_TEST CONTROL_FAIL com.openwave.ocean pid=1703 no ACK",
        ):
            with self.subTest(invalid=invalid[-70:]):
                self.assertFalse(evaluate(invalid)["passed"])

    def test_guest_script_only_checks_apps_with_real_qa_control_channels(self):
        guest = (ROOT / "userspace/openrc/qa/app-launch.start").read_text()
        for app in ("com.openwave.files", "com.openwave.photos", "com.openwave.ocean"):
            self.assertIn(f"{app}:stop", guest)
        self.assertNotIn("com.openwave.settings:stop", guest)
        self.assertNotIn("com.openwave.waterrouge-gallery:stop", guest)
        self.assertIn('control_checks=$((control_checks + 1))', guest)
        self.assertIn('control_passes=$((control_passes + 1))', guest)
        self.assertIn('wait-status=0 stopping=true', guest)
        self.assertIn('control quit-fallback appId=', guest)
        self.assertIn('control quit-timeout appId=', guest)
        self.assertIn("APP_CONTROL_PASS authenticated quit acknowledgements and clean exits", guest)
        self.assertEqual(subprocess.run(
            ["/bin/sh", "-n", str(ROOT / "userspace/openrc/qa/app-launch.start")],
            capture_output=True).returncode, 0)

    def test_broker_checks_peer_pid_uid_and_reports_fallback(self):
        proc = (ROOT / "userspace/appd/process.c").read_text()
        self.assertIn("peer.uid != instance->uid", proc)
        self.assertIn("peer.pid != c->pid", proc)
        self.assertIn("if (!instance) { command_done(c, FALSE); return; }", proc)
        self.assertIn("appd: control quit-ack", proc)
        self.assertIn("appd: control quit-fallback", proc)
        self.assertIn("appd: control quit-timeout", proc)
        self.assertIn("kill(-c->pid, SIGTERM)", proc)

    def test_selinux_permission_only_broker_to_app_channel(self):
        policy = (ROOT / "userspace/security/selinux/openwave.cil").read_text()
        self.assertEqual(policy.count(
            "(allow openwave_appd_t openwave_app_t (unix_stream_socket (connectto)))"), 1)
        self.assertNotIn(
            "(allow openwave_appd_t openwave_all_types (unix_stream_socket (connectto)))", policy)
        self.assertNotIn(
            "(allow openwave_waydroid_mount_t openwave_app_t (unix_stream_socket (connectto)))", policy)
        qemu = (ROOT / "scripts/test-openwave-boot.py").read_text()
        self.assertIn("from openwave_app_control_report import evaluate", qemu)
        self.assertIn("not report['app_control_passed']", qemu)
        self.assertIn("app-control.json", qemu)


if __name__ == "__main__":
    unittest.main()
