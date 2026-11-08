#!/usr/bin/env python3
"""Fail-closed OpenWave Waydroid build/security/QEMU acceptance orchestrator.

Runs on the OpenWave build host. Never changes SELinux enforcement or overwrites
an existing image. Results are scoped to a new evidence directory under out/.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STATUS = ("PASS", "FAIL", "BLOCKED", "UNVERIFIED")
TRIPLE = "x86_64-unknown-openwave"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def command_text(argv):
    import shlex
    return shlex.join(str(x) for x in argv)


def safe_within(path, parent):
    resolved = path.resolve()
    return resolved == parent.resolve() or parent.resolve() in resolved.parents


class Runner:
    def __init__(self, args):
        self.args = args
        self.root = ROOT
        self.rootfs = (ROOT / args.rootfs).resolve()
        self.kernel = (ROOT / args.kernel).resolve() if args.kernel else None
        self.image = (ROOT / args.image).resolve() if args.image else None
        self.evidence_root = (ROOT / "out/waydroid-integration").resolve()
        if not safe_within(self.evidence_root, ROOT / "out"):
            raise ValueError("Refusing evidence outside OpenWave out/")
        self.evidence_root.mkdir(parents=True, exist_ok=True)
        if args.command == "report":
            if args.run_dir:
                chosen = (ROOT / args.run_dir).resolve()
                if not safe_within(chosen, self.evidence_root) or not chosen.is_dir():
                    raise ValueError("The requested report directory is not valid")
            else:
                candidates = sorted(p for p in self.evidence_root.iterdir()
                                    if p.is_dir() and (p / "result.json").is_file())
                if not candidates:
                    raise ValueError("No prior integration result exists")
                chosen = candidates[-1]
            self.out = chosen
            self.record = json.loads((chosen / "result.json").read_text())
        else:
            tag = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            self.out = self.evidence_root / f"{tag}-{os.getpid()}"
            self.out.mkdir(mode=0o700, exist_ok=False)
            (self.out / "logs").mkdir(mode=0o700)
            self.record = {"schema": 1, "mode": args.command, "root": str(ROOT),
                           "started_utc": datetime.now(timezone.utc).isoformat(),
                           "gates": {}, "inputs": {}, "commands": [],
                           "permissions_removed": [], "new_permissions": [],
                           "outstanding_avcs": [], "warnings": []}
            self.gate("android_shortcuts", "UNVERIFIED",
                      "No authenticated appd/Ocean Android launch evidence is available")
            self.gate("android_graphics", "UNVERIFIED",
                      "Not measured by the existing Waydroid serial test")

    def gate(self, name, status, detail):
        if status not in STATUS:
            raise ValueError("Invalid gate status")
        self.record["gates"][name] = {"status": status, "detail": str(detail)}
        print(f"{status:10s} {name}: {detail}", flush=True)
        return status == "PASS"

    def require(self, path, name, executable=False):
        if not path.is_file() or (executable and not os.access(path, os.X_OK)):
            return self.gate(name, "BLOCKED", f"Missing required file: {path}")
        return self.gate(name, "PASS", str(path))

    def run(self, name, argv, timeout=1800, cwd=None, env=None):
        log = self.out / "logs" / (name + ".log")
        argv = [str(x) for x in argv]
        info = {"name": name, "command": argv, "log": str(log.relative_to(self.out)),
                "timeout_seconds": timeout}
        self.record["commands"].append(info)
        if shutil.which(argv[0]) is None and not Path(argv[0]).is_file():
            return self.gate(name, "BLOCKED", "Executable missing: " + argv[0])
        print("RUN", command_text(argv), flush=True)
        start = time.monotonic()
        with log.open("w") as stream:
            stream.write("$ " + command_text(argv) + "\n")
            stream.flush()
            try:
                result = subprocess.run(argv, cwd=cwd or ROOT, env=env, timeout=timeout,
                                        stdout=stream, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL, check=False)
                info["returncode"] = result.returncode
                status = "PASS" if result.returncode == 0 else "FAIL"
                detail = f"exit={result.returncode}, log={log}"
            except subprocess.TimeoutExpired:
                info["timed_out"] = True
                status, detail = "FAIL", f"timed out after {timeout}s, log={log}"
            except OSError as error:
                status, detail = "BLOCKED", f"{error}, log={log}"
        info["duration_seconds"] = round(time.monotonic() - start, 3)
        return self.gate(name, status, detail)

    def audit(self):
        ok = True
        for name, path in (
            ("source_policy", ROOT / "userspace/security/selinux/openwave.cil"),
            ("file_contexts", ROOT / "userspace/security/selinux/file_contexts"),
            ("source_builder", ROOT / "scripts/build-openwave-selinux.py"),
            ("image_builder", ROOT / "scripts/openwave-label-ext4.py"),
            ("openrc_waydroid", ROOT / "userspace/openrc/etc/init.d/waydroid"),
            ("waydroid_shim", ROOT / "userspace/waydroid/waydroidd.c"),
            ("waydroid_qemu_runner", ROOT / "scripts/test-openwave-boot.py"),
        ):
            ok = self.require(path, name) and ok
        try:
            git = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                                 capture_output=True, text=True, timeout=10)
            self.record["inputs"]["git_head"] = git.stdout.strip()
            dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                                   check=True, capture_output=True, text=True, timeout=10)
            self.record["inputs"]["git_dirty"] = bool(dirty.stdout)
            self.gate("git_revision", "PASS", git.stdout.strip())
        except (OSError, subprocess.SubprocessError) as exc:
            ok = self.gate("git_revision", "BLOCKED", str(exc)) and ok
        for name, executable in (("python", "python3"), ("compiler", "gcc"),
                                 ("qemu", "qemu-system-x86_64"), ("fakeroot", "fakeroot"),
                                 ("e2fsck", "e2fsck")):
            present = shutil.which(executable)
            state = "PASS" if present else "BLOCKED"
            self.gate("tool_" + name, state, present or f"{executable} not installed")
        sdk = ROOT / "toolchain"
        self.gate("target_clang", "PASS" if (sdk / "bin/clang").is_file() else "BLOCKED",
                  str(sdk / "bin/clang"))
        self.gate("target_libselinux", "PASS" if (
            sdk / f"sysroot/{TRIPLE}/usr/lib64/libselinux.so.1").exists() else "BLOCKED",
            "Target sysroot libselinux.so.1")
        for name, candidate in (("system", self.rootfs / "usr/share/waydroid-extra/images/system.img"),
                                ("vendor", self.rootfs / "usr/share/waydroid-extra/images/vendor.img")):
            if candidate.is_file():
                self.record["inputs"][f"android_{name}"] = {
                    "path": str(candidate), "bytes": candidate.stat().st_size,
                    "sha256": digest(candidate)}
                self.gate(f"android_{name}_image", "PASS", str(candidate))
            else:
                self.gate(f"android_{name}_image", "BLOCKED", str(candidate))
        config = self.kernel.parent.parent.parent / ".config" if self.kernel else None
        if config and config.is_file():
            settings = config.read_text()
            required = ("CONFIG_SECURITY_SELINUX=y", "CONFIG_ANDROID_BINDER_IPC=y",
                        "CONFIG_EROFS_FS=y", "CONFIG_BLK_DEV_LOOP=y")
            missing = [x for x in required if x not in settings]
            self.gate("kernel_features", "PASS" if not missing else "BLOCKED",
                      "Requirements checked" if not missing else f"Missing: {missing}")
        else:
            self.gate("kernel_features", "UNVERIFIED", "Supply --kernel and verify matching .config")
        if self.kernel and self.kernel.is_file():
            self.record["inputs"]["kernel_sha256"] = digest(self.kernel)
            self.gate("kernel_image", "PASS", str(self.kernel))
        else:
            self.gate("kernel_image", "BLOCKED", "No verified Waydroid-capable kernel selected")
        source = ROOT / "third_party/waydroid"
        if (source / ".git").exists() or (source / ".git").is_file():
            try:
                revision = subprocess.check_output(
                    ["git", "-C", str(source), "rev-parse", "HEAD"], text=True, timeout=10).strip()
                expected = "5b7e2e71be3f6bfaaaab3b461251dacaf1ce4991"
                self.record["inputs"]["waydroid_upstream"] = revision
                self.gate("waydroid_pinned", "PASS" if revision == expected else "FAIL",
                          f"Found {revision}, expected {expected}")
            except (OSError, subprocess.SubprocessError) as exc:
                self.gate("waydroid_pinned", "BLOCKED", str(exc))
        else:
            self.gate("waydroid_pinned", "BLOCKED", "Pinned upstream checkout missing")
        policy = ROOT / "userspace/security/selinux/openwave.cil"
        if policy.is_file():
            source_text = policy.read_text()
            broad = bool(re.search(
                r"\(allow\s+openwave_waydroid_manager_t\s+self\s+"
                r"\(capability\s+\(sys_admin\)\)\)", source_text, re.S))
            if broad:
                self.record["warnings"].append(
                    "Waydroid manager still has CAP_SYS_ADMIN (diagnostic grant)")
            self.gate("manager_no_sys_admin", "FAIL" if broad else "PASS",
                      "Diagnostic CAP_SYS_ADMIN grant still present" if broad
                      else "No direct manager sys_admin allow found")
            self.record["inputs"]["policy_source_sha256"] = digest(policy)
        return ok

    def build(self):
        if not self.require(ROOT / "build-openwave", "build_tool", executable=True):
            return False
        if not self.run("build_selinux", [ROOT / "build-openwave", "selinux"], timeout=3600):
            return False
        if not self.run("build_userspace", [ROOT / "build-openwave", "userspace"], timeout=7200):
            return False
        return self.run("build_rootfs", [ROOT / "build-openwave", "rootfs"], timeout=1800)

    def test(self):
        policy = ROOT / f"out/sdk-dependencies/{TRIPLE}/selinux-policy"
        if not self.require(policy / "compile-policy", "compiled_policy_tool", executable=True):
            return False
        env = dict(os.environ, OPENWAVE_POLICY_BUILD=str(policy))
        checks = [
            ("effective_file_contexts", [sys.executable,
                ROOT / "userspace/security/selinux/tests/test_file_contexts.py"], 120),
            ("full_security_suite", [sys.executable,
                ROOT / "userspace/security/selinux/tests/test_policy.py", "-v"], 900),
            ("upstream_package_suite", [sys.executable,
                ROOT / "userspace/waydroid/tests/test_package.py"], 300),
        ]
        good = True
        for name, argv, timeout in checks:
            good = self.run(name, argv, timeout=timeout, env=env) and good
        if self.rootfs.is_dir():
            good = self.run("policy_manifest_verify", [
                sys.executable, ROOT / "scripts/build-openwave-selinux.py",
                "--verify-only", "--sdk", ROOT / "toolchain",
                "--target", TRIPLE, "--stage", self.rootfs], timeout=120) and good
            good = self.run("openrc_validate", [
                sys.executable, ROOT / "userspace/openrc/validate_openrc.py"
            ], timeout=180, env=dict(env, OPENWAVE_OPENRC_ROOT=str(self.rootfs))) and good
        else:
            good = self.gate("rootfs_tests", "BLOCKED",
                             f"rootfs unavailable: {self.rootfs}") and good
        self.gate("security_release_gate", "PASS" if good else "FAIL",
                  "Full suite and staged policy checks" if good
                  else "At least one mandatory test failed or was blocked")
        return good

    def image_build(self):
        if not self.test():
            self.gate("image_build", "BLOCKED", "Full security test gate did not pass")
            return False
        if not self.rootfs.is_dir() or self.rootfs.is_symlink():
            return self.gate("image_build", "BLOCKED", "Missing or symlinked rootfs")
        target = self.image or self.out / "OpenWave-waydroid-enforcing.img"
        if not safe_within(target, ROOT / "out") or target.exists() or target.is_symlink():
            return self.gate("image_build", "BLOCKED",
                             "Image must be a fresh, nonexistent path under out/")
        if not re.fullmatch(r"[1-9][0-9]*[GM]", self.args.image_size):
            return self.gate("image_build", "BLOCKED", "Invalid --image-size")
        env = dict(os.environ, OPENWAVE_IMAGE_SIZE=self.args.image_size)
        success = self.run("image_build", [
            ROOT / "build-openwave", "image", self.rootfs, target], timeout=3600, env=env)
        if success and target.is_file():
            self.image = target
            self.record["inputs"]["image"] = str(target)
            self.record["inputs"]["image_sha256"] = digest(target)
            return True
        return False

    def qemu(self):
        if not self.image or not self.image.is_file():
            return self.gate("qemu", "BLOCKED", "Supply --image or build a new image in all")
        if not self.kernel or not self.kernel.is_file():
            return self.gate("qemu", "BLOCKED", "Supply --kernel pointing to a Waydroid-capable bzImage")
        if not (os.getenv("DISPLAY") or os.getenv("WAYLAND_DISPLAY")):
            return self.gate("qemu", "BLOCKED",
                             "Existing QEMU GTK/VirGL test requires an active graphical display")
        target = self.out / "qemu"
        if target.exists():
            return self.gate("qemu", "BLOCKED", "QEMU evidence directory already exists")
        before = {"image": digest(self.image), "kernel": digest(self.kernel)}
        self.record["inputs"]["qemu_inputs"] = before
        called = self.run("qemu_runner", [sys.executable,
            ROOT / "scripts/test-openwave-boot.py",
            "--waydroid", "--mode", "enforcing", "--seconds", str(self.args.seconds),
            "--image", self.image, "--kernel", self.kernel, "--output", target],
            timeout=self.args.seconds + 100)
        result_path = target / "result.json"
        waydroid_path = target / "waydroid.json"
        if not result_path.is_file() or not waydroid_path.is_file():
            self.gate("android_framework", "BLOCKED",
                      "No QEMU result.json and waydroid.json evidence")
            return False
        try:
            result = json.loads(result_path.read_text())
            waydroid = json.loads(waydroid_path.read_text())
        except (ValueError, OSError) as exc:
            return self.gate("android_framework", "FAIL", str(exc))
        unchanged = before == {"image": digest(self.image),
                               "kernel": digest(self.kernel)}
        inputs_ok = (unchanged and result.get("inputs_unchanged") is True
                     and result.get("mode") == "enforcing"
                     and result.get("kernel_panic") is False)
        self.gate("qemu_inputs_immutable", "PASS" if inputs_ok else "FAIL",
                  "Image/kernel unchanged and enforcing mode" if inputs_ok
                  else "Image/kernel or QEMU integrity requirement failed")
        stages = waydroid.get("stages", {})
        for name in ("container", "servicemanager", "zygote", "system_server",
                     "boot_completed", "package_manager"):
            item = stages.get(name)
            self.gate("android_" + name, item if item in STATUS else "UNVERIFIED",
                      f"QEMU stage: {item!r}")
        framework = (called and inputs_ok
                     and waydroid.get("diagnostics_complete") is True
                     and waydroid.get("framework_passed") is True
                     and result.get("waydroid_framework_passed") is True)
        if stages.get("container") != "PASS":
            self.gate("android_framework", "BLOCKED", "LXC container not RUNNING")
        else:
            self.gate("android_framework", "PASS" if framework else "FAIL",
                      "Verified Android framework boot" if framework
                      else "Framework acceptance predicates not met")
        serial = target / "serial.log"
        if serial.is_file():
            self.record["outstanding_avcs"] = [
                line.strip()[:1500] for line in serial.read_text(errors="replace").splitlines()
                if "avc:  denied" in line.lower() or "avc: denied" in line.lower()
            ][:200]
        self.gate("android_appd_shortcuts", "UNVERIFIED",
                  "QEMU Waydroid probe does not test authenticated appd shortcut launch")
        return framework

    def report(self):
        self.record["finished_utc"] = datetime.now(timezone.utc).isoformat()
        release_gates = ("full_security_suite", "security_release_gate",
                         "image_build", "qemu_inputs_immutable", "android_framework",
                         "android_appd_shortcuts")
        self.record["release_ready"] = all(self.record["gates"].get(x, {}).get("status") == "PASS"
                                            for x in release_gates)
        (self.out / "result.json").write_text(json.dumps(self.record, indent=2, sort_keys=True) + "\n")
        lines = ["# OpenWave Waydroid integration report", "",
                 f"Source: {self.record['inputs'].get('git_head', 'UNVERIFIED')}",
                 f"Release-ready: {'YES' if self.record['release_ready'] else 'NO'}", "",
                 "| Gate | Status | Details |", "|---|---|---|"]
        for name, gate in sorted(self.record["gates"].items()):
            details = str(gate["detail"]).replace("|", "\\|").replace("\n", " ")
            lines.append(f"| {name} | {gate['status']} | {details} |")
        lines.extend(["", "## Remaining AVCs", ""])
        avcs = self.record.get("outstanding_avcs") or []
        lines.extend(f"- {line}" for line in avcs)
        if not avcs:
            lines.append("No AVC evidence collected; this does not prove there are no denials.")
        lines.extend(["", "## Scope", "",
                      "SELinux policy is never switched to permissive by this runner.",
                      "Android GUI shortcuts are UNVERIFIED without a separate authenticated GUI test.",
                      "No diagnostic permissions have been removed by this runner."])
        (self.out / "report.md").write_text("\n".join(lines) + "\n")
        print("Evidence:", self.out, flush=True)
        print("Release-ready:", self.record["release_ready"], flush=True)
        return self.record["release_ready"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("audit", "build", "test", "image", "qemu", "report", "all"))
    parser.add_argument("--rootfs", default="rootfs",
                        help="Existing OpenWave staged runtime root (default: rootfs)")
    parser.add_argument("--kernel", help="Explicit Waydroid-capable bzImage")
    parser.add_argument("--image", help="Existing image for qemu; new image path for image/all")
    parser.add_argument("--image-size", default="6G",
                        help="Fresh rootfs image size (default: 6G)")
    parser.add_argument("--seconds", type=int, default=240,
                        help="QEMU runtime from 10 to 600 seconds")
    parser.add_argument("--run-dir", help="Existing evidence directory, for report only")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error("--seconds must be 10..600")
    if args.run_dir and args.command != "report":
        parser.error("--run-dir is only supported with report")
    try:
        runner = Runner(args)
        if args.command == "report":
            return 0 if runner.report() else 1
        success = False
        try:
            if args.command == "audit":
                success = runner.audit()
            elif args.command == "build":
                success = runner.build()
            elif args.command == "test":
                success = runner.test()
            elif args.command == "image":
                success = runner.image_build()
            elif args.command == "qemu":
                success = runner.qemu()
            else:
                success = (runner.audit() and runner.build() and
                           runner.image_build() and runner.qemu())
                if not success:
                    runner.gate("all", "BLOCKED", "Stopped at the first mandatory failed gate")
        finally:
            release = runner.report()
        return 0 if success and (args.command != "all" or release) else 1
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Integration runner failed closed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
