"""Fail-closed parser for authenticated appd quit-channel QEMU acceptance.

A process exit or APP_SMOKE PASS alone cannot certify the private quit path:
appd may SIGTERM the process when IPC fails. Require explicit application-
specific quit ACKs, clean exits and the guest's checked summary.
"""
import re

ACK = re.compile(r"APP_TEST CONTROL_ACK (com\.openwave\.[a-z0-9-]+) pid=([1-9][0-9]*) authenticated response and normal exit")
SUMMARY = re.compile(r"APP_CONTROL_PASS authenticated quit acknowledgements and clean exits checks=([1-9][0-9]*)")
EXPECTED = frozenset({"com.openwave.files", "com.openwave.photos", "com.openwave.ocean"})


def evaluate(serial: str) -> dict:
    serial = serial.replace("\r\n", "\n").replace("\r", "\n").replace("\x00", "")
    matches = [(m.group(1), int(m.group(2))) for m in ACK.finditer(serial)]
    passes = SUMMARY.findall(serial)
    ids = {name for name, _ in matches}
    unique = len(matches) == len(set(matches))
    count = int(passes[0]) if len(passes) == 1 else 0
    passed = (
        len(passes) == 1 and unique and count == len(matches)
        and count >= len(EXPECTED) and EXPECTED <= ids
        and "APP_CONTROL_FAIL" not in serial
        and "APP_TEST CONTROL_FAIL" not in serial
        and "APP_SMOKE PASS confined live app launched through Ocean" in serial
        and "OPENWAVE_APP_SMOKE_END" in serial
    )
    return {
        "schema": 1,
        "passed": passed,
        "quit_ack_count": len(matches),
        "declared_check_count": count,
        "app_ids": sorted(ids),
        "required_app_ids": sorted(EXPECTED),
        "source": "enforcing QEMU serial acceptance markers",
        "release_ready": False,  # Not a Waydroid, SELinux-policy, or OS release approval
    }
