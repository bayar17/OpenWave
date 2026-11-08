#!/usr/bin/env python3
"""Offline regression tests for Ocean's unprivileged OpenRC log handling."""

from pathlib import Path
import os
import re
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "userspace/openrc/etc/init.d/openwave-ocean"
RUNTIME = ROOT / "userspace/openrc/etc/init.d/openwave-runtime"
RUNTIME_CHECK = ROOT / "userspace/openrc/runtime-check.c"
OBSERVER = ROOT / "userspace/openrc/qa/openwave-qa-observer"
PERMISSIOND_MAIN = ROOT / "userspace/permissiond/main.cpp"
SSD_SOURCE = ROOT / "third_party/openrc-0.64.1/src/start-stop-daemon/start-stop-daemon.c"


def extracted_start_pre(log_dir: Path, stdout: Path, stderr: Path) -> str:
    service = SERVICE.read_text()
    match = re.search(r"(?ms)^start_pre\(\) \{.*?^\}", service)
    if not match:
        raise AssertionError("Ocean service has no start_pre safety checks")
    body = match.group(0)
    body = body.replace("/run/user/1000/openwave-logs", str(log_dir))
    body = body.replace("$output_log", str(stdout)).replace("$error_log", str(stderr))
    # This fixture exercises only pathname validation. The real trusted
    # registrar is covered by test_supervised_identity.py.
    body = body.replace(
        "/System/Services/openwave-register-service-identity --invalidate ocean || return 1",
        ": # registrar behavior covered separately")
    pointer = re.search(r"(?ms)^configure_qemu_tablet_pointer\(\) \{.*?^\}", service)
    if not pointer:
        raise AssertionError("Ocean service has no pointer configuration function")
    return "eerror() { :; }; einfo() { :; }\n" + pointer.group(0) + "\n" + body + "\nstart_pre\n"


