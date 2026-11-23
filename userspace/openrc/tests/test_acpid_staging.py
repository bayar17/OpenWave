#!/usr/bin/env python3
"""Exercise acpid staging contracts with tiny offline shell-tool fixtures.

The real builders perform all control flow, copying, checks and installation.
Configure/make/readelf stand-ins prevent native compilation, network access,
image creation or any interaction with the production rootfs.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[3]
OPENRC_BUILDER = ROOT / "userspace/openrc/build-openrc-candidate.sh"
ACPID_BUILDER = ROOT / "userspace/openrc/build-acpid-candidate.sh"


class StageFixture:
    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="openwave-acpid-stage-")
        self.root = Path(self.temporary.name)
        self.output = self.root / "out/acpid"
        self.tools = self.root / "fixture-tools"
        self.tools.mkdir()
        self.script = self.root / "userspace/openrc/build-acpid-candidate.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copy2(ACPID_BUILDER, self.script)
        for relative in ("etc/init.d/acpid", "etc/acpi/events/power"):
            destination = self.script.parent / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "userspace/openrc" / relative, destination)
        (self.root / "toolchain/sysroot/x86_64-unknown-openwave").mkdir(parents=True)
        archive = self.output / "download/acpid-fixture.tar.xz"
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"fixture archive; the source is already extracted\n")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.write("userspace/openrc/acpid-source.env", (
            "ACPID_VERSION=fixture\n"
            "ACPID_SOURCE_URL=https://invalid.example/never-download\n"
            f"ACPID_SOURCE_SHA256={digest}\n"
        ))
        self.source = self.output / "source/acpid-fixture"
        self.write("out/acpid/source/acpid-fixture/configure.ac",
                   "AC_INIT([acpid], [fixture])\n")
        self.write("out/acpid/source/acpid-fixture/configure", """\
            #!/bin/sh
            printf '%s|%s|%s\\n' "$PWD" "${CC:-host-default}" "$*" >> "$OPENWAVE_FIXTURE_CONFIGURE_LOG"
            """, executable=True)
        self.write("fixture-tools/make", """\
            #!/bin/sh
            test "$1" = -C || exit 91
            printf '%s\\n' '#!/bin/sh' 'exit 0' > "$2/acpid"
            chmod 0755 "$2/acpid"
            """, executable=True)
        self.write("fixture-tools/curl", "#!/bin/sh\nexit 92\n", executable=True)
        self.write("userspace/openrc/tests/test_acpid_integration.py", """\
            import pathlib
            import sys
            assert sys.argv[1] == '--acpid'
            assert pathlib.Path(sys.argv[2]).is_file()
            """)
        self.write("toolchain/bin/llvm-readelf", """\
            #!/bin/sh
            test -x "$2" || exit 93
            case "$1" in
                -h)
                    if test "${OPENWAVE_FIXTURE_WRONG_ELF:-0}" = 1; then
                        echo 'Machine: AArch64'
                    else
                        echo 'Machine: Advanced Micro Devices X86-64'
                    fi ;;
                -l) echo '[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]' ;;
                -d) echo 'NEEDED libc.so.6' ;;
                *) exit 94 ;;
            esac
            """, executable=True)
        self.write("build-openwave", """\
            #!/bin/sh
            : > "$OPENWAVE_FIXTURE_PACK_MARKER"
            exit 95
            """, executable=True)
        self.pack_marker = self.root / "packing-attempted"
        self.configure_log = self.root / "configure.log"

    def write(self, relative, content, executable=False):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(content))
        path.chmod(0o755 if executable else 0o644)
        return path

    def environment(self, **overrides):
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("OPENWAVE_ACPID_", "OPENWAVE_FIXTURE_"))}
        environment.update({
            "PATH": f"{self.tools}{os.pathsep}{os.defpath}",
            "OPENWAVE_ACPID_OUTPUT": str(self.output),
            # Stage-only must not require any pre-existing image/rootfs.
            "OPENWAVE_ACPID_BASE_ROOT": str(self.root / "missing-base"),
            "OPENWAVE_FIXTURE_CONFIGURE_LOG": str(self.configure_log),
            "OPENWAVE_FIXTURE_PACK_MARKER": str(self.pack_marker),
        })
        environment.update(overrides)
        return environment

    def run(self, *arguments, **overrides):
        return subprocess.run(["bash", str(self.script), *arguments], cwd=self.root,
                              env=self.environment(**overrides), text=True,
                              capture_output=True, timeout=10)

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.temporary.cleanup()


class AcpidStageOnlyTests(unittest.TestCase):
    def test_real_stage_only_installs_daemon_service_rule_and_runlevel_without_image(self):
        with StageFixture() as fixture:
            existing = fixture.write("out/acpid/rootfs/sentinel", "preserve prior evidence\n")
            production = fixture.write("rootfs/sentinel", "preserve production\n")
            source_before = {path.name: path.read_bytes()
                             for path in fixture.source.iterdir() if path.is_file()}
            result = fixture.run("--stage-only")
            self.assertEqual(result.returncode, 0, result.stderr)
            stage = fixture.output / "stage-target"
            daemon = stage / "usr/sbin/acpid"
            service = stage / "etc/init.d/acpid"
            rule = stage / "etc/acpi/events/power"
            self.assertTrue(os.access(daemon, os.X_OK))
            self.assertEqual(stat.S_IMODE(daemon.stat().st_mode), 0o755)
            self.assertEqual(service.read_bytes(), (ROOT / "userspace/openrc/etc/init.d/acpid").read_bytes())
            self.assertEqual(rule.read_bytes(), (ROOT / "userspace/openrc/etc/acpi/events/power").read_bytes())
            self.assertEqual(stat.S_IMODE(rule.stat().st_mode), 0o644)
            self.assertEqual(os.readlink(stage / "etc/runlevels/boot/acpid"), "/etc/init.d/acpid")
            self.assertEqual(existing.read_text(), "preserve prior evidence\n")
            self.assertEqual(production.read_text(), "preserve production\n")
            self.assertFalse(fixture.pack_marker.exists())
            self.assertFalse(list(fixture.output.glob("*.img")))
            self.assertIn("target stage=", result.stdout)
            source_after = {path.name: path.read_bytes()
                            for path in fixture.source.iterdir() if path.is_file()}
            self.assertEqual(source_after, source_before)
            configure = fixture.configure_log.read_text().splitlines()
            self.assertEqual(len(configure), 2)
            self.assertIn("--host=x86_64-linux-gnu", configure[1])
            self.assertIn("--target=x86_64-unknown-openwave", configure[1])

    def test_wrong_target_architecture_is_rejected_without_packing(self):
        with StageFixture() as fixture:
            result = fixture.run("--stage-only", OPENWAVE_FIXTURE_WRONG_ELF="1")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(fixture.pack_marker.exists())
            self.assertNotIn("target stage=", result.stdout)

    def test_full_builder_still_requires_its_base_before_any_build(self):
        with StageFixture() as fixture:
            result = fixture.run()
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(fixture.configure_log.exists())
            self.assertFalse(fixture.pack_marker.exists())

    def test_unknown_argument_is_rejected_before_staging(self):
        with StageFixture() as fixture:
            result = fixture.run("--stage-only", "--unexpected")
            self.assertEqual(result.returncode, 2)
            self.assertIn("Usage:", result.stderr)
            self.assertFalse(fixture.configure_log.exists())
            self.assertFalse(fixture.pack_marker.exists())


class AcpidMainBuilderIntegrationTests(unittest.TestCase):
    """Execute the main builder's actual acpid block and boot-runlevel loop."""

    @staticmethod
    def script_fragment():
        script = OPENRC_BUILDER.read_text()
        begin = script.index("stage_acpid() {")
        end = script.index("\nCLANG=", begin)
        boot_end_text = 'done < "$ROOT/userspace/openrc/boot-services.txt"'
        boot_end = script.index(boot_end_text) + len(boot_end_text)
        boot_begin = script.rfind("while read -r service; do", end, boot_end)
        if boot_begin == -1:
            raise AssertionError("main builder has no boot-services installation loop")
        return script[begin:end] + "\n" + script[boot_begin:boot_end]

    def exercise(self, omitted="", helper_status="0"):
        with StageFixture() as fixture:
            # Replace only the delegated component build. Main builder's real
            # invocation, result checks, metadata-preserving overlay and
            # boot-services loop all execute unchanged from current source.
            fixture.write("userspace/openrc/build-acpid-candidate.sh", """\
                #!/usr/bin/env bash
                set -Eeuo pipefail
                printf '%s\\n' "$@" > "$OPENWAVE_FIXTURE_HELPER_ARGUMENTS"
                printf '%s\\n' "$OPENWAVE_ACPID_OUTPUT" > "$OPENWAVE_FIXTURE_HELPER_OUTPUT"
                [[ "$#" == 1 && "$1" == --stage-only ]] || exit 96
                [[ "$OPENWAVE_FIXTURE_HELPER_STATUS" == 0 ]] || exit "$OPENWAVE_FIXTURE_HELPER_STATUS"
                stage="$OPENWAVE_ACPID_OUTPUT/stage-target"
                mkdir -p "$stage/usr/sbin" "$stage/etc/init.d" "$stage/etc/acpi/events" "$stage/etc/runlevels/boot"
                if [[ "$OPENWAVE_FIXTURE_OMIT" != binary ]]; then
                    printf '%s\\n' '#!/bin/sh' 'exit 0' > "$stage/usr/sbin/acpid"
                    chmod 0755 "$stage/usr/sbin/acpid"
                fi
                if [[ "$OPENWAVE_FIXTURE_OMIT" != service ]]; then
                    cp "$ROOT/userspace/openrc/etc/init.d/acpid" "$stage/etc/init.d/acpid"
                    chmod 0755 "$stage/etc/init.d/acpid"
                fi
                if [[ "$OPENWAVE_FIXTURE_OMIT" != rule ]]; then
                    cp "$ROOT/userspace/openrc/etc/acpi/events/power" "$stage/etc/acpi/events/power"
                fi
                if [[ "$OPENWAVE_FIXTURE_OMIT" == bootlink ]]; then
                    ln -s /etc/init.d/wrong "$stage/etc/runlevels/boot/acpid"
                else
                    ln -s /etc/init.d/acpid "$stage/etc/runlevels/boot/acpid"
                fi
                """)
            shutil.copy2(ROOT / "userspace/openrc/boot-services.txt",
                         fixture.root / "userspace/openrc/boot-services.txt")
            candidate = fixture.root / "out/candidate/rootfs"
            candidate.mkdir(parents=True)
            sentinel = fixture.write("rootfs/sentinel", "production untouched\n")
            shell = (
                "set -Eeuo pipefail\n"
                'OUTPUT="$ROOT/out/candidate"\n'
                'CANDIDATE_ROOT="$OUTPUT/rootfs"\n'
                + self.script_fragment()
                + '\n: > "$OPENWAVE_FIXTURE_PACK_MARKER"\n'
            )
            environment = fixture.environment(
                ROOT=str(fixture.root),
                OPENWAVE_FIXTURE_OMIT=omitted,
                OPENWAVE_FIXTURE_HELPER_STATUS=helper_status,
                OPENWAVE_FIXTURE_HELPER_ARGUMENTS=str(fixture.root / "helper-args"),
                OPENWAVE_FIXTURE_HELPER_OUTPUT=str(fixture.root / "helper-output"),
            )
            result = subprocess.run(["bash", "-c", shell], cwd=fixture.root,
                                    env=environment, text=True, capture_output=True,
                                    timeout=10)
            self.assertEqual((fixture.root / "helper-args").read_text(), "--stage-only\n")
            self.assertEqual((fixture.root / "helper-output").read_text().strip(),
                             str(fixture.root / "out/candidate/acpid"))
            self.assertEqual(sentinel.read_text(), "production untouched\n")
            if omitted or helper_status != "0":
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(fixture.pack_marker.exists())
                self.assertFalse((candidate / "usr/sbin/acpid").exists())
                self.assertFalse((candidate / "etc/init.d/acpid").exists())
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(fixture.pack_marker.exists())
                self.assertTrue(os.access(candidate / "usr/sbin/acpid", os.X_OK))
                self.assertEqual((candidate / "etc/init.d/acpid").read_bytes(),
                                 (ROOT / "userspace/openrc/etc/init.d/acpid").read_bytes())
                self.assertEqual((candidate / "etc/acpi/events/power").read_bytes(),
                                 (ROOT / "userspace/openrc/etc/acpi/events/power").read_bytes())
                self.assertEqual(os.readlink(candidate / "etc/runlevels/boot/acpid"),
                                 "/etc/init.d/acpid")
                self.assertEqual(os.readlink(candidate / "etc/runlevels/boot/openwave-runtime"),
                                 "/etc/init.d/openwave-runtime")
            return result

    def test_main_builder_consumes_stage_and_enables_acpid_without_modifying_production(self):
        self.exercise()

    def test_component_build_failure_stops_before_overlay_and_packing(self):
        result = self.exercise(helper_status="67")
        self.assertEqual(result.returncode, 67)

    def test_missing_or_invalid_stage_artifacts_stop_before_overlay_and_packing(self):
        for omitted in ("binary", "service", "rule", "bootlink"):
            with self.subTest(omitted=omitted):
                self.exercise(omitted=omitted)


if __name__ == "__main__":
    unittest.main()
