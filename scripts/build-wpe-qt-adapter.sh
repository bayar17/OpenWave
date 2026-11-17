#!/usr/bin/env bash
# Rebuild only WPE's Qt binding against the exported target Qt SDK.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build
BUILD="$ROOT/out/wpe-qt-adapter-$OPENWAVE_TARGET_TRIPLE"
for relative in usr/include/wpe-webkit-2.0/wpe/webkit.h usr/include/epoxy/egl.h usr/include/unicode/utypes.h usr/lib64/libWPEWebKit-2.0.so; do
    [[ -f "$OPENWAVE_SYSROOT/$relative" ]] || {
        echo "Missing target WPE development file: $OPENWAVE_SYSROOT/$relative" >&2
        exit 1
    }
done
cmake -S "$ROOT/cross/wpe-qt-adapter" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$OPENWAVE_CMAKE_TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" --parallel "${OPENWAVE_BUILD_JOBS:-4}"
DESTDIR="$OPENWAVE_SYSROOT" cmake --install "$BUILD"
