#!/usr/bin/env bash
# Cross-build Qt 6.10.3 host tools and target runtime for OpenWave.
# Does not assemble rootfs or disk images.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# When installed as ROOT/scripts/build-qt.sh, resolve the repository root.
if [[ ! -f "$ROOT/env.sh" ]]; then
    ROOT="$(cd -- "$ROOT/.." && pwd)"
fi
[[ -f "$ROOT/env.sh" ]] || { echo "Cannot locate OpenWave repository (env.sh)" >&2; exit 1; }
VERSION="${OPENWAVE_QT_VERSION:-6.10.3}"
[[ "$VERSION" == 6.10.3 ]] || { echo "Only the pinned Qt 6.10.3 archive is configured in this script" >&2; exit 2; }
TARGET="${OPENWAVE_TARGET_TRIPLE:-x86_64-unknown-openwave}"
SDK="${OPENWAVE_SDK:-$ROOT/toolchain}"
SYSROOT="${OPENWAVE_SYSROOT:-$SDK/sysroot/$TARGET}"
SOURCE="${OPENWAVE_QT_SOURCE:-$ROOT/third_party/qt-everywhere-src-$VERSION}"
OUT="${OPENWAVE_QT_BUILD_ROOT:-$ROOT/out/qt-$VERSION-$TARGET}"
HOST_BUILD="$OUT/host-build"
TARGET_BUILD="$OUT/target-build"
TARGET_TOOLCHAIN="$OUT/qt-target-toolchain.cmake"
ARCHIVE="$ROOT/out/downloads/qt-everywhere-src-$VERSION.tar.xz"
URL="https://download.qt.io/official_releases/qt/6.10/$VERSION/single/qt-everywhere-src-$VERSION.tar.xz"
SHA256="cbc81e726b0ff3c0cdb0219bf74545e91cec013c4a8503c20f93f83d73dff5d2"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"
SUBMODULES='qtbase,qtshadertools,qtdeclarative'

usage() {
cat <<'EOF'
Usage: scripts/build-qt.sh [fetch|host|target|all|check]
  fetch    Download and verify Qt 6.10.3 source archive (approx. 1.2 GB).
  host     Build/install native Qt host tools into toolchain/.
  target   Cross-build/install Qt into toolchain/sysroot/.../usr.
  all      Run host then target; source must already be present.
  check    Validate Qt CMake config, libraries, and host tools.
Optional: OPENWAVE_QT_SOURCE, OPENWAVE_BUILD_JOBS.
No rootfs or image is assembled by any action.
EOF
}

verify_source() {
    [[ -f "$SOURCE/configure" && -f "$SOURCE/qtbase/CMakeLists.txt" && -f "$SOURCE/qtdeclarative/CMakeLists.txt" && -f "$SOURCE/qtshadertools/CMakeLists.txt" ]] || {
      echo "Missing Qt 6.10.3 source: $SOURCE" >&2
      echo "Run '$0 fetch' or set OPENWAVE_QT_SOURCE to a complete Qt checkout." >&2
      exit 1
    }
}

fetch_source() {
    mkdir -p "$(dirname "$ARCHIVE")" "$ROOT/third_party"
    if [[ ! -f "$ARCHIVE" ]]; then
        curl -fL --retry 3 "$URL" -o "$ARCHIVE.partial"
        mv -f "$ARCHIVE.partial" "$ARCHIVE"
    fi
    echo "$SHA256  $ARCHIVE" | sha256sum -c - || {
        echo 'SHA-256 mismatch: refusing to extract Qt source.' >&2
        exit 1
    }
    if [[ ! -e "$SOURCE" ]]; then
        tar -xJf "$ARCHIVE" -C "$ROOT/third_party"
    fi
    verify_source
    echo "Qt source ready: $SOURCE"
}

# Protect native host tools from any OpenWave cross variables.
native() {
    env -u CC -u CXX -u AR -u AS -u LD -u NM -u RANLIB -u STRIP \
        -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u CPATH \
        -u CMAKE_TOOLCHAIN_FILE -u CMAKE_PREFIX_PATH -u QT_HOST_PATH \
        -u PKG_CONFIG_PATH -u PKG_CONFIG_LIBDIR -u PKG_CONFIG_SYSROOT_DIR \
        -u QMAKESPEC -u QTDIR "$@"
}