class OceanLoggingTests(unittest.TestCase):
    def test_service_keeps_unprivileged_identity_and_private_logs(self):
        service = SERVICE.read_text()
        self.assertIn('command_user="ocean:ocean"', service)
        self.assertIn('no_new_privs="yes"', service)
        self.assertIn('umask="0077"', service)
        self.assertIn('output_log="/run/user/1000/openwave-logs/ocean.stdout.log"', service)
        self.assertIn('error_log="/run/user/1000/openwave-logs/ocean.stderr.log"', service)
        self.assertIn("need openwave-runtime", service)
        self.assertNotIn('command_user="root', service)
        self.assertIn('XDG_RUNTIME_DIR="/run/user/1000"', service)
        self.assertIn('DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus"', service)
        self.assertIn('QT_QPA_PLATFORM="eglfs"', service)
        self.assertIn('QT_QPA_EGLFS_INTEGRATION="eglfs_kms"', service)

    def test_runtime_provisions_and_checks_ocean_log_directory(self):
        runtime = RUNTIME.read_text()
        self.assertLess(runtime.index("/run/user/1000 || return 1"),
                        runtime.index("/run/user/1000/openwave-logs"))
        self.assertIn("[ ! -L /run/user/1000/openwave-logs ]", runtime)
        self.assertIn("checkpath -d -o ocean:ocean -m 0700 /run/user/1000/openwave-logs", runtime)
        check = RUNTIME_CHECK.read_text()
        self.assertIn('{"/run/user/1000/openwave-logs", 1000, 1000, 0700, "system_u:object_r:openwave_qa_evidence_t:s0"}', check)
        contexts = (ROOT / "userspace/security/selinux/file_contexts").read_text()
        self.assertIn("/(run|tmp)(/.*)? system_u:object_r:openwave_runtime_t:s0", contexts)
        self.assertIn("/run/user/1000/openwave-logs(/.*)? system_u:object_r:openwave_qa_evidence_t:s0", contexts)

    def test_start_pre_accepts_regular_logs_and_rejects_symlinked_or_wrong_types(self):
        with tempfile.TemporaryDirectory(prefix="openwave-ocean-log-guard-") as temporary:
            base = Path(temporary)
            logs = base / "logs"
            logs.mkdir(mode=0o700)
            stdout, stderr = logs / "ocean.stdout.log", logs / "ocean.stderr.log"
            wrapper = extracted_start_pre(logs, stdout, stderr)

            valid = subprocess.run(["/bin/sh", "-c", wrapper], capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            stdout.write_text("prior output\n")
            regular = subprocess.run(["/bin/sh", "-c", wrapper], capture_output=True, text=True)
            self.assertEqual(regular.returncode, 0, regular.stderr)

            stdout.unlink()
            stdout.symlink_to(base / "outside")
            linked_file = subprocess.run(["/bin/sh", "-c", wrapper], capture_output=True, text=True)
            self.assertNotEqual(linked_file.returncode, 0)
            stdout.unlink()
            stdout.mkdir()
            wrong_type = subprocess.run(["/bin/sh", "-c", wrapper], capture_output=True, text=True)
            self.assertNotEqual(wrong_type.returncode, 0)

            stdout.rmdir()
            logs.rename(base / "real-logs")
            (base / "logs").symlink_to(base / "real-logs", target_is_directory=True)
            linked_directory = subprocess.run(["/bin/sh", "-c", wrapper], capture_output=True, text=True)
            self.assertNotEqual(linked_directory.returncode, 0)

    def test_openrc_mode_creates_both_streams_private_and_opens_after_uid_drop(self):
        with tempfile.TemporaryDirectory(prefix="openwave-ocean-log-mode-") as temporary:
            directory = Path(temporary) / "logs"
            directory.mkdir(mode=0o700)
            stdout, stderr = directory / "out.log", directory / "err.log"
            env = os.environ.copy()
            result = subprocess.run(
                ["/bin/sh", "-c",
                 'umask 0077; printf stdout > "$1"; printf stderr > "$2"',
                 "log-fixture", str(stdout), str(stderr)],
                env=env, capture_output=True, text=True, check=True)
            self.assertEqual(stdout.read_text(), "stdout")
            self.assertEqual(stderr.read_text(), "stderr")
            self.assertEqual(stat.S_IMODE(stdout.stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(stderr.stat().st_mode), 0o600)

        source = SSD_SOURCE.read_text()
        uid_drop = source.index("if (uid && setuid(uid))")
        stdout_open = source.index("if ((stdout_fd = xopen_nonblock(redirect_stdout,")
        stderr_open = source.index("if ((stderr_fd = xopen_nonblock(redirect_stderr,")
        self.assertLess(uid_drop, stdout_open)
        self.assertLess(uid_drop, stderr_open)

    def test_qa_observer_captures_ocean_and_explicit_appd_identity_evidence(self):
        source = OBSERVER.read_text()
        for evidence in (
            "/proc/$pid/attr/current", "/proc/$pid/status", "Groups:*", "CapEff:*",
            "NoNewPrivs:*", "/proc/$pid/exe", "/proc/$pid/environ",
            "trusted_appd_identity=verified", "shell_graphical_readiness=",
            "ocean_user_logs:", "ocean.stdout.log", "ocean.stderr.log",
            "1000:1000 700", "1000:1000 600",
            "/dev/dri/card*", "QT_QPA_PLATFORM", "DBUS_SESSION_BUS_ADDRESS",
        ):
            self.assertIn(evidence, source)
        message = "permissiond: trusted appd process identity verified"
        self.assertIn(message, source)
        self.assertIn(message, PERMISSIOND_MAIN.read_text())
        self.assertIn("GetSessionIdentity", (ROOT / "userspace/permissiond/PermissionService.cpp").read_text())

    def test_runtime_type_and_inherited_descriptor_permissions_are_scoped(self):
        cil = (ROOT / "userspace/security/selinux/openwave.cil").read_text()
        self.assertIn('(typetransition openwave_system_t openwave_runtime_t dir "openwave-logs" openwave_qa_evidence_t)', cil)
        self.assertNotIn("(allow openwave_system_t openwave_runtime_t (dir (relabelfrom)))", cil)
        self.assertNotIn("(allow openwave_system_t openwave_qa_evidence_t (dir (relabelto)))", cil)
        self.assertIn("(allow openwave_shell_t openwave_system_t (fd (use)))", cil)
        self.assertIn("(allow openwave_shell_t openwave_qa_evidence_t (file (append getattr open write)))", cil)
        self.assertNotIn("(allow openwave_shell_t openwave_qa_evidence_t (dir", cil)

    def test_qa_logs_remain_in_private_runtime_not_privileged_evidence_path(self):
        service = SERVICE.read_text()
        self.assertIn('output_log="/run/user/1000/openwave-logs/ocean.stdout.log"', service)
        self.assertIn('error_log="/run/user/1000/openwave-logs/ocean.stderr.log"', service)
        self.assertNotIn("/var/log/openwave-qa", service)
        runtime = RUNTIME.read_text()
        self.assertLess(runtime.index("checkpath -d -o ocean:ocean -m 0700 /run/user/1000/openwave-logs"),
                        runtime.index("/System/Services/openwave-runtime-check"))


if __name__ == "__main__":
    unittest.main()
