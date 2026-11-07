#!/usr/bin/env python3
"""Fail-closed guest-side Waydroid boot evidence check (does not verify GUI)."""
import argparse
from pathlib import Path
import subprocess
import sys


def run(command, timeout=60):
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"{command[0]} unavailable or timed out: {exc}") from exc
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout.strip()


def enforcing():
    try:
        value = Path("/sys/fs/selinux/enforce").read_text().strip()
    except OSError as exc:
        raise RuntimeError("Cannot read enforcing state") from exc
    if value != "1":
        raise RuntimeError("OpenWave is not SELinux enforcing; actual value=" + value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--container", default="waydroid")
    parser.add_argument("--lxc-path", help="Directory containing the LXC container configuration")
    args = parser.parse_args()
    tests = []

    def check(name, fn):
        try:
            result = fn()
            print("PASS", name, result)
            tests.append((name, True))
        except RuntimeError as exc:
            print("FAIL", name, str(exc), file=sys.stderr)
            tests.append((name, False))

    check("SELinux enforcing before tests", enforcing)
    lxc_command = ["lxc-info", "-n", args.container, "-s"]
    if args.lxc_path:
        lxc_command.extend(["-P", args.lxc_path])

    def lxc_running():
        state = run(lxc_command)
        if "RUNNING" not in state.upper():
            raise RuntimeError("Container not running: " + state)
        return state

    check("LXC running", lxc_running)

    def android_property(key, expected):
        value = run(["waydroid", "shell", "getprop", key])
        if value != expected:
            raise RuntimeError(f"{key}={value!r}, expected {expected!r}")
        return f"{key}={value}"

    check("Android boot completed", lambda: android_property("sys.boot_completed", "1"))
    check("Android logd running", lambda: android_property("init.svc.logd", "running"))

    def logcat():
        output = run(["waydroid", "shell", "logcat", "-d", "-t", "10"])
        if not output or "logd is not running" in output.lower():
            raise RuntimeError("logcat returned no valid Android messages")
        return str(len(output.splitlines())) + " lines"

    check("Android logcat valid", logcat)

    def packages():
        output = run(["waydroid", "shell", "cmd", "package", "list", "packages"])
        count = sum(line.startswith("package:") for line in output.splitlines())
        if not count:
            raise RuntimeError("No Android packages returned")
        return str(count) + " packages"

    check("Android package manager", packages)
    check("SELinux enforcing after tests", enforcing)
    print(f"RESULT {sum(ok for _, ok in tests)}/{len(tests)} gates")
    print("NOT TESTED: Android SELinux process context and appd/Ocean Launcher shortcut GUI launch")
    return int(not all(ok for _, ok in tests))


if __name__ == "__main__":
    sys.exit(main())
