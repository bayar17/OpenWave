#!/usr/bin/env python3
"""Run candidate preflight/staging offline, stopping before any compilation."""

from pathlib import Path
import os
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[3]
BUILDER = ROOT / "userspace/openrc/build-openrc-candidate.sh"
PIN = "83fb7385f4e9115a1c804b1c035bfbad04ebc46a"
ENFORCING = "SELINUX=enforcing\nSELINUXTYPE=openwave\n"
PERMISSIVE = "SELINUX=permissive\nSELINUXTYPE=openwave\n"
MAKE_BARRIER_EXIT = 73


class CandidateFixture:
    """A tiny repository, with real shell copy/check behavior and no compiler."""

    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="openwave-candidate-staging-")
        self.root = Path(self.temporary.name)
        self.script = self.root / "userspace/openrc/build-openrc-candidate.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copy2(BUILDER, self.script)
        self.base = self.root / "rootfs"
        self.output = self.root / "out/candidate"
        self.authority = self.root / "userspace/security/selinux/config"
        self.base_config = self.base / "etc/selinux/config"
        self.tools = self.root / "fixture-tools"
        self.tools.mkdir()
        self.barrier = self.root / "make-attempted"
        for relative in (
            "toolchain/sysroot/x86_64-unknown-openwave",
            "third_party/openrc-0.64.1/.git",
            "third_party/busybox-1.37.0",
            "scripts",
        ):
            (self.root / relative).mkdir(parents=True, exist_ok=True)
        self.write("third_party/busybox-1.37.0/Makefile", "# fixture only\n")
        self.write("userspace/openrc/busybox.config", "# fixture only\n")
        self.write("rootfs/init", "fixture bootstrap\n")
        self.write("rootfs/System/Applications/com.openwave.shell/bin/ocean-launcher",
                   "fixture Ocean\n")
        self.write("rootfs/etc/selinux/config", PERMISSIVE)
        self.write("userspace/security/selinux/config", ENFORCING)
        self.write("fixture-tools/git", textwrap.dedent(f"""\
            #!/bin/sh
            case "$3" in
                rev-parse) printf '%s\\n' '{PIN}' ;;
                status) exit 0 ;;
                *) echo 'Unexpected fixture git invocation' >&2; exit 99 ;;
            esac
            """), executable=True)
        self.write("fixture-tools/make", textwrap.dedent("""\
            #!/bin/sh
            printf '%s\\n' "$*" > "$OPENWAVE_FIXTURE_MAKE_MARKER"
            echo 'Fixture barrier: no compilation permitted' >&2
            exit 73
            """), executable=True)
        # The real policy compiler is intentionally replaced in this isolated
        # repository. Only its documented staging output contract is exercised.
        self.write("scripts/build-openwave-selinux.py", textwrap.dedent("""\
            import argparse
            import os
            from pathlib import Path

            parser = argparse.ArgumentParser()
            parser.add_argument('--sdk')
            parser.add_argument('--target')
            parser.add_argument('--stage', type=Path)
            parser.add_argument('--output', type=Path)
            args = parser.parse_args()
            root = Path(__file__).resolve().parents[1]
            destination = args.stage / 'etc/selinux'
            destination.mkdir(parents=True)
            config = (root / 'userspace/security/selinux/config').read_bytes()
            if os.environ.get('OPENWAVE_FIXTURE_BAD_POLICY_CONFIG') == '1':
                config = b'SELINUX=permissive\\nSELINUXTYPE=openwave\\n'
            (destination / 'config').write_bytes(config)
            policy = destination / 'openwave/policy/policy.35'
            policy.parent.mkdir(parents=True)
            policy.write_bytes(b'fixture policy, never executable')
            """))

    def write(self, relative, content, executable=False):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        path.chmod(0o755 if executable else 0o644)
        return path

    def run(self, **overrides):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith("OPENWAVE_OPENRC_")}
        env.update({
            "PATH": f"{self.tools}{os.pathsep}{os.defpath}",
            "OPENWAVE_OPENRC_OUTPUT": str(self.output),
            "OPENWAVE_FIXTURE_MAKE_MARKER": str(self.barrier),
        })
        env.update(overrides)
        return subprocess.run(["bash", str(self.script)], cwd=self.root, env=env,
                              capture_output=True, text=True, timeout=10)

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.temporary.cleanup()


