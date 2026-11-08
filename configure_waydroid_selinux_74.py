#!/usr/bin/env python3
"""Apply OpenWave Waydroid diagnostic-74 SELinux loop-device policy changes.

Run from /mnt/aosp/ThunderOS (or pass --root PATH). Source edits only:
this does not build/relabel/install the policy in a guest image.
"""
from __future__ import annotations

import argparse
import datetime as dt
import difflib
import re
import shutil
from pathlib import Path

TYPES = (
    "(type openwave_loop_control_t)",
    "(type openwave_loop_block_t)",
)
RULES = (
    "(allow openwave_waydroid_manager_t openwave_loop_control_t (chr_file (open read write getattr ioctl)))",
    "(allow openwave_waydroid_manager_t openwave_loop_block_t (blk_file (open read write getattr ioctl)))",
    "(allow openwave_udev_t openwave_loop_control_t (chr_file (open read write getattr setattr ioctl)))",
    "(allow openwave_udev_t openwave_loop_block_t (blk_file (open read write getattr setattr ioctl)))",
)
CONTEXTS = (
    "/dev/loop-control system_u:object_r:openwave_loop_control_t:s0",
    "/dev/loop[0-9]+ system_u:object_r:openwave_loop_block_t:s0",
)


def add_cil(old: str) -> str:
    result = old
    missing_types = [t for t in TYPES if t not in result.splitlines()]
    if missing_types:
        anchor = "(type openwave_device_t)"
        if result.count(anchor) != 1:
            raise ValueError(f"Expected one {anchor!r} in openwave.cil")
        result = result.replace(anchor, anchor + "\n" + "\n".join(missing_types), 1)
    missing_rules = [r for r in RULES if r not in result.splitlines()]
    if missing_rules:
        result = result.rstrip("\n") + "\n\n;; Diagnostic 74: dedicated Waydroid loop device access\n" + "\n".join(missing_rules) + "\n"
    return result


def add_contexts(old: str) -> str:
    result = old
    for line in CONTEXTS:
        if line not in result.splitlines():
            result = result.rstrip("\n") + "\n" + line + "\n"
    return result


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path.cwd(), help="OpenWave repository root")
    ap.add_argument("--check", action="store_true", help="Preview diff without modifying files")
    args = ap.parse_args()
    root = args.root.resolve()
    policy_dir = root / "userspace/security/selinux"
    cil = policy_dir / "openwave.cil"
    contexts = policy_dir / "file_contexts"
    for p in (cil, contexts):
        if not p.is_file():
            ap.error(f"Missing {p}; run in the OpenWave source tree or provide --root")

    changes = []
    for path, transform in ((cil, add_cil), (contexts, add_contexts)):
        original = path.read_text(encoding="utf-8")
        changed = transform(original)
        if original != changed:
            changes.append((path, original, changed))
            diff = difflib.unified_diff(original.splitlines(keepends=True), changed.splitlines(keepends=True),
                                        fromfile=str(path) + ':old', tofile=str(path) + ':new')
            print(''.join(diff))
        else:
            print(f"UNCHANGED: {path.relative_to(root)}")

    if args.check:
        print(f"Preview only: {len(changes)} file(s) would change")
        return

    stamp = dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    for path, old, new in changes:
        backup = path.with_name(path.name + f'.before-loop-74.{stamp}')
        shutil.copy2(path, backup)
        path.write_text(new, encoding='utf-8')
        print(f"UPDATED: {path.relative_to(root)} (backup: {backup.name})")
    print(f"Complete: {len(changes)} file(s) updated.")
    print("\nIMPORTANT: /dev is devtmpfs and the generic fsuse label is openwave_device_t.")
    print("The file_contexts entries require a boot-time relabel mechanism (e.g. udev/restorecon)")
    print("for /dev/loop-control and /dev/loop[0-9]+. They do NOT relabel devices on their own.")
    print("Do not add broad openwave_device_t access to work around missing labels.")
    print("Rebuild and deploy the compiled SELinux policy and updated image before retesting.")


if __name__ == '__main__':
    main()
