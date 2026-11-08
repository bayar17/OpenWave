#!/usr/bin/env python3
"""Label a COPY of an OpenWave ext4 image using its staged file_contexts.

Never mounts or labels the host. Requires host libselinux, debugfs, e2fsck,
GNU cp. The original image and source rootfs are left unchanged.

Usage:
  python3 scripts/openwave-label-ext4.py \
    --rootfs rootfs --image out/my-image.img --output out/my-image-selinux.img
"""

import argparse
import ctypes
import errno
import hashlib
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


class SelinuxOpt(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("value", ctypes.c_char_p)]


def need_tool(name):
    if shutil.which(name) is None:
        raise RuntimeError(f"Missing host utility: {name}")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def walk_without_following_symlinks(root):
    stack = [(root, "/")]
    while stack:
        directory, guest = stack.pop()
        yield guest, directory.lstat().st_mode
        with os.scandir(directory) as scan:
            entries = sorted(scan, key=lambda e: e.name, reverse=True)
        for item in entries:
            name = item.name
            require("\n" not in name and "\r" not in name and "\0" not in name,
                    f"Unsupported newline in filename: {item.path!r}")
            guest_path = guest.rstrip("/") + "/" + name
            mode = item.stat(follow_symlinks=False).st_mode
            if stat.S_ISDIR(mode):
                stack.append((Path(item.path), guest_path))
            else:
                yield guest_path, mode


def open_selinux_contexts(file_contexts):
    companions = sorted(path.name for path in file_contexts.parent.glob(file_contexts.name + '*')
                        if path.name != file_contexts.name)
    require(not companions, 'Unsupported file_contexts companions: ' + ', '.join(companions))
    try:
        library = ctypes.CDLL("libselinux.so.1", use_errno=True)
    except OSError as exc:
        raise RuntimeError("Host libselinux.so.1 is required") from exc
    library.selabel_open.argtypes = (ctypes.c_uint, ctypes.POINTER(SelinuxOpt), ctypes.c_uint)
    library.selabel_open.restype = ctypes.c_void_p
    library.selabel_lookup_raw.argtypes = (
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_char_p, ctypes.c_int)
    library.selabel_lookup_raw.restype = ctypes.c_int
    library.selabel_close.argtypes = (ctypes.c_void_p,)
    library.selabel_close.restype = None
    library.freecon.argtypes = (ctypes.c_void_p,)
    library.freecon.restype = None
    option_path = os.fsencode(file_contexts)
    opts = (SelinuxOpt * 1)(SelinuxOpt(3, option_path))  # SELABEL_OPT_PATH = 3
    handle = library.selabel_open(0, opts, 1)            # SELABEL_CTX_FILE = 0
    require(bool(handle), f"Cannot load file contexts: {file_contexts}: errno {ctypes.get_errno()}")
    return library, handle


def label_lookup(library, handle, guest_path, mode):
    result = ctypes.c_char_p()
    code = library.selabel_lookup_raw(handle, ctypes.byref(result),
                                      os.fsencode(guest_path), mode)
    if code != 0:
        err = ctypes.get_errno()
        raise RuntimeError(
            f"No SELinux file-context match for {guest_path!r} "
            f"(mode {mode:o}): {os.strerror(err or errno.EINVAL)}")
    try:
        context = ctypes.string_at(result)
        require(context.startswith(b"system_u:object_r:") and b":s0" in context,
                f"Unexpected SELinux context for {guest_path}: {context!r}")
        return context
    finally:
        library.freecon(ctypes.cast(result, ctypes.c_void_p))


