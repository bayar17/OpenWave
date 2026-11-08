#!/usr/bin/env python3
"""Careful installer: integrate rootfs mode checks and fakeroot ownership fixes."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def modify(text: str) -> str:
    hook = 'python3 "$ROOT/scripts/fix-openwave-rootfs-permissions.py" --root "$OPENWAVE_ROOTFS" --apply\n'
    owner_hook = '    python3 "$3" --root "$1" --image-owners\n'
    arg_hook = '\' _ "$RUNTIME" "$TEMP_IMAGE" "$ROOT/scripts/fix-openwave-rootfs-permissions.py"'
    a = text.find('build_userspace() (')
    b = text.find('\nbuild_image() (', a)
    if a < 0 or b < 0:
        raise ValueError('Cannot locate unambiguous build_userspace/build_image functions')
    block = text[a:b]
    if hook not in block:
        anchor = 'python3 "$ROOT/scripts/assemble-openwave-rootfs.py"\n'
        if block.count(anchor) != 1:
            raise ValueError('Cannot locate unique rootfs assembly call within build_userspace')
        block = block.replace(anchor, anchor + hook, 1)
    text = text[:a] + block + text[b:]
    a = text.find('\nbuild_image() (')
    if a < 0:
        raise ValueError('Cannot locate build_image')
    b = text.find('\n\n', a)
    # Operate only on build_image; find the closing function marker before test functions.
    end = text.find('\nbuild_test_permissions() (', a)
    if end < 0:
        raise ValueError('Cannot locate end of build_image')
    block = text[a:end]
    if owner_hook not in block:
        anchor = '    mke2fs -q -t ext4 -L OpenWave -d "$1" "$2"\n'
        if block.count(anchor) != 1:
            raise ValueError('Cannot locate unique mke2fs image population step')
        block = block.replace(anchor, owner_hook + anchor, 1)
    if arg_hook not in block:
        anchor = "' _ \"$RUNTIME\" \"$TEMP_IMAGE\""
        if block.count(anchor) != 1:
            raise ValueError('Cannot locate fakeroot argument list')
        block = block.replace(anchor, arg_hook, 1)
    return text[:a] + block + text[end:]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    source = HERE / 'scripts/fix-openwave-rootfs-permissions.py'
    dest = ROOT / 'scripts/fix-openwave-rootfs-permissions.py'
    entry = ROOT / 'build-openwave'
    if not source.is_file() or not entry.is_file():
        p.error(f'Unrecognized project layout; expected {entry} and {source}')
    old = entry.read_text()
    new = modify(old)
    with tempfile.TemporaryDirectory(prefix='openwave-permissions-validate-') as temp:
        candidate = Path(temp) / 'build-openwave'
        candidate.write_text(new)
        subprocess.run(['bash', '-n', str(candidate)], check=True)
        subprocess.run(['python3', '-m', 'py_compile', str(source)], check=True)
    print(f'Build dispatcher: {entry}')
    print(f'Mode utility: {dest}')
    if old == new and dest.is_file() and dest.read_bytes() == source.read_bytes():
        print('Already installed; no changes needed.')
        return
    if args.dry_run:
        print('Dry run passed; would install utility and update build dispatcher.')
        return
    backup = ROOT / 'build-openwave.before-permissions.bak'
    if not backup.exists():
        shutil.copy2(entry, backup)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)
    dest.chmod(0o755)
    entry.write_text(new)
    subprocess.run(['bash', '-n', str(entry)], check=True)
    print('Integrated rootfs mode step and fakeroot image-ownership exceptions.')
    print(f'Backup (if newly created): {backup}')


if __name__ == '__main__':
    main()
