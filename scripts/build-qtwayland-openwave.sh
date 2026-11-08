#!/usr/bin/env bash
# Export matching QtWayland 6.10.3 and its target development dependencies.
# Host scanners stay in toolchain; target files install only into target SDK.
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build
SDK="$OPENWAVE_SDK"
SYSROOT="$OPENWAVE_SYSROOT"
TARGET="$OPENWAVE_TARGET_TRIPLE"
[[ "$TARGET" == x86_64-unknown-openwave ]] || { echo 'QtWayland currently supports x86_64 only' >&2; exit 2; }
QT_SOURCE="${OPENWAVE_QT_SOURCE:-$ROOT/third_party/qt-everywhere-src-6.10.3}"
QT_BUILD="${OPENWAVE_QT_BUILD_ROOT:-$ROOT/out/qt-6.10.3-$TARGET}"
DEPENDENCIES="$ROOT/out/sdk-dependencies/$TARGET"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"
HOST_PKG_DIRS="$SDK/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"

native() {
    env -u CC -u CXX -u AR -u AS -u LD -u NM -u RANLIB -u STRIP \
        -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u CPATH \
        -u CMAKE_TOOLCHAIN_FILE -u CMAKE_PREFIX_PATH -u QT_HOST_PATH \
        -u PKG_CONFIG_PATH -u PKG_CONFIG_LIBDIR -u PKG_CONFIG_SYSROOT_DIR \
        -u QMAKESPEC -u QTDIR "$@"
}

configure_meson() {
    local build="$1" source="$2"
    shift 2
    if [[ -f "$build/build.ninja" ]]; then
        meson setup --reconfigure "$build" "$source" "$@"
    else
        meson setup "$build" "$source" "$@"
    fi
}

build_dependencies() {
    local host="$DEPENDENCIES/wayland-host" wayland="$DEPENDENCIES/wayland-openwave" xkb="$DEPENDENCIES/xkbcommon-openwave"
    mkdir -p "$DEPENDENCIES"
    local native_file="$DEPENDENCIES/wayland-build-machine.ini"
    cat >"$native_file" <<EOF
[properties]
sys_root = '/'
pkg_config_libdir = ['$SDK/lib/pkgconfig', '/usr/lib/x86_64-linux-gnu/pkgconfig', '/usr/lib/pkgconfig', '/usr/share/pkgconfig']
EOF
    if [[ -f "$host/build.ninja" ]]; then
        native meson setup --reconfigure "$host" "$ROOT/third_party/wayland" --prefix="$SDK" --libdir=lib \
            -Dlibraries=false -Dscanner=true -Dtests=false -Ddocumentation=false -Ddtd_validation=false
    else
        native meson setup "$host" "$ROOT/third_party/wayland" --prefix="$SDK" --libdir=lib \
            -Dlibraries=false -Dscanner=true -Dtests=false -Ddocumentation=false -Ddtd_validation=false
    fi
    native ninja -C "$host" -j "$JOBS"
    native meson install -C "$host" --no-rebuild
    export PATH="$SDK/bin:$PATH"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
    export PKG_CONFIG_PATH_FOR_BUILD="$HOST_PKG_DIRS"
    export PKG_CONFIG_LIBDIR_FOR_BUILD="$HOST_PKG_DIRS"
    unset PKG_CONFIG_PATH
    configure_meson "$wayland" "$ROOT/third_party/wayland" \
        --native-file "$native_file" \
        --cross-file "$SDK/cmake/openwave-x86_64.ini" --prefix=/usr --libdir=lib64 --buildtype=release \
        -Dlibraries=true -Dscanner=false -Dtests=false -Ddocumentation=false -Ddtd_validation=false
    ninja -C "$wayland" -j "$JOBS"
    DESTDIR="$SYSROOT" meson install -C "$wayland" --no-rebuild
    configure_meson "$xkb" "$ROOT/third_party/libxkbcommon" \
        --native-file "$native_file" \
        --cross-file "$SDK/cmake/openwave-x86_64.ini" --prefix=/usr --libdir=lib64 --buildtype=release \
        -Denable-x11=false -Denable-tools=false -Denable-wayland=false -Denable-xkbregistry=false \
        -Denable-docs=false -Denable-bash-completion=false -Dxkb-config-root=/usr/share/X11/xkb
    ninja -C "$xkb" -j "$JOBS"
    DESTDIR="$SYSROOT" meson install -C "$xkb" --no-rebuild
}

