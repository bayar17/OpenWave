"""Host-only tests for fail-closed Waydroid mount preflight reporting."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from openwave_mount_preflight_report import evaluate, REQUIRED


def sample():
    body = ["MOUNT_QA_BEGIN enforcing=1"]
    body.extend(f"MOUNT_QA_GATE {gate}=PASS expected" for gate in sorted(REQUIRED))
    body.extend([
        "MOUNT_QA_INFO helper_help=NOT_EXECUTABLE_FROM_OBSERVER",
        "MOUNT_QA_INFO helper_domain_transition=UNVERIFIED",
        "MOUNT_QA_INFO mount_syscalls=NOT_ATTEMPTED",
        "MOUNT_QA_END prerequisites=PASS",
    ])
    return "\n".join(body) + "\n"


class MountPreflightReportTests(unittest.TestCase):
    def test_complete_readonly_prerequisites_pass_but_not_release(self):
        result = evaluate(sample())
        self.assertTrue(result["diagnostics_complete"])
        self.assertTrue(result["preflight_passed"])
        self.assertFalse(result["release_ready"])
        self.assertEqual(result["helper_domain_transition"], "UNVERIFIED")
        self.assertEqual(result["mount_syscalls"], "NOT_ATTEMPTED")
        self.assertEqual(result["android_boot"], "UNVERIFIED")

    def test_missing_required_gate_cannot_pass(self):
        serial = sample().replace("MOUNT_QA_GATE helper_label=PASS expected\n", "")
        self.assertFalse(evaluate(serial)["preflight_passed"])

    def test_bad_label_or_image_does_not_pass(self):
        for gate in ("helper_label", "image_system", "image_vendor", "loop_control"):
            with self.subTest(gate=gate):
                bad = sample().replace(f"MOUNT_QA_GATE {gate}=PASS",
                                       f"MOUNT_QA_GATE {gate}=BLOCKED")
                self.assertFalse(evaluate(bad)["preflight_passed"])

    def test_permissive_guest_is_rejected(self):
        self.assertFalse(evaluate(sample().replace("MOUNT_QA_BEGIN enforcing=1",
                                                  "MOUNT_QA_BEGIN enforcing=0"))["preflight_passed"])

    def test_missing_end_marker_is_rejected(self):
        self.assertFalse(evaluate(sample().replace("MOUNT_QA_END prerequisites=PASS", ""))["preflight_passed"])

    def test_duplicate_gate_is_rejected(self):
        serial = sample().replace("MOUNT_QA_END prerequisites=PASS",
                                  "MOUNT_QA_GATE enforcing=PASS unexpected\nMOUNT_QA_END prerequisites=PASS")
        self.assertFalse(evaluate(serial)["preflight_passed"])

    def test_no_conflation_with_real_mount_or_android_boot(self):
        r = evaluate(sample())
        self.assertFalse(r["release_ready"])
        self.assertNotIn("mount_operations_passed", r)
        self.assertNotIn("android_framework_passed", r)

    def test_crlf_guest_serial_is_accepted(self):
        self.assertTrue(evaluate(sample().replace("\n", "\r\n"))["preflight_passed"])

    def test_guest_inline_python_is_syntactically_valid(self):
        script = (Path(__file__).resolve().parents[2] /
                  "userspace/openrc/qa/waydroid-mount-preflight.start").read_text()
        self.assertIn("openwave.qa=mount-preflight", script)
        source = script.split("<<'PY'\n", 1)[1].rsplit("\nPY\n", 1)[0]
        compile(source, "waydroid-mount-preflight.start", "exec")
        self.assertNotIn("os.system(", source)
        self.assertNotIn("subprocess.Popen(", source)
        self.assertNotIn("MS_BIND", source)
        self.assertNotIn("umount(", source)
        self.assertIn("helper_domain_transition=UNVERIFIED", source)
        self.assertIn("mount_syscalls=NOT_ATTEMPTED", source)

    def test_observer_never_executes_privileged_helper_during_preflight(self):
        shell = (Path(__file__).resolve().parents[2] /
                 "userspace/openrc/qa/waydroid-mount-preflight.start").read_text()
        source = shell.split("<<'PY'\n", 1)[1].rsplit("\nPY\n", 1)[0]
        self.assertIn("def helper_inode_ok(path):", source)
        self.assertIn("st.st_mode & 0o111", source)
        self.assertNotIn("os.access(", source.split("record('helper_inode'", 1)[1])
        self.assertIn("helper_inode_ok(HELPER)", source)
        self.assertNotIn("subprocess.run(", source)
        self.assertNotIn("subprocess.Popen(", source)
        self.assertIn("helper_help=NOT_ATTEMPTED_READ_ONLY_OBSERVER", source)
        self.assertIn("def loop_control_ok():", source)
        self.assertIn("stat.S_ISCHR(st.st_mode)", source)
        self.assertIn("os.major(st.st_rdev) == 10", source)
        self.assertIn("os.minor(st.st_rdev) == 237", source)
        self.assertNotIn("Path('/dev/loop-control').exists()", source)

    def test_observer_policy_is_metadata_only_and_explicitly_scoped(self):
        policy = (Path(__file__).resolve().parents[2] /
                  "userspace/security/selinux/openwave.cil").read_text()
        self.assertIn("(allow openwave_system_t openwave_waydroid_mount_exec_t\n       (file (getattr read open)))", policy)
        self.assertIn("(allow openwave_system_t openwave_loop_control_t\n       (chr_file (getattr)))", policy)
        self.assertNotIn("(allow openwave_system_t openwave_waydroid_mount_exec_t\n       (file (read open getattr map execute)))", policy)
        self.assertNotIn("(allow openwave_system_t openwave_loop_control_t\n       (chr_file (open read write ioctl)))", policy)

    def test_unknown_success_markers_cannot_replace_required_gate(self):
        serial = sample().replace("MOUNT_QA_GATE helper_elf=PASS expected\n",
                                  "MOUNT_QA_GATE placeholder=PASS expected\n")
        self.assertFalse(evaluate(serial)["preflight_passed"])


if __name__ == "__main__":
    unittest.main()
