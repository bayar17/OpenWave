#!/usr/bin/env bash
# Source this file before configuring/building Qt applications for OpenWave.
# Host Qt tools come from toolchain/bin; target headers/libraries stay in sysroot.
# Build helpers can source this file with --build for SDK paths only.
OPENWAVE_REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OPENWAVE_SDK="${OPENWAVE_SDK:-$OPENWAVE_REPO/toolchain}"
OPENWAVE_TARGET_TRIPLE="${OPENWAVE_TARGET_TRIPLE:-x86_64-unknown-openwave}"
OPENWAVE_AUTOTOOLS_HOST="${OPENWAVE_TARGET_TRIPLE%%-*}-unknown-linux-gnu"
OPENWAVE_ROOTFS="${OPENWAVE_ROOTFS:-$OPENWAVE_REPO/rootfs}"
export OPENWAVE_SDK OPENWAVE_TARGET_TRIPLE OPENWAVE_ROOTFS
OPENWAVE_SYSROOT="$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE"
OPENWAVE_QT_HOST="$OPENWAVE_SDK"
OPENWAVE_RUNTIME_STAGE="$OPENWAVE_REPO/out/staging/$OPENWAVE_TARGET_TRIPLE/userspace"
OPENWAVE_DEPENDENCY_BUILD="$OPENWAVE_REPO/out/sdk-dependencies/$OPENWAVE_TARGET_TRIPLE"
if [[ ! -d "$OPENWAVE_SYSROOT/usr/include" ]]; then
    echo "Missing target SDK: $OPENWAVE_SYSROOT. Run scripts/export-openwave-sdk.py first." >&2
    return 1
fi
# Exported machine files are per SDK location and target, never shared by ARM64.
OPENWAVE_CMAKE_TOOLCHAIN="$OPENWAVE_SDK/cmake/openwave-${OPENWAVE_TARGET_TRIPLE%%-*}.cmake"
OPENWAVE_MESON_TOOLCHAIN="$OPENWAVE_SDK/cmake/openwave-${OPENWAVE_TARGET_TRIPLE%%-*}.ini"
mkdir -p "$OPENWAVE_RUNTIME_STAGE" "$OPENWAVE_DEPENDENCY_BUILD"

openwave_cmake_configure() {
    python3 "$OPENWAVE_REPO/scripts/configure-openwave-cmake.py" \
        --sdk "$OPENWAVE_SDK" --target "$OPENWAVE_TARGET_TRIPLE" -- "$@"
}

# Build helpers need SDK paths without target flags in host-tool builds.
if [[ "${1:-}" == --build ]]; then
    return 0
fi

for openwave_env_required in "$OPENWAVE_SDK/bin/clang" "$OPENWAVE_SDK/bin/qmake" \
    "$OPENWAVE_SDK/bin/moc" "$OPENWAVE_CMAKE_TOOLCHAIN" \
    "$OPENWAVE_SYSROOT/usr/lib/cmake/Qt6/Qt6Config.cmake" \
    "$OPENWAVE_SDK/lib/cmake/OpenWaveSDK/OpenWaveSDKConfig.cmake"; do
    if [[ ! -f "$openwave_env_required" ]]; then
        printf 'Incomplete OpenWave SDK: %s. Run scripts/export-openwave-sdk.py first.\n' "$openwave_env_required" >&2
        unset openwave_env_required
        return 1
    fi
done
unset openwave_env_required

export THUNDER="$OPENWAVE_REPO"
export LLVM_SRC="$OPENWAVE_REPO/llvm-project"
export GLIBC="$OPENWAVE_REPO/glibc"
export OUT="$OPENWAVE_REPO/out"
export LLVM_BUILD="$OUT/llvm-build"
export GLIBC_BUILD="$OUT/glibc-build"
export SYSROOT="$OPENWAVE_SYSROOT"
case ":$PATH:" in
    ":$OPENWAVE_SDK/bin:"*) ;;
    *) export PATH="$OPENWAVE_SDK/bin:$PATH" ;;
esac
export CC="$OPENWAVE_SDK/bin/clang"
export CXX="$OPENWAVE_SDK/bin/clang++"
export AR="$OPENWAVE_SDK/bin/llvm-ar"
export NM="$OPENWAVE_SDK/bin/llvm-nm"
export RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib"
export STRIP="$OPENWAVE_SDK/bin/llvm-strip"
export OBJCOPY="$OPENWAVE_SDK/bin/llvm-objcopy"
export OBJDUMP="$OPENWAVE_SDK/bin/llvm-objdump"
export READELF="$OPENWAVE_SDK/bin/llvm-readelf"
export LD="$OPENWAVE_SDK/bin/ld.lld"
export CMAKE_TOOLCHAIN_FILE="$OPENWAVE_CMAKE_TOOLCHAIN"
export OPENWAVE_CMAKE_TOOLCHAIN OPENWAVE_SYSROOT OPENWAVE_QT_HOST
export CMAKE_PREFIX_PATH="$OPENWAVE_SYSROOT/usr:$OPENWAVE_SDK"
export QT_HOST_PATH="$OPENWAVE_QT_HOST"
export QT_HOST_PATH_CMAKE_DIR="$QT_HOST_PATH/lib/cmake"
export QTDIR="$OPENWAVE_SYSROOT/usr"
export QMAKE="$OPENWAVE_SDK/bin/qmake"
# Do not resolve target dependencies through the host's pkg-config packages.
export PKG_CONFIG_SYSROOT_DIR="$OPENWAVE_SYSROOT"
export PKG_CONFIG_LIBDIR="$OPENWAVE_SYSROOT/usr/lib64/pkgconfig:$OPENWAVE_SYSROOT/usr/lib/pkgconfig:$OPENWAVE_SYSROOT/usr/share/pkgconfig"
unset PKG_CONFIG_PATH
export CFLAGS="--target=$OPENWAVE_TARGET_TRIPLE --sysroot=$OPENWAVE_SYSROOT"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="$CFLAGS"
export LLVM_RESOURCE_DIR="$("$CC" -print-resource-dir)"

printf 'OpenWave SDK: %s\nTarget: %s\nSysroot: %s\nQt host tools: %s/bin\nCMake toolchain: %s\n' \
    "$OPENWAVE_SDK" "$OPENWAVE_TARGET_TRIPLE" "$SYSROOT" "$QT_HOST_PATH" "$CMAKE_TOOLCHAIN_FILE"
"$CC" --version | head -n 1
