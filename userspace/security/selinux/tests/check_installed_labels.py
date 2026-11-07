#!/usr/bin/env python3
"""Fail-closed check of actual installed inode labels; run inside the guest."""
import argparse
import os
from pathlib import Path
import sys


def inspect(root, paths):
    failures = []
    for relpath, expected_type in paths.items():
        path = root / relpath.lstrip("/")
        if not os.path.lexists(path):
            failures.append(f"MISSING {relpath}")
            continue
        try:
            # A symlink's inode label is distinct from the target's label.
            value = os.getxattr(path, "security.selinux", follow_symlinks=False).decode().rstrip("\x00")
        except (OSError, ValueError) as exc:
            failures.append(f"UNVERIFIED {relpath}: cannot read security.selinux: {exc}")
            continue
        if f":{expected_type}:" not in value:
            failures.append(f"BAD_LABEL {relpath}: {value}; expected {expected_type}")
        else:
            print(f"PASS {relpath}: {value}")
    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)
    return bool(failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/"))
    parser.add_argument("--arch", choices=("x86_64", "aarch64"), default="x86_64")
    args = parser.parse_args()
    paths = {
        "/usr/lib64/libselinux.so.1": "openwave_shared_lib_t",
        "/System/Services/appd": "openwave_appd_exec_t",
        "/etc/selinux/openwave/policy/policy.35": "openwave_private_t",
    }
    if args.arch == "x86_64":
        paths["/lib64/ld-linux-x86-64.so.2"] = "openwave_elf_loader_t"
    else:
        paths["/lib/ld-linux-aarch64.so.1"] = "openwave_elf_loader_t"
    return int(inspect(args.root, paths))


if __name__ == "__main__":
    sys.exit(main())
