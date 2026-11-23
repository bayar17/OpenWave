#!/usr/bin/env bash

if [ -n "${BASH_VERSION:-}" ]; then
    _openwave_self="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
    _openwave_self="${(%):-%x}"
else
    _openwave_self="$0"
fi
_openwave_root="$(cd -- "$(dirname -- "$_openwave_self")" >/dev/null 2>&1 && pwd -P)" || return 1
unset _openwave_self

if [ -n "${OPENWAVE_SDK:-}" ] && [ "${OPENWAVE_SDK_ACTIVE:-}" = 1 ] && command -v openwave_env_deactivate >/dev/null 2>&1; then
    openwave_env_deactivate
fi

OPENWAVE_SDK="$_openwave_root"
unset _openwave_root
if [ -z "${OPENWAVE_TARGET_TRIPLE:-}" ]; then
    OPENWAVE_TARGET_TRIPLE="x86_64-unknown-openwave"
    if [ ! -d "$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE" ]; then
        for _openwave_dir in "$OPENWAVE_SDK"/sysroot/*; do
            [ -d "$_openwave_dir" ] && OPENWAVE_TARGET_TRIPLE="$(basename "$_openwave_dir")"
        done
        unset _openwave_dir
    fi
fi
OPENWAVE_SYSROOT="$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE"
_openwave_arch="${OPENWAVE_TARGET_TRIPLE%%-*}"
OPENWAVE_CMAKE_TOOLCHAIN="$OPENWAVE_SDK/cmake/openwave-$_openwave_arch.cmake"
OPENWAVE_MESON_TOOLCHAIN="$OPENWAVE_SDK/cmake/openwave-$_openwave_arch.ini"
unset _openwave_arch
OPENWAVE_QT_HOST="$OPENWAVE_SDK"
OPENWAVE_SDK_CMAKE_DIR="$OPENWAVE_SDK/lib/cmake/OpenWaveSDK"
OPENWAVE_QML_IMPORT_PATH="$OPENWAVE_SYSROOT/usr/qml:$OPENWAVE_SYSROOT/usr/lib64/qt6/qml"
OPENWAVE_BLUEPRINTS="$OPENWAVE_SDK/share/openwave/blueprints"

for _openwave_required in "$OPENWAVE_SDK/bin/clang" "$OPENWAVE_SYSROOT/usr/include" "$OPENWAVE_CMAKE_TOOLCHAIN"; do
    if [ ! -e "$_openwave_required" ]; then
        printf 'OpenWave SDK at %s is incomplete: missing %s\n' "$OPENWAVE_SDK" "$_openwave_required" >&2
        unset _openwave_required OPENWAVE_SDK OPENWAVE_TARGET_TRIPLE OPENWAVE_SYSROOT OPENWAVE_CMAKE_TOOLCHAIN \
              OPENWAVE_MESON_TOOLCHAIN OPENWAVE_QT_HOST OPENWAVE_SDK_CMAKE_DIR OPENWAVE_QML_IMPORT_PATH OPENWAVE_BLUEPRINTS
        return 1
    fi
done
unset _openwave_required

_OPENWAVE_SAVED_PATH="$PATH"
_OPENWAVE_SAVED_TOOLS="${CC-}|${CXX-}|${AR-}|${NM-}|${RANLIB-}|${STRIP-}|${OBJCOPY-}|${OBJDUMP-}|${READELF-}|${LD-}"
_OPENWAVE_SAVED_CMAKE="${CMAKE_TOOLCHAIN_FILE-}|${CMAKE_PREFIX_PATH-}|${QT_HOST_PATH-}|${QT_HOST_PATH_CMAKE_DIR-}|${OpenWaveSDK_DIR-}"
_OPENWAVE_SAVED_PKG="${PKG_CONFIG_LIBDIR-}|${PKG_CONFIG_SYSROOT_DIR-}"

case ":$PATH:" in
    ":$OPENWAVE_SDK/bin:"*) ;;
    *) PATH="$OPENWAVE_SDK/bin:$PATH" ;;
esac
CC="$OPENWAVE_SDK/bin/clang"
CXX="$OPENWAVE_SDK/bin/clang++"
AR="$OPENWAVE_SDK/bin/llvm-ar"
NM="$OPENWAVE_SDK/bin/llvm-nm"
RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib"
STRIP="$OPENWAVE_SDK/bin/llvm-strip"
OBJCOPY="$OPENWAVE_SDK/bin/llvm-objcopy"
OBJDUMP="$OPENWAVE_SDK/bin/llvm-objdump"
READELF="$OPENWAVE_SDK/bin/llvm-readelf"
LD="$OPENWAVE_SDK/bin/ld.lld"
CMAKE_TOOLCHAIN_FILE="$OPENWAVE_CMAKE_TOOLCHAIN"
CMAKE_PREFIX_PATH="$OPENWAVE_SYSROOT/usr:$OPENWAVE_SDK"
QT_HOST_PATH="$OPENWAVE_QT_HOST"
QT_HOST_PATH_CMAKE_DIR="$QT_HOST_PATH/lib/cmake"
OpenWaveSDK_DIR="$OPENWAVE_SDK_CMAKE_DIR"
PKG_CONFIG_LIBDIR="$OPENWAVE_SYSROOT/usr/lib64/pkgconfig:$OPENWAVE_SYSROOT/usr/share/pkgconfig"
PKG_CONFIG_SYSROOT_DIR="$OPENWAVE_SYSROOT"
OPENWAVE_SDK_ACTIVE=1

export OPENWAVE_SDK OPENWAVE_TARGET_TRIPLE OPENWAVE_SYSROOT OPENWAVE_CMAKE_TOOLCHAIN OPENWAVE_MESON_TOOLCHAIN \
       OPENWAVE_QT_HOST OPENWAVE_SDK_CMAKE_DIR OPENWAVE_QML_IMPORT_PATH OPENWAVE_BLUEPRINTS OPENWAVE_SDK_ACTIVE \
       PATH CC CXX AR NM RANLIB STRIP OBJCOPY OBJDUMP READELF LD \
       CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH QT_HOST_PATH QT_HOST_PATH_CMAKE_DIR OpenWaveSDK_DIR \
       PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR

openwave_env() {
    printf '%s\n' \
        "OPENWAVE_SDK=$OPENWAVE_SDK" \
        "OPENWAVE_TARGET_TRIPLE=$OPENWAVE_TARGET_TRIPLE" \
        "OPENWAVE_SYSROOT=$OPENWAVE_SYSROOT" \
        "OPENWAVE_CMAKE_TOOLCHAIN=$OPENWAVE_CMAKE_TOOLCHAIN" \
        "OPENWAVE_MESON_TOOLCHAIN=$OPENWAVE_MESON_TOOLCHAIN" \
        "OPENWAVE_QT_HOST=$OPENWAVE_QT_HOST" \
        "OPENWAVE_SDK_CMAKE_DIR=$OPENWAVE_SDK_CMAKE_DIR" \
        "OPENWAVE_QML_IMPORT_PATH=$OPENWAVE_QML_IMPORT_PATH" \
        "OPENWAVE_BLUEPRINTS=$OPENWAVE_BLUEPRINTS" \
        "CC=$CC" "CXX=$CXX" \
        "CMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE" \
        "CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH" \
        "QT_HOST_PATH=$QT_HOST_PATH"
}

openwave_env_deactivate() {
    PATH="$_OPENWAVE_SAVED_PATH"
    _openwave_restore() {
        if [ -n "$2" ]; then export "$1=$2"; else unset "$1"; fi
    }
    _openwave_split() { printf '%s' "$1" | cut -d'|' -f"$2"; }
    _openwave_i=1
    for _openwave_name in CC CXX AR NM RANLIB STRIP OBJCOPY OBJDUMP READELF LD; do
        _openwave_restore "$_openwave_name" "$(_openwave_split "$_OPENWAVE_SAVED_TOOLS" $_openwave_i)"
        _openwave_i=$((_openwave_i + 1))
    done
    _openwave_i=1
    for _openwave_name in CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH QT_HOST_PATH QT_HOST_PATH_CMAKE_DIR OpenWaveSDK_DIR; do
        _openwave_restore "$_openwave_name" "$(_openwave_split "$_OPENWAVE_SAVED_CMAKE" $_openwave_i)"
        _openwave_i=$((_openwave_i + 1))
    done
    _openwave_i=1
    for _openwave_name in PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR; do
        _openwave_restore "$_openwave_name" "$(_openwave_split "$_OPENWAVE_SAVED_PKG" $_openwave_i)"
        _openwave_i=$((_openwave_i + 1))
    done
    unset _openwave_i _openwave_name _openwave_restore _openwave_split
    unset OPENWAVE_SDK OPENWAVE_TARGET_TRIPLE OPENWAVE_SYSROOT OPENWAVE_CMAKE_TOOLCHAIN OPENWAVE_MESON_TOOLCHAIN \
          OPENWAVE_QT_HOST OPENWAVE_SDK_CMAKE_DIR OPENWAVE_QML_IMPORT_PATH OPENWAVE_BLUEPRINTS OPENWAVE_SDK_ACTIVE \
          _OPENWAVE_SAVED_PATH _OPENWAVE_SAVED_TOOLS _OPENWAVE_SAVED_CMAKE _OPENWAVE_SAVED_PKG
    unset -f openwave_env openwave_env_deactivate
}