build_qt() {
    python3 "$ROOT/scripts/stage-openwave-compose.py" --sysroot "$SYSROOT" \
        --source "${OPENWAVE_X11_LOCALE_SOURCE:-/usr/share/X11/locale}" \
        --license "${OPENWAVE_X11_COMPOSE_LICENSE:-/usr/share/doc/libx11-data/copyright}" \
        --manifest "$DEPENDENCIES/x11-compose-source.json"
    local scanner="$SDK/bin/wayland-scanner"
    [[ -x "$scanner" && -f "$QT_SOURCE/qtwayland/CMakeLists.txt" ]]
    [[ -f "$QT_BUILD/host-build/CMakeCache.txt" && -f "$QT_BUILD/target-build/CMakeCache.txt" ]]
    # qtwaylandscanner belongs to QtBase, not the separate QtWayland module.
    native cmake -S "$QT_SOURCE" -B "$QT_BUILD/host-build" \
        -DFEATURE_qtwaylandscanner=ON -DWaylandScanner_EXECUTABLE="$scanner"
    native cmake --build "$QT_BUILD/host-build" --target qtwaylandscanner --parallel "$JOBS"
    native cmake --install "$QT_BUILD/host-build"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
    unset PKG_CONFIG_PATH
    cmake -S "$QT_SOURCE" -B "$QT_BUILD/target-build" \
        -DFEATURE_qtwaylandscanner=ON -DFEATURE_wayland=ON -DFEATURE_waylandscanner=ON \
        -DFEATURE_wayland_client=ON -DFEATURE_wayland_server=ON \
        -DFEATURE_xkbcommon=ON \
        -DWaylandScanner_EXECUTABLE="$scanner"
    # CMake's cached header scan can retain the pre-Wayland result for
    # qplatformwindow_p.h and omit QWaylandWindow's meta-object. Refresh only
    # this generated target cache after changing its feature configuration.
    rm -f -- "$QT_BUILD/target-build/qtbase/src/gui/CMakeFiles/Gui_autogen.dir/ParseCache.txt" \
        "$QT_BUILD/target-build/qtbase/src/gui/CMakeFiles/Gui_autogen.dir/AutogenUsed.txt"
    # Qt 6.10 also builds the Wayland client platform in QtBase. Build all
    # enabled targets before installing their newly generated export metadata.
    cmake --build "$QT_BUILD/target-build" --parallel "$JOBS"
    cmake --install "$QT_BUILD/target-build"
    local build="$QT_BUILD/wayland-target-build"
    cmake -S "$QT_SOURCE/qtwayland" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64 \
        -DCMAKE_TOOLCHAIN_FILE="$QT_BUILD/qt-target-toolchain.cmake" \
        -DQT_HOST_PATH="$SDK" -DQT_HOST_PATH_CMAKE_DIR="$SDK/lib/cmake" \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DWaylandScanner_EXECUTABLE="$scanner"
    cmake --build "$build" --parallel "$JOBS"
    DESTDIR="$SYSROOT" cmake --install "$build"
    [[ -f "$SYSROOT/usr/lib64/cmake/Qt6WaylandCompositor/Qt6WaylandCompositorConfig.cmake" ]]
    echo "QtWayland6.10.3 exported to $SYSROOT/usr"
}

case "${1:-all}" in
    dependencies) build_dependencies ;;
    target) build_qt ;;
    all) build_dependencies; build_qt ;;
    *) echo "Usage: $0 [dependencies|target|all]" >&2; exit 2 ;;
esac