build_host() {
    verify_source
    for prog in cmake ninja perl python3; do
        command -v "$prog" >/dev/null || { echo "Missing host tool: $prog" >&2; exit 1; }
    done
    mkdir -p "$HOST_BUILD" "$SDK"
    if [[ ! -f "$HOST_BUILD/CMakeCache.txt" ]]; then
        (
          cd "$HOST_BUILD"
          native "$SOURCE/configure" \
            -prefix "$SDK" -release -nomake examples -nomake tests \
            -submodules "$SUBMODULES" \
            -- -DCMAKE_C_COMPILER=/usr/bin/cc -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
               -DCMAKE_INSTALL_LIBDIR=lib
        )
    fi
    native cmake --build "$HOST_BUILD" --parallel "$JOBS"
    native cmake --install "$HOST_BUILD"

    # OpenWave's env.sh expects certain host tools in toolchain/bin.
    mkdir -p "$SDK/bin"
    for name in moc rcc uic qmlcachegen qsb; do
        if [[ ! -e "$SDK/bin/$name" && -x "$SDK/libexec/$name" ]]; then
            ln -s "../libexec/$name" "$SDK/bin/$name"
        fi
    done
    if [[ ! -e "$SDK/bin/qmake" && -x "$SDK/bin/qmake6" ]]; then
        ln -s qmake6 "$SDK/bin/qmake"
    fi
    [[ -f "$SDK/lib/cmake/Qt6/Qt6Config.cmake" ]] || {
        echo "Host Qt installation did not create $SDK/lib/cmake/Qt6/Qt6Config.cmake" >&2; exit 1;
    }
    echo 'Host Qt tools installed.'
}

build_target() {
    verify_source
    [[ -f "$SDK/lib/cmake/Qt6/Qt6Config.cmake" ]] || {
        echo "Host Qt missing. Run '$0 host' first." >&2; exit 1;
    }
    [[ -x "$SDK/bin/clang" && -f "$SDK/cmake/openwave-${TARGET%%-*}.cmake" ]] || {
        echo 'Missing OpenWave Clang or cross CMake toolchain file.' >&2; exit 1;
    }
    [[ -d "$SYSROOT/usr/include" ]] || { echo "Incomplete sysroot: $SYSROOT" >&2; exit 1; }
    mkdir -p "$OUT" "$TARGET_BUILD"

    # Avoid accidentally finding host Qt libraries as target dependencies.
    cat > "$TARGET_TOOLCHAIN" <<EOF
include("$SDK/cmake/openwave-${TARGET%%-*}.cmake")
set(CMAKE_FIND_ROOT_PATH "\${CMAKE_SYSROOT}")
set(CMAKE_PREFIX_PATH "\${CMAKE_SYSROOT}/usr")
set(QT_HOST_PATH "$SDK" CACHE PATH "Native Qt tools" FORCE)
set(QT_HOST_PATH_CMAKE_DIR "$SDK/lib/cmake" CACHE PATH "Native Qt cmake" FORCE)
EOF

    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
    unset PKG_CONFIG_PATH

    if [[ ! -f "$TARGET_BUILD/CMakeCache.txt" ]]; then
        (
          cd "$TARGET_BUILD"
          "$SOURCE/configure" -release -nomake examples -nomake tests \
            -submodules "$SUBMODULES" \
            -prefix /usr -extprefix "$SYSROOT/usr" \
            -libdir lib64 -qt-host-path "$SDK" \
            -opengl es2 -eglfs -gbm -kms -no-xcb \
            -- -DCMAKE_TOOLCHAIN_FILE="$TARGET_TOOLCHAIN" \
               -DQT_QPA_DEFAULT_PLATFORM=eglfs \
               -DQT_QPA_DEFAULT_EGLFS_INTEGRATION=eglfs_kms
        )
    fi
    cmake --build "$TARGET_BUILD" --parallel "$JOBS"
    cmake --install "$TARGET_BUILD"
    check_install
}

check_install() {
    local cfg found=0
    for cfg in "$SYSROOT/usr/lib64/cmake/Qt6/Qt6Config.cmake" \
               "$SYSROOT/usr/lib/cmake/Qt6/Qt6Config.cmake"; do
        if [[ -f "$cfg" ]]; then echo "Target Qt config: $cfg"; found=1; fi
    done
    ((found)) || { echo 'Missing target Qt6Config.cmake' >&2; return 1; }
    for lib in Core Gui Network Qml Quick; do
        if ! find "$SYSROOT/usr/lib64" "$SYSROOT/usr/lib" \
               -maxdepth 1 -name "libQt6${lib}.so.6*" -print -quit 2>/dev/null | grep -q .; then
            echo "Missing target library: libQt6${lib}.so.6" >&2
            return 1
        fi
    done
    [[ -f "$SDK/lib/cmake/Qt6/Qt6Config.cmake" ]] || { echo 'Missing native host Qt' >&2; return 1; }
    echo 'Target Qt Qt6Config.cmake and Core/Gui/Network/Qml/Quick libraries are present.'
}

case "${1:-all}" in
    fetch) fetch_source ;;
    host) build_host ;;
    target) build_target ;;
    all) build_host; build_target ;;
    check) check_install ;;
    -h|--help|help) usage ;;
    *) usage >&2; exit 2 ;;
esac
