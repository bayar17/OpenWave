#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def stale_configuration(build, sdk, target):
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return False
    contents = cache.read_text()
    values = dict(re.findall(r"^([^#/:\n]+):[^=\n]+=(.*)$", contents, re.MULTILINE))
    expected = {
        "CMAKE_C_COMPILER": sdk / "bin/clang",
        "CMAKE_CXX_COMPILER": sdk / "bin/clang++",
        "CMAKE_ASM_COMPILER": sdk / "bin/clang",
        "CMAKE_SYSROOT": sdk / "sysroot" / target,
        "CMAKE_TOOLCHAIN_FILE": sdk / "cmake" / f"openwave-{target.split('-')[0]}.cmake",
        "QT_HOST_PATH": sdk,
    }
    for key, path in expected.items():
        if values.get(key) and Path(values[key]).absolute() != path:
            return True
    for language in ("C", "CXX", "ASM"):
        key = f"CMAKE_{language}_COMPILER"
        for metadata in (build / "CMakeFiles").glob(f"*/CMake{language}Compiler.cmake"):
            match = re.search(rf'set\({key}\s+"([^"]+)"\)', metadata.read_text())
            if match and Path(match[1]).absolute() != expected[key]:
                return True
    return any(f"{sdk}/{directory}/" in contents for directory in ("clang", "qt", "openwave-api"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("cmake_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    cmake_args = args.cmake_args
    if cmake_args[:1] == ["--"]:
        cmake_args = cmake_args[1:]
    sdk = args.sdk.absolute()
    for compiler in ("clang", "clang++"):
        if not os.access(sdk / "bin" / compiler, os.X_OK):
            parser.error(f"missing SDK compiler: {sdk / 'bin' / compiler}; run scripts/export-openwave-sdk.py")
    build = None
    for index, argument in enumerate(cmake_args):
        if argument == "-B" and index + 1 < len(cmake_args):
            build = Path(cmake_args[index + 1]).absolute()
        elif argument.startswith("-B") and len(argument) > 2:
            build = Path(argument[2:]).absolute()
    if build is None:
        parser.error("an explicit CMake -B build directory is required")
    if stale_configuration(build, sdk, args.target) and "--fresh" not in cmake_args:
        print(f"Resetting stale CMake configuration in {build} for SDK {sdk}", flush=True)
        cmake_args.insert(0, "--fresh")
    return subprocess.call(["cmake", *cmake_args])


if __name__ == "__main__":
    sys.exit(main())