def debugfs_quote(value):
    # debugfs's ss parser understands quoted filenames (including spaces).
    # Reject newline/control characters to keep the batch file unambiguous.
    require(all(ord(c) >= 32 and ord(c) != 127 for c in value),
            f"Unsafe path in debugfs command: {value!r}")
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def check_label(image, guest_path):
    proc = subprocess.run(
        ["debugfs", "-R", f"ea_list {debugfs_quote(guest_path)}", str(image)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    require(proc.returncode == 0 and "security.selinux" in proc.stdout,
            f"Missing SELinux attribute after labeling: {guest_path}\n"
            f"{proc.stdout}\n{proc.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rootfs = args.rootfs.resolve(strict=True)
    image = args.image.resolve(strict=True)
    output = args.output.absolute()
    require(rootfs.is_dir(), f"Not a directory: {rootfs}")
    require(image.is_file(), f"Not a regular image: {image}")
    require(not output.exists() and not output.is_symlink(),
            f"Refusing to overwrite an existing destination: {output}")
    require(image != output, "Input and output must differ")
    fc = rootfs / "etc/selinux/openwave/contexts/files/file_contexts"
    policy = rootfs / "etc/selinux/openwave/policy/policy.35"
    require(fc.is_file() and fc.stat().st_size > 0, f"Missing file_contexts: {fc}")
    require(policy.is_file() and policy.stat().st_size > 0, f"Missing policy.35: {policy}")
    for tool in ("debugfs", "e2fsck", "cp"):
        need_tool(tool)
    print("Checking source image (read-only)...", flush=True)
    source_check = subprocess.run(["e2fsck", "-fn", str(image)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    require(source_check.returncode == 0,
            f"Source image failed read-only e2fsck:\n{source_check.stdout}")

    library, handle = open_selinux_contexts(fc)
    try:
        with tempfile.TemporaryDirectory(prefix="openwave-label-") as tmpname:
            tmp = Path(tmpname)
            batch = tmp / "commands.txt"
            value_files = {}
            number = 0
            expected = []
            inode_contexts = {}
            # Refuse a rootfs/image mismatch before changing any inode labels.
            for source, guest in ((policy, '/etc/selinux/openwave/policy/policy.35'),
                                  (fc, '/etc/selinux/openwave/contexts/files/file_contexts')):
                extracted = tmp / source.name
                subprocess.run(['debugfs', '-R', f'dump {debugfs_quote(guest)} {debugfs_quote(str(extracted))}', str(image)],
                               check=True, capture_output=True)
                require(extracted.is_file() and extracted.read_bytes() == source.read_bytes(),
                        f'Image and staging disagree: {guest}')
            with batch.open("w", encoding="utf-8") as script:
                objects = list(walk_without_following_symlinks(rootfs))
                if not (rootfs / 'lost+found').exists():
                    # mke2fs creates this directory independently of -d.
                    objects.append(('/lost+found', stat.S_IFDIR | 0o700))
                for guest_path, mode in objects:
                    if stat.S_ISSOCK(mode):
                        raise RuntimeError(f"Cannot label socket in staging tree: {guest_path}")
                    context = label_lookup(library, handle, guest_path, mode)
                    source_path = rootfs / guest_path.lstrip('/')
                    if source_path.exists() or source_path.is_symlink():
                        info = source_path.lstat()
                        inode = (info.st_dev, info.st_ino)
                        require(inode not in inode_contexts or inode_contexts[inode] == context,
                                f'Conflicting labels for hard-linked inode: {guest_path}')
                        inode_contexts[inode] = context
                    expected.append((guest_path, context))
                    if context not in value_files:
                        label_file = tmp / ("context-" + hashlib.sha256(context).hexdigest())
                        label_file.write_bytes(context + b"\x00")
                        value_files[context] = label_file
                    # Use the path as a guest path. debugfs resolves paths in the
                    # image itself, never against the build host.
                    script.write(f"ea_set -f {debugfs_quote(str(value_files[context]))} "
                                 f"{debugfs_quote(guest_path)} security.selinux\n")
                    number += 1
            require(number > 0, "Rootfs is empty")
            print(f"Prepared {number} xattr operations / {len(value_files)} labels", flush=True)
            output.parent.mkdir(parents=True, exist_ok=True)
            print("Copying ext4 image without modifying the original...", flush=True)
            subprocess.run(["cp", "--reflink=auto", "--sparse=always", "--", str(image), str(output)],
                           check=True)
            print("Applying SELinux contexts directly to ext4 inodes...", flush=True)
            log_path = output.with_suffix(output.suffix + ".label.log")
            with log_path.open("w", encoding="utf-8") as log:
                proc = subprocess.run(["debugfs", "-w", "-f", str(batch), str(output)],
                                      stdout=log, stderr=subprocess.STDOUT)
            require(proc.returncode == 0, f"debugfs failed; examine {log_path}")
            # debugfs may exit successfully after a failed individual command.
            # Check its error diagnostics instead of trusting exit status alone.
            log_text = log_path.read_text(errors="replace")
            errors = ("while setting extended attribute", "File not found by ext2_lookup",
                      "No such file or directory", "Invalid argument", "Bad inode number",
                      "does not exist", "Filesystem is read-only")
            matches = [line for line in log_text.splitlines()
                       if any(phrase in line for phrase in errors)]
            require(not matches,
                    f"Some debugfs commands failed ({len(matches)}):\n" +
                    "\n".join(matches[:12]) + f"\nComplete log: {log_path}")
            # Read back EVERY label, including symlink inodes. ea_get prints
            # the full value whereas ea_list abbreviates larger attributes.
            batch.write_text(''.join(f'ea_get {debugfs_quote(path)} security.selinux\n'
                                     for path, _ in expected))
            checked = subprocess.run(['debugfs', '-f', str(batch), str(output)],
                                     capture_output=True, text=True, check=True)
            values = re.findall(r'^security\.selinux \(\d+\) = "(.*)"$', checked.stdout, re.M)
            require(values == [context.decode() + r'\000' for _, context in expected],
                    'Full inode-label readback failed')
            print("Checking representative file attributes...", flush=True)
            for guest in ("/", "/init", "/etc/init.d/appd",
                          "/etc/runlevels/default/appd", "/System/Services/appd",
                          "/usr/sbin/acpid", "/etc/selinux/openwave/policy/policy.35"):
                if (rootfs / guest.lstrip("/")).is_symlink() or \
                   (rootfs / guest.lstrip("/")).exists():
                    check_label(output, guest)
            print("Checking labeled image (read-only)...", flush=True)
            check = subprocess.run(["e2fsck", "-fn", str(output)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            require(check.returncode == 0,
                    f"Labeled image failed e2fsck:\n{check.stdout}")
            print(f"SUCCESS: {output}")
            print(f"SELinux attributes processed: {number}; debugfs log: {log_path}")
    finally:
        library.selabel_close(handle)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as err:
        print(f"ERROR: {err}", file=sys.stderr)
        sys.exit(1)
