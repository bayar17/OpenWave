#!/usr/bin/env python3
"""Host-only tests for bounded D-Bus readiness and OpenRC integration."""

from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
VALIDATOR_SPEC = importlib.util.spec_from_file_location(
    "validate_openrc", ROOT / "userspace/openrc/validate_openrc.py")
VALIDATOR = importlib.util.module_from_spec(VALIDATOR_SPEC)
VALIDATOR_SPEC.loader.exec_module(VALIDATOR)


class ServiceReadinessTests(unittest.TestCase):
    def test_wait_dbus_requires_every_name_and_is_bounded(self):
        source = (ROOT / "userspace/openrc/openwave-wait-dbus").read_text()
        with tempfile.TemporaryDirectory(prefix="openwave-wait-dbus-") as temporary:
            root = Path(temporary)
            gdbus = root / "gdbus"
            sleep = root / "sleep"
            gdbus.write_text("""#!/bin/sh
printf '%s\\n' \"$*\" >> \"$DBUS_CALL_LOG\"
for last do :; done
if [ \"$last\" = late ] && [ \"$(wc -l < \"$DBUS_CALL_LOG\")\" -ge 15 ]; then exit 0; fi
case \" $WAIT_FOR_NAMES \" in *\" $last \"*) exit 0 ;; esac
exit 1
""")
            sleep.write_text("#!/bin/sh\nexit 0\n")
            gdbus.chmod(0o755)
            sleep.chmod(0o755)
            helper = root / "helper"
            helper.write_text(source.replace("/usr/bin/gdbus", str(gdbus)).replace("/bin/sleep", str(sleep)))
            helper.chmod(0o755)
            env = os.environ | {"DBUS_CALL_LOG": str(root / "calls"), "WAIT_FOR_NAMES": "first second"}
            passed = subprocess.run(["/bin/sh", str(helper), "session", "first", "second"],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(passed.returncode, 0, passed.stderr)
            calls = (root / "calls").read_text().splitlines()
            self.assertEqual(len(calls), 2)
            (root / "calls").unlink()
            failed = subprocess.run(["/bin/sh", str(helper), "system", "never"],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(failed.returncode, 1)
            self.assertIn("timed out waiting for system bus names: never", failed.stderr)
            self.assertEqual(len((root / "calls").read_text().splitlines()), 100)
            self.assertTrue(all(line.startswith("call --") for line in
                                (root / "calls").read_text().splitlines()))
            (root / "calls").unlink()
            late = subprocess.run(["/bin/sh", str(helper), "system", "late"],
                                  env=env, capture_output=True, text=True)
            self.assertEqual(late.returncode, 0, late.stderr)
            self.assertGreater(len((root / "calls").read_text().splitlines()), 10)

    def test_session_bus_probe_uses_real_gdbus_protocol(self):
        dbus_daemon = shutil.which("dbus-daemon")
        gdbus = shutil.which("gdbus")
        if not dbus_daemon or not gdbus:
            self.skipTest("host dbus-daemon and gdbus are required")
        config_source = (ROOT / "userspace/openrc/openwave-openrc-session.conf").read_text()
        helper_source = (ROOT / "userspace/openrc/openwave-wait-dbus").read_text()
        with tempfile.TemporaryDirectory(prefix="openwave-dbus-protocol-") as temporary:
            root = Path(temporary)
            service_dir = root / "services"
            service_dir.mkdir()
            socket_path = root / "bus"
            config = root / "session.conf"
            config.write_text(config_source
                              .replace("/run/user/1000/bus", str(socket_path))
                              .replace("/usr/share/dbus-1/services", str(service_dir)))
            environment = os.environ.copy()
            environment["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={socket_path}"
            daemon = subprocess.Popen(
                [dbus_daemon, f"--config-file={config}", "--nofork"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                env=environment)
            try:
                for _ in range(100):
                    if socket_path.exists() or daemon.poll() is not None:
                        break
                    time.sleep(0.02)
                self.assertTrue(socket_path.exists(), "session bus did not create its socket")
                helper = root / "wait-dbus"
                helper.write_text(helper_source.replace("/usr/bin/gdbus", gdbus))
                helper.chmod(0o755)
                result = subprocess.run(
                    ["/bin/sh", str(helper), "session", "org.freedesktop.DBus"],
                    env=environment, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                names = subprocess.run(
                    [gdbus, "call", "--session", "--timeout", "2", "--dest",
                     "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus",
                     "--method", "org.freedesktop.DBus.ListNames"],
                    env=environment, capture_output=True, text=True, timeout=5)
                self.assertEqual(names.returncode, 0, names.stderr)
                self.assertIn("org.freedesktop.DBus", names.stdout)
            finally:
                if daemon.poll() is None:
                    daemon.terminate()
                    try:
                        daemon.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        daemon.kill()
                        daemon.wait(timeout=2)

    def test_units_gate_service_readiness_and_preserve_native_security(self):
        openrc = ROOT / "userspace/openrc/etc/init.d"
        self.assertIn("openwave-wait-dbus system org.openwave.Applications",
                      (openrc / "appd").read_text())
        self.assertIn("ListApplications", (openrc / "appd").read_text())
        self.assertIn("openwave-wait-dbus session org.openwave.Prompt",
                      (openrc / "promptd").read_text())
        ocean = (openrc / "openwave-ocean").read_text()
        self.assertIn('command_user="ocean:ocean"', ocean)
        self.assertIn('no_new_privs="yes"', ocean)
        self.assertNotIn('command_user="root', ocean)
        self.assertNotIn("openwave-runtime", (ROOT / "userspace/openrc/default-services.txt").read_text())
        self.assertIn("openwave-runtime", (ROOT / "userspace/openrc/boot-services.txt").read_text())
        runtime = (openrc / "openwave-runtime").read_text()
        udev = (openrc / "udevd").read_text()
        self.assertNotIn("keyword -shutdown", runtime)
        self.assertIn("stop() { return 0; }", runtime)
        self.assertNotRegex(runtime, r"(?m)^\s*before\s+.*\budevd\b")
        self.assertIn("need devfs sysfs", udev)
        self.assertNotIn("need openwave-runtime", udev)
        self.assertIn("udevadm trigger --action=add", udev)
        self.assertIn("udevadm settle", udev)
        self.assertLess(udev.index("udevadm trigger --action=add"), udev.index("udevadm settle"))
        openrc_source = (ROOT / "third_party/openrc-0.64.1/src/openrc/rc.c").read_text()
        self.assertIn('rc_stringlist_find(kwords, "-shutdown")', openrc_source)
        for name in ("openwave-wait-dbus", "appd", "openwave-qa-observer"):
            if name == "openwave-wait-dbus":
                path = ROOT / "userspace/openrc" / name
            elif name == "openwave-qa-observer":
                path = ROOT / "userspace/openrc/qa" / name
            else:
                path = openrc / name
            source = path.read_text()
            if name == "openwave-wait-dbus":
                self.assertIn('gdbus call "$bus_option"', source)
            else:
                self.assertIn("gdbus call --system", source)
            self.assertNotIn('gdbus "$bus_option"', source)
        for path in openrc.iterdir():
            subprocess.run(["/bin/sh", "-n", str(path)], check=True)

    def test_udev_provisions_early_paths_and_fails_closed(self):
        service = ROOT / "userspace/openrc/etc/init.d/udevd"
        script = '''
checkpath() { printf '%s\\n' "$*"; [ "$6" != "$FAIL_PATH" ]; }
. "$SERVICE"
start_pre
'''
        environment = os.environ | {"SERVICE": str(service), "FAIL_PATH": ""}
        passed = subprocess.run(["/bin/sh", "-c", script], env=environment,
                                capture_output=True, text=True)
        self.assertEqual(passed.returncode, 0, passed.stderr)
        self.assertEqual(passed.stdout.splitlines(), [
            "-d -o root:root -m 0755 /run/openwave",
            "-d -o root:root -m 0755 /run/udev",
        ])
        failed = subprocess.run(["/bin/sh", "-c", script],
                                env=environment | {"FAIL_PATH": "/run/openwave"},
                                capture_output=True, text=True)
        self.assertEqual(failed.returncode, 1, failed.stderr)
        self.assertEqual(len(failed.stdout.splitlines()), 1)

    def test_virtual_device_dependency_detects_shutdown_cycle(self):
        openrc = ROOT / "userspace/openrc/etc/init.d"
        # Include the real pinned stock dependency scripts: fsck uses the
        # virtual dev service, so the historical udev->runtime edge closes a
        # cycle even though the OpenWave-only service graph is acyclic.
        stock = ROOT / "third_party/openrc-0.64.1/init.d"
        sources = {name: (stock / f"{name}.in").read_text()
                   for name in ("fsck", "root", "localmount")}
        sources.update({name: (openrc / name).read_text()
                        for name in ("udevd", "openwave-runtime")})
        graph = VALIDATOR.dependency_graph(sources)
        self.assertIn("udevd", graph["fsck"])
        VALIDATOR.check_dependency_cycles(graph)
        sources["udevd"] = sources["udevd"].replace(
            "need devfs sysfs", "need openwave-runtime")
        with self.assertRaisesRegex(SystemExit, r"OpenRC dependency cycle:.*udevd"):
            VALIDATOR.check_dependency_cycles(VALIDATOR.dependency_graph(sources))

    def test_conditional_stock_dependency_is_not_invented(self):
        sources = {
            "clock": "depend() {\n if yesno $clock_adjfile; then\n use root\n fi\n}\n",
            "root": "depend() {\n use clock\n}\n",
        }
        graph = VALIDATOR.dependency_graph(sources)
        self.assertNotIn("root", graph["clock"])
        VALIDATOR.check_dependency_cycles(graph)


if __name__ == "__main__":
    unittest.main()
