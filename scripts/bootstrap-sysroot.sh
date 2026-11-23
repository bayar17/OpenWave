#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Only load path definitions. Never inherit target compiler flags into host tools.
source "$ROOT/env.sh" --bootstrap
: "${OPENWAVE_SDK:?}" "${OPENWAVE_SYSROOT:?}" "${OPENWAVE_TARGET_TRIPLE:?}"
[[ "$OPENWAVE_TARGET_TRIPLE" == x86_64-* ]] || { echo 'Only x86_64 sysroot bootstrap is currently implemented.' >&2; exit 2; }
for tool in clang clang++ ld.lld llvm-ar llvm-ranlib llvm-nm; do
    [[ -x "$OPENWAVE_SDK/bin/$tool" ]] || { echo "Missing native toolchain executable: $OPENWAVE_SDK/bin/$tool" >&2; exit 1; }
done
[[ -f "$ROOT/linux/Makefile" ]] || { echo "Linux source missing: $ROOT/linux" >&2; exit 1; }
[[ -x "$ROOT/glibc/configure" ]] || { echo "glibc source/configure missing: $ROOT/glibc/configure" >&2; exit 1; }
mkdir -p "$OPENWAVE_SYSROOT/usr/include" "$OPENWAVE_SYSROOT/usr/lib" "$OPENWAVE_SYSROOT/usr/lib64" "$OPENWAVE_SYSROOT/lib64" "$ROOT/out/bootstrap"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"
# Install kernel UAPI headers, not the host's /usr/include/linux files.
make -C "$ROOT/linux" ARCH=x86 headers_install INSTALL_HDR_PATH="$OPENWAVE_SYSROOT/usr"
# Configure glibc, install preliminary target headers, then build compiler-rt
# builtins BEFORE attempting to link glibc itself.
GLIBC_BUILD="${OPENWAVE_GLIBC_BUILD:-$ROOT/out/bootstrap/glibc-$OPENWAVE_TARGET_TRIPLE}"
mkdir -p "$GLIBC_BUILD"
CLANG="$OPENWAVE_SDK/bin/clang"
CLANGXX="$OPENWAVE_SDK/bin/clang++"
RT_RESOURCE="$($CLANG --print-resource-dir)"
RT_DIR="$RT_RESOURCE/lib/$OPENWAVE_TARGET_TRIPLE"
RT_ARCHIVE="$RT_DIR/libclang_rt.builtins.a"
mkdir -p "$RT_DIR"

