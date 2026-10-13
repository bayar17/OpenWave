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

The repository now registers `llvm-project/` as an upstream submodule at that
commit. For a fresh OpenWave checkout, run:

```bash
git submodule update --init llvm-project
git -C llvm-project apply --check ../patches/llvm/openwave-llvm.patch
git -C llvm-project apply --whitespace=nowarn ../patches/llvm/openwave-llvm.patch
```

The gitlink records the upstream base; OpenWave changes remain in this patch.
Do not reapply it to the existing modified development checkout.

```bash
git -C /path/to/clean-llvm-project checkout --detach 85ac560262434c9ccfc0c183ec22d4138ed647fb
git -C /path/to/clean-llvm-project apply --check /mnt/aosp/ThunderOS/patches/llvm/openwave-llvm.patch
git -C /path/to/clean-llvm-project apply --whitespace=nowarn /mnt/aosp/ThunderOS/patches/llvm/openwave-llvm.patch
```

`--whitespace=nowarn` preserves the exact local snapshot rather than rewriting
its whitespace. To remove the patch, use `git apply --reverse` with the same
patch path.

## Verification and existing issue

The patch was checked and applied to pristine copies of all affected files from
the base commit. Every resulting file matched the local source byte for byte.
Reverse application restored the original files and removed the new files.
The local LLVM working-tree status was unchanged.

This verifies patch application, not a fresh LLVM build. No compiler rebuild
was performed. The existing `TripleTest.cpp` addition calls
`Triple::isOSOpenWave()`, but that method is absent from the current `Triple.h`.
That unit-test compilation issue is preserved in this exact snapshot and needs
a separate source correction before building the affected unit tests.

The validation report is in `out/llvm-patch-validation/report.json`.
