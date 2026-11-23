#!/usr/bin/env bash
# Install only the target libdbus-1 SDK into OpenWave's sysroot.
# Does not replace the runtime D-Bus daemon, alter SELinux policy, build
# userspace, assemble rootfs, or create an image.
# Usage: OPENWAVE_REPO_ROOT=/mnt/aosp/ThunderOS bash build-libdbus-openwave.sh
set -Eeuo pipefail

ROOT="${OPENWAVE_REPO_ROOT:-$(pwd)}"
ROOT="$(cd -- "$ROOT" && pwd)"
if [[ ! -f "$ROOT/env.sh" ]]; then
    printf 'Missing OpenWave env.sh in %s\n' "$ROOT" >&2
    exit 2
fi
source "$ROOT/env.sh" --build

SOURCE="$ROOT/third_party/dbus-1.16.2"
ARCHIVE_DIR="$ROOT/out/downloads"
ARCHIVE="$ARCHIVE_DIR/dbus-1.16.2.tar.xz"
URL="https://dbus.freedesktop.org/releases/dbus/dbus-1.16.2.tar.xz"
SHA256="0ba2a1a4b16afe7bceb2c07e9ce99a8c2c3508e5dec290dbb643384bd6beb7e2"
BUILD="$OPENWAVE_DEPENDENCY_BUILD/dbus-lib-only"
SYSROOT="$OPENWAVE_SYSROOT"
CROSS="$OPENWAVE_MESON_TOOLCHAIN"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"

if [[ ! -f "$CROSS" ]]; then
    printf 'Missing OpenWave target Meson cross file: %s\n' "$CROSS" >&2
    exit 2
fi

if [[ ! -f "$SOURCE/meson.build" ]]; then
    if [[ -e "$SOURCE" ]]; then
        printf 'Refusing to overwrite incomplete or nonstandard source path: %s\n' "$SOURCE" >&2
        exit 2
    fi
    mkdir -p "$ARCHIVE_DIR" "$ROOT/third_party"
    if [[ ! -f "$ARCHIVE" ]]; then
        curl --proto '=https' --tlsv1.2 --fail --location --retry 3 \
            --output "$ARCHIVE.partial" "$URL"
        mv -- "$ARCHIVE.partial" "$ARCHIVE"
    fi
    printf '%s  %s\n' "$SHA256" "$ARCHIVE" | sha256sum -c -
    tar -xJf "$ARCHIVE" -C "$ROOT/third_party"
fi

export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=""

# The installed OpenWave D-Bus daemon is intentionally not rebuilt here.
# These flags only apply to the cross-compiled client library.
opts=(
    --buildtype=release
    --wrap-mode=nofallback
    -Ddefault_library=shared
    -Dmessage_bus=false
    -Dtools=false
    -Dsystemd=disabled
    -Dapparmor=disabled
    -Dselinux=disabled
    -Dlibaudit=disabled
    -Dx11_autolaunch=disabled
    -Dmodular_tests=disabled
    -Dxml_docs=disabled
    -Ddoxygen_docs=disabled
    -Dducktype_docs=disabled
    -Dqt_help=disabled
)

if [[ -f "$BUILD/build.ninja" ]]; then
    meson setup --reconfigure "$BUILD" "$SOURCE" "${opts[@]}"
else
    meson setup "$BUILD" "$SOURCE" \
        --cross-file "$CROSS" --prefix=/usr --libdir=lib64 "${opts[@]}"
fi
meson compile -C "$BUILD" -j "$JOBS"
DESTDIR="$SYSROOT" meson install -C "$BUILD" --no-rebuild

for required in \
    "$SYSROOT/usr/lib64/pkgconfig/dbus-1.pc" \
    "$SYSROOT/usr/include/dbus-1.0/dbus/dbus.h" \
    "$SYSROOT/usr/lib64/libdbus-1.so"; do
    if [[ ! -e "$required" ]]; then
        printf 'Missing target D-Bus development file: %s\n' "$required" >&2
        exit 1
    fi
done

pkg-config --modversion dbus-1
pkg-config --cflags --libs dbus-1
printf 'PASS: Target libdbus-1 SDK installed in %s\n' "$SYSROOT"
