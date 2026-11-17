from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "userspace/openrc/register-service-identity.c"
HARNESS = ROOT / "userspace/openrc/tests/identity_harness.c"
SERVICES = ROOT / "userspace/openrc/etc/init.d"
IDENTITY_CONSUMER = ROOT / "userspace/security/OpenWaveIdentity.c"
PERMISSION_SERVICE = ROOT / "userspace/permissiond/PermissionService.cpp"


class SupervisedIdentityTests(unittest.TestCase):
    def test_fixed_allowlist_and_fail_closed_production_entrypoint(self):
        source = SOURCE.read_text()
        for name, pidfile, executable in (
            ("appd", "/run/openwave/appd.pid", "/System/Services/appd"),
            ("permissiond", "/run/openwave/permissiond.pid", "/System/Services/permissiond"),
            ("portal", "/run/openwave/portal.pid", "/System/Services/openwave-portal"),
            ("promptd", "/run/openwave/promptd.pid", "/System/Services/promptd"),
            ("wallpaperagentd", "/run/openwave/wallpaper.pid", "/System/Services/wallpaperagentd"),
            ("connectivityd", "/run/openwave/connectivityd.pid", "/System/Services/connectivityd"),
            ("ocean", "/run/openwave/ocean.pid", "/System/Applications/com.openwave.shell/bin/ocean-launcher"),
        ):
            self.assertIn(f'{{"{name}", "{pidfile}",', source)
            self.assertIn(f'"{executable}"', source)
        self.assertIn("if (!service)", source)
        self.assertIn("if (geteuid() != 0)", source)
        self.assertIn("O_NOFOLLOW", source)
        self.assertIn("process_is_expected(pid, service->executable, service->process_uid)", source)
        self.assertIn("process_start(pid) != ticks", source)
        self.assertIn("service->process_uid", source)
        self.assertIn("fsync(fd)", source)
        self.assertIn("renameat(dirfd, temporary, dirfd, marker)", source)
        self.assertIn("fchown(fd, OPENWAVE_OWNER_UID, OPENWAVE_OWNER_UID)", source)

        with tempfile.TemporaryDirectory(prefix="openwave-identity-cli-") as tmp:
            binary = Path(tmp) / "registrar"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(SOURCE), "-o", str(binary)], check=True)
            unknown = subprocess.run([str(binary), "arbitrary"], capture_output=True, text=True)
            self.assertEqual(unknown.returncode, 2)
            self.assertIn("unknown supervised service", unknown.stderr)
            if os.geteuid() != 0:
                denied = subprocess.run([str(binary), "--invalidate", "appd"], capture_output=True, text=True)
                self.assertEqual(denied.returncode, 1)
                self.assertIn("must run as root", denied.stderr)

    def test_openrc_identity_start_restart_and_stop_order(self):
        expected = {
            "connectivityd": "connectivityd",
            "appd": "appd", "permissiond": "permissiond", "openwave-portal": "portal",
            "promptd": "promptd", "wallpaperagentd": "wallpaperagentd", "openwave-ocean": "ocean",
        }
        for service, identity in expected.items():
            script = (SERVICES / service).read_text()
            invalidate = f"openwave-register-service-identity --invalidate {identity}"
            register = f"openwave-register-service-identity {identity} || return 1"
            self.assertIn(f"{invalidate} || return 1", script)
            self.assertIn(register, script)
            self.assertIn(invalidate, script.split("stop_post()", 1)[-1])
            self.assertLess(script.index(f"{invalidate} || return 1"), script.index("start_post()"))
            self.assertGreater(script.index(register), script.index("start_post()"))
            self.assertLess(script.index(register), script.index("openwave-wait-dbus"))

        # The XDG portal runs in Ocean's session and is deliberately not a
        # root-owned trusted-service identity in permissiond's allowlist.
        xdg = (SERVICES / "xdg-desktop-portal-openwave").read_text()
        self.assertNotIn("openwave-register-service-identity", xdg)

    def test_permissiond_keeps_dbus_credentials_and_live_marker_authentication(self):
        identity = IDENTITY_CONSUMER.read_text()
        permission = PERMISSION_SERVICE.read_text()
        self.assertIn('"GetNameOwner"', identity)
        self.assertIn('"GetConnectionCredentials"', identity)
        self.assertIn('"UnixUserID"', identity)
        self.assertIn('"ProcessID"', identity)
        self.assertIn("openwave_supervised_process(uid, pid, pidfile)", identity)
        self.assertIn('"/run/openwave/appd.pid"', permission)
        self.assertIn('"org.openwave.Applications"', permission)
        self.assertIn("O_NOFOLLOW", identity)
        self.assertIn("st_uid", identity)
        self.assertIn("ticks == openwave_process_start(pid)", identity)
        self.assertIn("The supervised system service could not be authenticated", identity)

    def test_live_identity_validation_write_invalidation_and_restart(self):
        with tempfile.TemporaryDirectory(prefix="openwave-identity-fixture-") as tmp:
            base = Path(tmp)
            runtime = base / "runtime"
            proc = base / "proc"
            pid = os.getpid()
            process = proc / str(pid)
            runtime.mkdir(mode=0o700)
            process.mkdir(parents=True)
            expected_exe = base / "appd"
            wrong_exe = base / "not-appd"
            expected_exe.write_text("fixture executable")
            wrong_exe.write_text("wrong executable")
            expected_exe.chmod(0o755)
            wrong_exe.chmod(0o755)

            def proc_state(ticks, exe):
                stat_fields = ["S"] + ["1"] * 18 + [str(ticks)]
                (process / "stat").write_text(f"{pid} (fixture daemon) " + " ".join(stat_fields) + "\n")
                owner = os.geteuid()
                (process / "status").write_text(
                    f"Name:\tfixture\nUid:\t{owner}\t{owner}\t{owner}\t{owner}\n")
                (process / "exe").unlink(missing_ok=True)
                (process / "exe").symlink_to(exe)
                (runtime / "appd.pid").write_text(f"{pid}\n")
                (runtime / "appd.pid").chmod(0o600)

            compiler = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f'-DOPENWAVE_RUNTIME_DIR="{runtime}"',
                f'-DOPENWAVE_PROC_DIR="{proc}"',
                f"-DOPENWAVE_OWNER_UID={os.geteuid()}",
                f"-DOPENWAVE_ROOT_SERVICE_UID={os.geteuid()}",
                f"-DOPENWAVE_OCEAN_UID={os.geteuid()}",
                f'-DOPENWAVE_APPD_EXECUTABLE="{expected_exe}"',
                f'-DOPENWAVE_OCEAN_EXECUTABLE="{expected_exe}"',
                str(HARNESS), "-o", str(base / "harness"),
            ]
            subprocess.run(compiler, check=True)
            harness = base / "harness"

            proc_state(701, expected_exe)
            subprocess.run([str(harness), "register", str(pid)], check=True)
            marker = runtime / "appd.pid.identity"
            self.assertEqual(marker.read_text(), f"{pid} {os.geteuid()} 701\n")
            self.assertEqual(marker.stat().st_uid, os.geteuid())
            self.assertEqual(marker.stat().st_mode & 0o777, 0o644)
            subprocess.run([str(harness), "check", str(pid)], check=True)

            # A different executable cannot satisfy the fixed appd identity.
            proc_state(701, wrong_exe)
            rejected = subprocess.run([str(harness), "check", str(pid)])
            self.assertEqual(rejected.returncode, 13)

            # Missing /proc state is a stale PID; changing start time makes
            # an old record fail the same check permissiond performs.
            proc_state(702, expected_exe)
            self.assertNotEqual(702, int(marker.read_text().split()[2]))
            self.assertEqual(subprocess.run([str(harness), "check", str(pid)]).returncode, 13)
            (process / "stat").unlink()
            self.assertEqual(subprocess.run([str(harness), "check", str(pid)]).returncode, 13)

            # A restart first removes the old record, then registers the new
            # PID/start-time pair. Stop cleanup is idempotent.
            proc_state(702, expected_exe)
            subprocess.run([str(harness), "invalidate", str(pid)], check=True)
            self.assertFalse(marker.exists())

            # A process identity owned by the host user is not acceptable to
            # the production root-owned identity consumer.
            if os.geteuid() != 0:
                subprocess.run([str(harness), "register", str(pid)], check=True)
                root_consumer = base / "root-consumer"
                subprocess.run([
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    f'-DOPENWAVE_RUNTIME_DIR="{runtime}"', f'-DOPENWAVE_PROC_DIR="{proc}"',
                    "-DOPENWAVE_OWNER_UID=0", "-DOPENWAVE_ROOT_SERVICE_UID=0",
                    f'-DOPENWAVE_APPD_EXECUTABLE="{expected_exe}"',
                    str(HARNESS), "-o", str(root_consumer),
                ], check=True)
                self.assertEqual(subprocess.run([str(root_consumer), "check", str(pid)]).returncode, 13)
            subprocess.run([str(harness), "register", str(pid)], check=True)
            self.assertEqual(marker.read_text(), f"{pid} {os.geteuid()} 702\n")
            subprocess.run([str(harness), "invalidate", str(pid)], check=True)
            subprocess.run([str(harness), "invalidate", str(pid)], check=True)
            self.assertFalse(marker.exists())

    def test_ocean_identity_records_unprivileged_uid_and_rejects_uid_tampering(self):
        with tempfile.TemporaryDirectory(prefix="openwave-ocean-identity-") as tmp:
            base = Path(tmp)
            runtime = base / "runtime"
            proc = base / "proc"
            pid = os.getpid()
            process = proc / str(pid)
            runtime.mkdir(mode=0o700)
            process.mkdir(parents=True)
            executable = base / "ocean-launcher"
            executable.write_text("fixture Ocean executable")
            executable.chmod(0o755)
            ticks = 811
            stat_fields = ["S"] + ["1"] * 18 + [str(ticks)]
            (process / "stat").write_text(f"{pid} (fixture Ocean) " + " ".join(stat_fields) + "\n")
            owner = os.geteuid()
            (process / "status").write_text(f"Name:\tOcean\nUid:\t{owner}\t{owner}\t{owner}\t{owner}\n")
            (process / "exe").symlink_to(executable)
            (runtime / "ocean.pid").write_text(f"{pid}\n")
            (runtime / "ocean.pid").chmod(0o600)
            binary = base / "harness"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f'-DOPENWAVE_RUNTIME_DIR="{runtime}"', f'-DOPENWAVE_PROC_DIR="{proc}"',
                f"-DOPENWAVE_OWNER_UID={owner}", f"-DOPENWAVE_ROOT_SERVICE_UID={owner}",
                f"-DOPENWAVE_OCEAN_UID={owner}", f'-DOPENWAVE_OCEAN_EXECUTABLE="{executable}"',
                str(HARNESS), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary), "register", "ocean", str(pid)], check=True)
            marker = runtime / "ocean.pid.identity"
            self.assertEqual(marker.read_text(), f"{pid} {owner} {ticks}\n")
            subprocess.run([str(binary), "check", "ocean", str(pid)], check=True)
            marker.write_text(f"{pid} {owner + 1} {ticks}\n")
            self.assertEqual(subprocess.run([str(binary), "check", "ocean", str(pid)]).returncode, 13)

    @unittest.skipIf(os.geteuid() == 0, "ownership mismatch fixture requires an unprivileged host")
    def test_wrong_runtime_directory_owner_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="openwave-identity-owner-") as tmp:
            runtime = Path(tmp) / "runtime"
            proc = Path(tmp) / "proc"
            runtime.mkdir(mode=0o700)
            binary = Path(tmp) / "harness"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                f'-DOPENWAVE_RUNTIME_DIR="{runtime}"', f'-DOPENWAVE_PROC_DIR="{proc}"',
                "-DOPENWAVE_OWNER_UID=0", f'-DOPENWAVE_APPD_EXECUTABLE="{tmp}/appd"',
                str(HARNESS), "-o", str(binary),
            ], check=True)
            self.assertNotEqual(subprocess.run([str(binary), "invalidate", "2"]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
