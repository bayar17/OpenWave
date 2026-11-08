#!/usr/bin/env python3
"""Verify wallpaper QA settings are explicit and candidate-only."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
CONFIGURATOR = ROOT / "userspace/openrc/enable-wallpaper-qa.py"
SERVICE_DIR = ROOT / "userspace/openrc/etc/init.d"
OBSERVER = ROOT / "userspace/openrc/qa/openwave-qa-observer"


class WallpaperQaEnvironmentTests(unittest.TestCase):
    def test_production_service_sources_do_not_enable_diagnostics(self):
        for service in ("appd", "wallpaperagentd", "openwave-ocean"):
            text = (SERVICE_DIR / service).read_text()
            for flag in ("OPENWAVE_QA_IDENTITY", "OPENWAVE_QA_GRAPHICS",
                         "OPENWAVE_QA_WALLPAPER_IMAGE", "QT_QPA_EGLFS_DEBUG",
                         "QT_LOGGING_RULES"):
                self.assertNotIn(flag, text, (service, flag))
        self.assertNotIn('export OPENWAVE_QA_SHUTDOWN="1"', OBSERVER.read_text())

    def test_candidate_environment_is_installed_once_and_captured(self):
        with tempfile.TemporaryDirectory(prefix="openwave-wallpaper-qa-env-") as temporary:
            root = Path(temporary)
            (root / "etc/selinux").mkdir(parents=True)
            (root / "etc/selinux/config").write_text("SELINUX=enforcing\n")
            services = root / "etc/init.d"
            services.mkdir()
            for name in ("appd", "wallpaperagentd", "openwave-ocean"):
                shutil.copy2(SERVICE_DIR / name, services / name)
            shutil.copy2(OBSERVER, services / "openwave-qa-observer")

            subprocess.run(["python3", str(CONFIGURATOR), "--root", str(root)], check=True)
            expected = {
                "appd": ('export OPENWAVE_QA_IDENTITY="1"',),
                "wallpaperagentd": ('export OPENWAVE_QA_IDENTITY="1"',
                                    "/var/log/openwave-qa/wallpaper.stdout.log",
                                    "/var/log/openwave-qa/wallpaper.stderr.log", 'umask="0077"'),
                "openwave-ocean": ('export OPENWAVE_QA_GRAPHICS="1"',
                                    "/run/user/1000/openwave-logs/ocean.stdout.log",
                                    "/run/user/1000/openwave-logs/ocean.stderr.log",
                                    'export OPENWAVE_QA_WALLPAPER_IMAGE="bundled-svg"',
                                    'export QT_QPA_EGLFS_DEBUG="1"',
                                    'export QT_LOGGING_RULES="qt.qpa.eglfs.kms=true"'),
                "openwave-qa-observer": ('export OPENWAVE_QA_SHUTDOWN="1"',),
            }
            for service, values in expected.items():
                text = (services / service).read_text()
                for value in values:
                    self.assertIn(value, text, (service, value))

            ocean = (services / "openwave-ocean").read_text()
            self.assertIn("command_user=\"ocean:ocean\"", ocean)
            self.assertIn('OPENWAVE_TRUSTED_SHELL="1"', ocean)
            self.assertIn("/System/Services/openwave-register-service-identity ocean || return 1",
                          ocean)
            self.assertIn("/System/Services/openwave-register-service-identity --invalidate ocean",
                          ocean)
            observer = (services / "openwave-qa-observer").read_text()
            self.assertIn("start_shutdown_lock_watch", observer)
            self.assertIn("/var/log/openwave-qa/*.stderr.log", observer)
            self.assertIn("ocean_user_logs:", observer)
            self.assertIn("/bin/busybox xxd -r -p", observer)
            self.assertIn("/bin/busybox sha256sum", observer)
            self.assertNotIn("wc -l", observer)
            self.assertNotIn("| tr -d", observer)
            self.assertNotIn("| wc -l", observer)
            self.assertIn("OPENWAVE_QA_WALLPAPER_IMAGE", OBSERVER.read_text())

            before = {name: (services / name).read_bytes()
                      for name in expected}
            subprocess.run(["python3", str(CONFIGURATOR), "--root", str(root)], check=True)
            self.assertEqual(before, {name: (services / name).read_bytes()
                                      for name in expected})

    def test_qa_log_destinations_are_exact_and_production_logs_stay_unchanged(self):
        base = {name: (SERVICE_DIR / name).read_text()
                for name in ("appd", "wallpaperagentd", "openwave-ocean")}
        self.assertIn('output_log="/run/openwave/appd.stdout.log"', base["appd"])
        self.assertIn('output_log="/run/user/1000/openwave-logs/ocean.stdout.log"',
                      base["openwave-ocean"])
        self.assertIn('error_log="/run/user/1000/openwave-logs/ocean.stderr.log"',
                      base["openwave-ocean"])
        self.assertNotIn("/var/log/openwave-qa", "\n".join(base.values()))
        cil = (ROOT / "userspace/security/selinux/openwave.cil").read_text()
        contexts = (ROOT / "userspace/security/selinux/file_contexts").read_text()
        self.assertIn("/var/log/openwave-qa(/.*)? system_u:object_r:openwave_qa_evidence_t:s0",
                      contexts)
        self.assertIn("(allow openwave_system_t openwave_private_t (dir (search)))", cil)
        self.assertIn("(allow openwave_system_t openwave_qa_evidence_t (file (append create getattr open read rename setattr unlink write)))", cil)
        for domain in ("openwave_appd_t", "openwave_wallpaper_t", "openwave_shell_t"):
            self.assertIn(f"(allow {domain} openwave_qa_evidence_t (file (append getattr open write)))", cil)
        self.assertNotIn("openwave_qa_evidence_t (file (all))", cil)
        self.assertNotIn("(allow openwave_shell_t openwave_qa_evidence_t (dir", cil)

    def test_refuses_non_enforcing_or_incomplete_root(self):
        with tempfile.TemporaryDirectory(prefix="openwave-wallpaper-qa-refuse-") as temporary:
            root = Path(temporary)
            (root / "etc/selinux").mkdir(parents=True)
            (root / "etc/selinux/config").write_text("SELINUX=permissive\n")
            result = subprocess.run(["python3", str(CONFIGURATOR), "--root", str(root)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("SELinux enforcing candidate is required", result.stderr)


if __name__ == "__main__":
    unittest.main()
