#!/usr/bin/env python3
"""Offline fail-closed checks for the integration orchestrator (no QEMU needed)."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "openwave-waydroid-integration.py"
spec = importlib.util.spec_from_file_location("openwave_waydroid_integration", SCRIPT)
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)


class OrchestratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openwave-waydroid-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "out").mkdir()
        (self.root / "rootfs").mkdir()
        self.patch = mock.patch.object(integration, "ROOT", self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def args(self, command="audit", image=None, kernel=None):
        return SimpleNamespace(command=command, rootfs="rootfs", image=image, kernel=kernel,
                               seconds=60, image_size="6G", run_dir=None)

    def test_parent_directory_does_not_pass_output_containment(self):
        self.assertTrue(integration.safe_within(self.root / "out/candidate.img",
                                                self.root / "out"))
        self.assertFalse(integration.safe_within(self.root / "secrets/candidate.img",
                                                 self.root / "out"))

    def test_report_requires_all_release_gates(self):
        runner = integration.Runner(self.args())
        runner.gate("full_security_suite", "PASS", "fixture")
        runner.gate("security_release_gate", "PASS", "fixture")
        runner.gate("image_build", "PASS", "fixture")
        runner.gate("qemu_inputs_immutable", "PASS", "fixture")
        runner.gate("android_framework", "PASS", "fixture")
        self.assertFalse(runner.report())
        self.assertFalse(runner.record["release_ready"])
        self.assertEqual(runner.record["gates"]["android_shortcuts"]["status"],
                         "UNVERIFIED")

    def test_image_never_overwrites_existing_file(self):
        image = self.root / "out/immutable.img"
        image.write_bytes(b"known-good")
        runner = integration.Runner(self.args("image", image="out/immutable.img"))
        with mock.patch.object(runner, "test", return_value=True):
            self.assertFalse(runner.image_build())
        self.assertEqual(image.read_bytes(), b"known-good")
        self.assertEqual(runner.record["gates"]["image_build"]["status"], "BLOCKED")

    def test_qemu_requires_explicit_kernel_and_image(self):
        runner = integration.Runner(self.args("qemu"))
        self.assertFalse(runner.qemu())
        self.assertEqual(runner.record["gates"]["qemu"]["status"], "BLOCKED")

    def test_known_security_failure_blocks_image_creation(self):
        policy = self.root / "out/sdk-dependencies/x86_64-unknown-openwave/selinux-policy/compile-policy"
        policy.parent.mkdir(parents=True)
        policy.write_text("#!/bin/sh\nexit 0\n")
        policy.chmod(0o755)
        runner = integration.Runner(self.args("image"))
        def fake_run(name, *unused_args, **unused_kwargs):
            return runner.gate(name, "FAIL" if name == "full_security_suite" else "PASS",
                               "synthetic failure" if name == "full_security_suite" else "fixture")
        with mock.patch.object(runner, "run", side_effect=fake_run):
            self.assertFalse(runner.image_build())
        self.assertEqual(runner.record["gates"]["security_release_gate"]["status"],
                         "FAIL")
        self.assertEqual(runner.record["gates"]["image_build"]["status"], "BLOCKED")

    def test_external_rootfs_is_rejected(self):
        runner = integration.Runner(self.args())
        self.assertTrue(runner.rootfs == self.root / "rootfs")
        self.assertFalse(integration.safe_within(Path(self.temp.name).parent / "other",
                                                 self.root))


if __name__ == "__main__":
    unittest.main()
