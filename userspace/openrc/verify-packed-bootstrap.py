#!/usr/bin/env python3
"""Require the packed ext4 /init to match the freshly built bootstrap."""

from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--built", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    if args.built.is_symlink() or args.image.is_symlink():
        raise SystemExit("built /init and image must not be symlinks")
    built = args.built.resolve(strict=True)
    image = args.image.resolve(strict=True)
    if not built.is_file() or not image.is_file():
        raise SystemExit("built /init and image must be regular files")
    debugfs = shutil.which("debugfs")
    if not debugfs:
        raise SystemExit("debugfs is required to inspect the packed ext4 image")

    with tempfile.TemporaryDirectory(prefix="openwave-packed-init-") as temporary:
        packed = Path(temporary) / "init"
        result = subprocess.run([debugfs, "-R", f"dump /init {packed}", str(image)],
                                capture_output=True, text=True)
        if result.returncode or not packed.is_file():
            raise SystemExit(f"could not extract packed /init: {result.stderr or result.stdout}")
        built_hash = digest(built)
        packed_hash = digest(packed)
        if built_hash != packed_hash or built.stat().st_size != packed.stat().st_size:
            raise SystemExit("packed /init does not match the freshly built bootstrap")
        record = {
            "built_path": str(built),
            "image_path": str(image),
            "packed_path": "/init",
            "built_size": built.stat().st_size,
            "packed_size": packed.stat().st_size,
            "built_sha256": built_hash,
            "packed_sha256": packed_hash,
            "match": True,
        }
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps(record, indent=2) + "\n")
        print(f"packed /init identity PASS sha256={packed_hash} size={packed.stat().st_size}")


if __name__ == "__main__":
    main()
