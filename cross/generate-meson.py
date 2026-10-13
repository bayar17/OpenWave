#!/usr/bin/env python3
"""Regenerate Meson's absolute machine paths after relocating this SDK."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--target', default='x86_64-unknown-openwave')
a=p.parse_args()
sdk=Path(__file__).resolve().parents[1]
arch=a.target.split('-')[0]
if arch not in ('x86_64', 'aarch64'):
 p.error('Unsupported target architecture')
sysroot=sdk/'sysroot'/a.target
if not (sysroot/'usr/include').is_dir():
 p.error('Install the target sysroot before generating its machine file')
bin=sdk/'bin'
def literal(s): return repr(str(s))
s='[binaries]\n'
for key,program in (('c','clang'),('cpp','clang++')):
 s+=f'{key} = [{literal(bin/program)}, {literal("--target="+a.target)}, {literal("--sysroot="+str(sysroot))}]\n'
for key,program in (('ar','llvm-ar'),('strip','llvm-strip'),('nm','llvm-nm'),('objcopy','llvm-objcopy'),('readelf','llvm-readelf'),('ld','ld.lld')):
 s+=f'{key} = {literal(bin/program)}\n'
s+="pkg-config = 'pkg-config'\n\n[host_machine]\nsystem = 'linux'\n"
s+=f'cpu_family = {literal(arch)}\ncpu = {literal(arch)}\nendian = \'little\'\n\n[properties]\n'
s+=f'sys_root = {literal(sysroot)}\npkg_config_libdir = [{literal(sysroot/"usr/lib64/pkgconfig")}, {literal(sysroot/"usr/lib/pkgconfig")}, {literal(sysroot/"usr/share/pkgconfig")}]\nneeds_exe_wrapper = true\n'
file=sdk/'cmake'/f'openwave-{arch}.ini'
file.write_text(s)
print(file)