class CandidateStagingTests(unittest.TestCase):
    def test_default_permissive_base_is_unchanged_and_candidate_is_enforcing(self):
        with CandidateFixture() as fixture:
            original = fixture.base_config.read_bytes()
            original_mode = stat.S_IMODE(fixture.base_config.stat().st_mode)
            result = fixture.run()
            self.assertEqual(result.returncode, MAKE_BARRIER_EXIT, result.stderr)
            self.assertTrue(fixture.barrier.is_file())
            self.assertEqual(fixture.base_config.read_bytes(), original)
            self.assertEqual(stat.S_IMODE(fixture.base_config.stat().st_mode), original_mode)
            candidate_config = fixture.output / "rootfs/etc/selinux/config"
            self.assertEqual(candidate_config.read_bytes(), fixture.authority.read_bytes())
            self.assertEqual(candidate_config.read_text(), ENFORCING)
            self.assertIn("base root unchanged", result.stdout)
            self.assertFalse(any(fixture.output.glob("*.img")))

    def test_absent_policy_parents_are_created_only_in_candidate(self):
        with CandidateFixture() as fixture:
            shutil.rmtree(fixture.base / "etc/selinux")
            result = fixture.run()
            self.assertEqual(result.returncode, MAKE_BARRIER_EXIT, result.stderr)
            self.assertFalse((fixture.base / "etc/selinux").exists())
            self.assertEqual((fixture.output / "rootfs/etc/selinux/config").read_text(),
                             ENFORCING)

    def test_missing_relative_base_reports_path_before_creating_output(self):
        with CandidateFixture() as fixture:
            result = fixture.run(OPENWAVE_OPENRC_BASE_ROOT="prepared-enforcing-rootfs")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Base root directory does not exist: prepared-enforcing-rootfs",
                          result.stderr)
            self.assertIn("Omit OPENWAVE_OPENRC_BASE_ROOT", result.stderr)
            self.assertFalse(fixture.output.exists())
            self.assertFalse(fixture.barrier.exists())

    def test_non_enforcing_authority_is_rejected_before_output_changes(self):
        with CandidateFixture() as fixture:
            fixture.authority.write_text(PERMISSIVE)
            result = fixture.run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("authoritative SELinux config is not exactly enforcing", result.stderr)
            self.assertFalse(fixture.output.exists())
            self.assertFalse(fixture.barrier.exists())
            self.assertEqual(fixture.base_config.read_text(), PERMISSIVE)

    def test_symlinked_policy_directory_is_rejected_without_touching_target(self):
        for relative in ("etc", "etc/selinux", "etc/selinux/openwave",
                         "etc/selinux/openwave/policy",
                         "etc/selinux/openwave/contexts",
                         "etc/selinux/openwave/contexts/files"):
            with self.subTest(relative=relative), CandidateFixture() as fixture:
                path = fixture.base / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                outside = fixture.root / "outside-policy-directory"
                if path.exists():
                    path.rename(outside)
                else:
                    outside.mkdir()
                sentinel = outside / "sentinel"
                sentinel.write_text("must remain unchanged\n")
                path.symlink_to(outside, target_is_directory=True)
                result = fixture.run()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Refusing unsafe SELinux staging directory", result.stderr)
                self.assertEqual(sentinel.read_text(), "must remain unchanged\n")
                self.assertFalse(fixture.output.exists())
                self.assertFalse(fixture.barrier.exists())

    def test_symlinked_policy_file_is_rejected_without_touching_target(self):
        for relative in ("etc/selinux/config", "etc/selinux/openwave/policy/policy.35",
                         "etc/selinux/openwave/contexts/files/file_contexts",
                         "etc/selinux/openwave/contexts/files/file_contexts.bin"):
            with self.subTest(relative=relative), CandidateFixture() as fixture:
                path = fixture.base / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.unlink(missing_ok=True)
                outside = fixture.write("outside-policy-file", "must remain unchanged\n")
                path.symlink_to(outside)
                result = fixture.run()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Refusing unsafe SELinux staging file", result.stderr)
                self.assertEqual(outside.read_text(), "must remain unchanged\n")
                self.assertFalse(fixture.output.exists())
                self.assertFalse(fixture.barrier.exists())

    def test_policy_stage_with_wrong_config_is_rejected_before_compilation(self):
        with CandidateFixture() as fixture:
            result = fixture.run(OPENWAVE_FIXTURE_BAD_POLICY_CONFIG="1")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("differs from the authoritative enforcing config", result.stderr)
            self.assertFalse(fixture.barrier.exists())
            self.assertEqual(fixture.base_config.read_text(), PERMISSIVE)
            self.assertFalse(any(fixture.output.glob("*.img")))

    def test_existing_candidate_image_is_preserved(self):
        with CandidateFixture() as fixture:
            fixture.output.mkdir(parents=True)
            image = fixture.output / "OpenWave-openrc-candidate-v4.img"
            image.write_bytes(b'preexisting image must remain unchanged')
            result = fixture.run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Refusing to overwrite candidate image", result.stderr)
            self.assertEqual(image.read_bytes(), b'preexisting image must remain unchanged')
            self.assertFalse((fixture.output / "rootfs").exists())
            self.assertFalse(fixture.barrier.exists())


if __name__ == "__main__":
    unittest.main()