# Used by glibc's linker invocation, even when the archive isn't in the sysroot.
GLIBC_CC="$CLANG --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$OPENWAVE_SYSROOT -L$RT_DIR"
GLIBC_CXX="$CLANGXX --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$OPENWAVE_SYSROOT -L$RT_DIR"
if [[ ! -f "$GLIBC_BUILD/config.status" ]]; then
  (cd "$GLIBC_BUILD" && \
    env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS \
      CC="$GLIBC_CC" CXX="$GLIBC_CXX" \
      AR="$OPENWAVE_SDK/bin/llvm-ar" RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib" \
      LD="$OPENWAVE_SDK/bin/ld.lld" \
      "$ROOT/glibc/configure" \
      --prefix=/usr --libdir=/usr/lib64 --with-headers="$OPENWAVE_SYSROOT/usr/include" \
      --host="$OPENWAVE_AUTOTOOLS_HOST" --build="$("$ROOT/glibc/scripts/config.guess")" \
      --disable-werror)
else
  echo 'Reusing configured glibc build directory; overriding CC to include the compiler-rt library path.'
fi

# This must not require a finished libc or compiler-rt archive.
make -C "$GLIBC_BUILD" install-headers install_root="$OPENWAVE_SYSROOT"
for header in assert.h limits.h features.h; do
  [[ -s "$OPENWAVE_SYSROOT/usr/include/$header" ]] || {
    echo "Missing preliminary glibc header: $header" >&2; exit 1;
  }
done

# glibc install-headers can omit gnu/stubs.h until the complete libc is built.
# Create a strictly temporary, empty bootstrap placeholder, not a host header.
STUBS="$OPENWAVE_SYSROOT/usr/include/gnu/stubs.h"
created_stubs=0
mkdir -p "$(dirname "$STUBS")"
if [[ ! -e "$STUBS" ]]; then
  printf '/* Temporary OpenWave glibc bootstrap header; NOT FOR SDK RELEASE. */\n' > "$STUBS"
  created_stubs=1
fi
cleanup_bootstrap_stubs() {
  if (( created_stubs )) && [[ -f "$STUBS" ]] && grep -q 'NOT FOR SDK RELEASE' "$STUBS"; then
    rm -f -- "$STUBS"
  fi
}
trap cleanup_bootstrap_stubs EXIT

RT_BUILD="${OPENWAVE_COMPILER_RT_BUILD:-$ROOT/out/bootstrap/compiler-rt-$OPENWAVE_TARGET_TRIPLE}"
if [[ ! -s "$RT_ARCHIVE" ]]; then
  [[ -f "$ROOT/llvm-project/compiler-rt/CMakeLists.txt" ]] || {
    echo 'Missing llvm-project/compiler-rt sources.' >&2; exit 1;
  }
  cmake -S "$ROOT/llvm-project/compiler-rt" -B "$RT_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_CXX_COMPILER="$CLANGXX" \
    -DCMAKE_ASM_COMPILER="$CLANG" \
    -DCMAKE_C_COMPILER_TARGET="$OPENWAVE_TARGET_TRIPLE" \
    -DCMAKE_CXX_COMPILER_TARGET="$OPENWAVE_TARGET_TRIPLE" \
    -DCMAKE_ASM_COMPILER_TARGET="$OPENWAVE_TARGET_TRIPLE" \
    -DCMAKE_SYSROOT="$OPENWAVE_SYSROOT" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCOMPILER_RT_BUILD_BUILTINS=ON -DCOMPILER_RT_BUILD_CRT=OFF \
    -DCOMPILER_RT_BUILD_SANITIZERS=OFF -DCOMPILER_RT_BUILD_XRAY=OFF \
    -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_PROFILE=OFF \
    -DCOMPILER_RT_BAREMETAL_BUILD=ON
  cmake --build "$RT_BUILD" --target builtins --parallel "$JOBS"
  mapfile -t archives < <(find "$RT_BUILD" -type f -name 'libclang_rt.builtins*.a' -print)
  if (( ${#archives[@]} != 1 )); then
    echo "Expected exactly one compiler-rt builtins archive, found ${#archives[@]}" >&2
    printf '%s\n' "${archives[@]}" >&2
    exit 1
  fi
  [[ -s "${archives[0]}" ]] || { echo 'compiler-rt archive is empty' >&2; exit 1; }
  "$OPENWAVE_SDK/bin/llvm-ar" t "${archives[0]}" | grep -q . || {
    echo 'compiler-rt archive has no members' >&2; exit 1;
  }
  install -m 0644 "${archives[0]}" "$RT_ARCHIVE"
fi
[[ -s "$RT_ARCHIVE" ]] || { echo "compiler-rt builtins missing: $RT_ARCHIVE" >&2; exit 1; }

# Ensure a previously configured glibc build also sees this runtime at link time.
make -C "$GLIBC_BUILD" -j "$JOBS" CC="$GLIBC_CC"
# Replace the temporary stub with the authentic glibc-generated header.
cleanup_bootstrap_stubs
created_stubs=0
make -C "$GLIBC_BUILD" install install_root="$OPENWAVE_SYSROOT" CC="$GLIBC_CC"
if [[ ! -s "$STUBS" ]] || grep -q 'NOT FOR SDK RELEASE' "$STUBS"; then
  echo "glibc did not install a real gnu/stubs.h: $STUBS" >&2
  exit 1
fi
# Do not accept a directory-only 'sysroot'. Require actual C library/linker material.
for file in "$OPENWAVE_SYSROOT/usr/include/stdio.h" "$OPENWAVE_SYSROOT/usr/include/features.h"; do
  [[ -s "$file" ]] || { echo "glibc install incomplete: $file" >&2; exit 1; }
done
if ! find "$OPENWAVE_SYSROOT" -type f \( -name 'libc.so.6' -o -name 'libc-*.so' \) -print -quit | grep -q .; then
  [[ -e "$OPENWAVE_SYSROOT/usr/lib64/libc.so.6" || -e "$OPENWAVE_SYSROOT/lib64/libc.so.6" ]] || { echo 'glibc runtime missing from target sysroot' >&2; exit 1; }
fi
# Regenerate absolute paths only after libc is installed.
python3 "$ROOT/cross/generate-meson.py" --target "$OPENWAVE_TARGET_TRIPLE"
# Compiler identity check (does not require executing an OpenWave binary).
printf '#include <stdio.h>\nint main(void) { return 0; }\n' > "$ROOT/out/bootstrap/smoke.c"
"$OPENWAVE_SDK/bin/clang" --target="$OPENWAVE_TARGET_TRIPLE" --sysroot="$OPENWAVE_SYSROOT" \
  "$ROOT/out/bootstrap/smoke.c" -o "$ROOT/out/bootstrap/smoke"
file "$ROOT/out/bootstrap/smoke"
echo "Bootstrapped sysroot: $OPENWAVE_SYSROOT"
