"""Strict interpretation of read-only Waydroid mount guest preflight evidence.

The probe intentionally does NOT verify capability transfer, mount syscalls,
namespace isolation or Android boot. No report may claim those gates passed.
"""
import re

REQUIRED = frozenset({
    "enforcing", "helper_inode", "helper_label", "helper_elf",
    "image_system", "image_vendor", "loop_control",
})
BEGIN = re.compile(r"^MOUNT_QA_BEGIN enforcing=([01])$", re.M)
END = re.compile(r"^MOUNT_QA_END prerequisites=(PASS|BLOCKED)$", re.M)
GATE = re.compile(r"^MOUNT_QA_GATE ([a-z_]+)=(PASS|BLOCKED)(?: (.*))?$", re.M)
INFO = re.compile(r"^MOUNT_QA_INFO ([a-z_]+)=([^\r\n]+)$", re.M)


def evaluate(serial: str) -> dict:
    # QEMU serial consoles may emit CRLF; do not misclassify valid guest markers.
    serial = serial.replace("\r\n", "\n").replace("\r", "\n").replace("\x00", "")
    begins, ends = list(BEGIN.finditer(serial)), list(END.finditer(serial))
    matches = list(GATE.finditer(serial))
    gates = {}
    duplicated = []
    for match in matches:
        key, state, details = match.groups()
        if key in gates:
            duplicated.append(key)
        # Kernel audit output can interleave with console printf mid-line.
        # Keep AVCs in the full serial log; do not absorb them into a gate detail.
        detail = re.split(r"(?:\[\s*\d+\.\d+\]\s*)?audit:\s*type=|\bavc:\s+denied\b",
                          details or "", maxsplit=1, flags=re.I)[0].rstrip()
        gates[key] = {"status": state, "detail": detail}
    info = {}
    for match in INFO.finditer(serial):
        info[match.group(1)] = match.group(2)
    complete = (
        len(begins) == 1 and len(ends) == 1
        and begins[0].end() < ends[0].start()
        and len(matches) == len(REQUIRED)
        and not duplicated and set(gates) == REQUIRED
    )
    mode_ok = bool(begins and begins[0].group(1) == "1")
    passed = (
        complete and mode_ok and ends[0].group(1) == "PASS"
        and all(gates[key]["status"] == "PASS" for key in REQUIRED)
        and info.get("helper_domain_transition") == "UNVERIFIED"
        and info.get("mount_syscalls") == "NOT_ATTEMPTED"
    )
    return {
        "schema": 1,
        "diagnostics_complete": complete,
        "preflight_passed": passed,
        "enforcing": mode_ok,
        "gates": gates,
        "info": info,
        "helper_domain_transition": "UNVERIFIED",
        "mount_syscalls": "NOT_ATTEMPTED",
        "android_boot": "UNVERIFIED",
        "release_ready": False,
    }
