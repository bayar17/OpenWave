# OpenWave LLVM/Clang patch

`openwave-llvm.patch` exports the local LLVM working-tree changes against upstream
commit `85ac560262434c9ccfc0c183ec22d4138ed647fb` from
`https://github.com/llvm/llvm-project.git`.

The patch contains nine modified files and the two new
`clang/lib/Driver/ToolChains/OpenWave.{cpp,h}` files. It preserves the source
bytes, including existing formatting edits and trailing whitespace.

The changes provide the `openwave` OS triple, x86_64 Clang target definitions,
driver registration, the OpenWave linker/sysroot implementation, and x86 stack
protector TLS handling. The existing stack-protector and Triple tests are also
included. The toolchain selects LLD, compiler-rt, libc++, libc++abi and the
`/lib64/ld-linux-x86-64.so.2` dynamic loader.

## Apply

Use a **clean LLVM checkout** at the recorded commit. The current local
`llvm-project/` already contains these changes.

The repository registers `llvm-project/` as an upstream submodule at that
commit. Build commands and SDK export initialize it and apply this patch
automatically. Preparation can also be run without an installed SDK:

```bash
./build-openwave llvm-patch
```

The gitlink records the upstream base; OpenWave changes remain in this patch.
The helper detects an already-applied patch with a reverse application check.
It checks the complete patch before applying and stops on conflicts or partial
application without resetting, cleaning, or overwriting local work. Image packing,
time-zone staging, and help do not require LLVM preparation. This step prepares
sources only; it does not rebuild the compiler. For manual application elsewhere:

```bash
git -C /path/to/clean-llvm-project checkout --detach 85ac560262434c9ccfc0c183ec22d4138ed647fb
git -C /path/to/clean-llvm-project apply --check /mnt/aosp/ThunderOS/patches/llvm/openwave-llvm.patch
git -C /path/to/clean-llvm-project apply --whitespace=nowarn /mnt/aosp/ThunderOS/patches/llvm/openwave-llvm.patch
```

`--whitespace=nowarn` preserves the exact local snapshot rather than rewriting
its whitespace. To remove the patch, use `git apply --reverse` with the same
patch path.

## Verification

The patch was checked and applied to pristine copies of all affected files from
the base commit. Every resulting file matched the local source byte for byte.
Reverse application restored the original files and removed the new files.
The local LLVM working-tree status was unchanged.

This verifies patch application, not a fresh LLVM build. No compiler rebuild
was performed. The original export omitted the `Triple::isOSOpenWave()` helper
used by its Triple test. The patch now includes that helper, and its call was
checked with the native C++ compiler. The original validation report below
predates this correction; updated patch checks are in
`out/llvm-build-check/automatic-patch-report.json`.

Use `./build-openwave llvm` to prepare, build and install the compiler, or
`./build-openwave all` for LLVM followed by the dependency and userspace builds.
Existing LLVM CMake options are preserved; fresh configurations match the
current Release/X86/clang/lld setup with tests disabled by default.

The validation report is in `out/llvm-patch-validation/report.json`.
