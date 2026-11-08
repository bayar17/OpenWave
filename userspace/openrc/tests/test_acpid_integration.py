#!/usr/bin/env python3
"""Exercise upstream acpid's real event parser and action path without a guest."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
RULE = ROOT / "userspace/openrc/etc/acpi/events/power"
SERVICE = ROOT / "userspace/openrc/etc/init.d/acpid"
EVENT = "button/power PBTN 00000080 00000000"


def wait_for(path: Path, process: subprocess.Popen[bytes], timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            raise AssertionError(f"acpid exited early with status {process.returncode}")
        time.sleep(0.02)
    raise AssertionError("upstream acpid did not invoke the configured ACPI action")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--acpid", type=Path, required=True)
    args = parser.parse_args()

    service = SERVICE.read_text()
    rule_text = RULE.read_text()
    if 'command="/usr/sbin/acpid"' not in service or 'command_args_foreground="-n -f"' not in service:
        raise SystemExit("OpenRC service must run upstream acpid in foreground netlink/input mode")
    if "need devfs" not in service or "use sysfs" not in service:
        raise SystemExit("OpenRC acpid service lacks device filesystem dependencies")
    if "event=button/power" not in rule_text or "action=/sbin/openrc-shutdown -p now" not in rule_text:
        raise SystemExit("ACPI power rule does not map to the required OpenRC poweroff command")

    with tempfile.TemporaryDirectory(prefix="openwave-acpid-test-") as temporary:
        base = Path(temporary)
        event_fifo = base / "events.fifo"
        os.mkfifo(event_fifo)
        config = base / "rules"
        config.mkdir()
        marker = base / "action-called"
        action = base / "fake-openrc-shutdown"
        action.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" > \"$OPENWAVE_ACPID_TEST_MARKER\"\n")
        action.chmod(0o755)
        (config / "power").write_text(
            "event=button/power\n"
            f"action={action} -p now\n"
        )
        environment = os.environ.copy()
        environment["OPENWAVE_ACPID_TEST_MARKER"] = str(marker)
        process = subprocess.Popen(
            [str(args.acpid), "-f", "-l", "-e", str(event_fifo), "-c", str(config),
             "-S", "-L", str(base / "lock")],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            env=environment,
        )
        writer = None
        try:
            # Opening the FIFO pairs with acpid's event-file reader. Keeping it
            # open avoids a synthetic EOF being mistaken for a daemon failure.
            writer = event_fifo.open("w")
            writer.write(EVENT + "\n")
            writer.flush()
            wait_for(marker, process)
            if marker.read_text().strip() != "-p now":
                raise AssertionError(f"wrong action arguments: {marker.read_text()!r}")
        finally:
            if writer is not None:
                writer.close()
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if process.returncode not in (0, -signal.SIGTERM):
            stderr = process.stderr.read().decode(errors="replace") if process.stderr else ""
            raise AssertionError(f"acpid exited with {process.returncode}: {stderr}")

    print("ACPID_EVENT_TEST PASS upstream_rule_parser action_invoked button/power -> -p now")


if __name__ == "__main__":
    main()
