#!/usr/bin/env python3
"""Generate SDK-local Meson and CMake cross-files from versioned cross/ sources."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--target", default="x86_64-unknown-openwave")
parser.add_argument("--sdk", type=Path, help="Override SDK output root (default: repo/toolchain)")
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
sdk = (args.sdk or repo / "toolchain").resolve()
arch = args.target.split("-", 1)[0]
if arch not in ("x86_64", "aarch64"):
    parser.error("Unsupported architecture")
sysroot = sdk / "sysroot" / args.target
if not (sysroot / "usr/include").is_dir():
    parser.error(f"Target headers missing: {sysroot}/usr/include; bootstrap sysroot first")
for exe in ("clang", "clang++", "llvm-ar", "ld.lld"):
    if not (sdk / "bin" / exe).is_file():
        parser.error(f"Compiler tool missing: {sdk}/bin/{exe}; build LLVM first")

quote = lambda path: repr(str(path))
lines = ["[binaries]"]
for key, program in (("c", "clang"), ("cpp", "clang++")):
    lines.append(f"{key} = [{quote(sdk / 'bin' / program)}, {quote('--target=' + args.target)}, {quote('--sysroot=' + str(sysroot))}]")
for key, program in (("ar", "llvm-ar"), ("strip", "llvm-strip"), ("nm", "llvm-nm"), ("objcopy", "llvm-objcopy"), ("readelf", "llvm-readelf"), ("ld", "ld.lld")):
    lines.append(f"{key} = {quote(sdk / 'bin' / program)}")
lines += ["pkg-config = 'pkg-config'", "", "[host_machine]", "system = 'linux'", f"cpu_family = {quote(arch)}", f"cpu = {quote(arch)}", "endian = 'little'", "", "[properties]", f"sys_root = {quote(sysroot)}", "pkg_config_libdir = [" + ", ".join(quote(sysroot / p) for p in ("usr/lib64/pkgconfig", "usr/lib/pkgconfig", "usr/share/pkgconfig")) + "]", "needs_exe_wrapper = true", "", "[built-in options]", "pkg_config_path = []", ""]
output = sdk / "cmake"
output.mkdir(parents=True, exist_ok=True)
meson_file = output / f"openwave-{arch}.ini"
meson_file.write_text("\n".join(lines))

template = (repo / "cross/sdk-toolchain.cmake.in").read_text()
cmake_file = output / f"openwave-{arch}.cmake"
cmake_file.write_text(template.replace("@TRIPLE@", args.target).replace("@PROCESSOR@", arch))
print(f"Meson: {meson_file}\nCMake: {cmake_file}")
