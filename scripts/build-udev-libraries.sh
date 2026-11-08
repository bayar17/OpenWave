#!/usr/bin/env bash
# Build target libudev (eudev) and, when target GLib is ready, libgudev.
# No systemd init, udevd, or host libraries are installed.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build

: "${OPENWAVE_SDK:=$ROOT/toolchain}"
: "${OPENWAVE_TARGET_TRIPLE:=x86_64-unknown-openwave}"
: "${OPENWAVE_SYSROOT:=$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE}"
: "${OPENWAVE_MESON_TOOLCHAIN:=$OPENWAVE_SDK/cmake/openwave-${OPENWAVE_TARGET_TRIPLE%%-*}.ini}"
: "${OPENWAVE_DEPENDENCY_BUILD:=$ROOT/out/sdk-dependencies/$OPENWAVE_TARGET_TRIPLE}"
: "${OPENWAVE_AUTOTOOLS_HOST:=${OPENWAVE_TARGET_TRIPLE%%-*}-unknown-linux-gnu}"

E_VERSION=3.2.15
E_SHA256=eb69809fc0d1187f2463ab2149c53aa9f42a78e59f14ebea68eae6cb6b4fc1c3
E_SOURCE="${OPENWAVE_EUDEV_SOURCE:-$ROOT/third_party/eudev}"
E_BUILD="$OPENWAVE_DEPENDENCY_BUILD/eudev"
GU_SOURCE="${OPENWAVE_LIBGUDEV_SOURCE:-$ROOT/third_party/gstreamer/subprojects/libgudev-238}"
GU_BUILD="$OPENWAVE_DEPENDENCY_BUILD/libgudev"
SYSROOT="$OPENWAVE_SYSROOT"

export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

log() { printf '[openwave-udev] %s\n' "$*"; }
die() { printf '[openwave-udev] ERROR: %s\n' "$*" >&2; exit 1; }

[[ -x "$OPENWAVE_SDK/bin/clang" ]] || die "Missing target Clang in $OPENWAVE_SDK/bin"
[[ -d "$SYSROOT/usr/include" ]] || die "Missing target sysroot; bootstrap it first"

# Install from a pinned, SHA256-verified upstream release if not yet vendored.
if [[ ! -f "$E_SOURCE/configure" ]]; then
    if [[ -d "$E_SOURCE" ]]; then
        die "Existing $E_SOURCE lacks configure; supply a release tarball or generate configure using autoreconf"
    fi
    command -v curl >/dev/null || die 'curl is required to obtain eudev'
    command -v sha256sum >/dev/null || die 'sha256sum is required to verify eudev'
    archive="$ROOT/third_party/eudev-$E_VERSION.tar.gz"
    mkdir -p "$ROOT/third_party"
    if [[ ! -f "$archive" ]]; then
        log "Downloading pinned eudev $E_VERSION source archive"
        curl --fail --location --retry 2 \
            "https://github.com/eudev-project/eudev/releases/download/v$E_VERSION/eudev-$E_VERSION.tar.gz" \
            --output "$archive"
    fi
    printf '%s  %s\n' "$E_SHA256" "$archive" | sha256sum --check --status || die 'eudev source checksum mismatch'
    tar -xzf "$archive" -C "$ROOT/third_party"
    mv "$ROOT/third_party/eudev-$E_VERSION" "$E_SOURCE"
fi

export CC="$OPENWAVE_SDK/bin/clang --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$SYSROOT -rtlib=compiler-rt"
export CXX="$OPENWAVE_SDK/bin/clang++ --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$SYSROOT -rtlib=compiler-rt"
export AR="$OPENWAVE_SDK/bin/llvm-ar"
export RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib"
export NM="$OPENWAVE_SDK/bin/llvm-nm"
export STRIP="$OPENWAVE_SDK/bin/llvm-strip"

if pkg-config --exists 'libudev >= 251' && [[ "${OPENWAVE_UDEV_FORCE_REBUILD:-0}" != 1 ]]; then
    log "Reusing target libudev $(pkg-config --modversion libudev)"
else
    mkdir -p "$E_BUILD"
    log 'Configuring eudev libudev (daemon and helpers disabled)'
    if [[ ! -f "$E_BUILD/Makefile" ]]; then
        (
          cd "$E_BUILD"
          "$E_SOURCE/configure" \
            --host="$OPENWAVE_AUTOTOOLS_HOST" \
            --prefix=/usr \
            --libdir=/usr/lib64 \
            --with-rootlibdir=/usr/lib64 \
            --disable-programs \
            --disable-kmod \
            --disable-blkid \
            --disable-hwdb
        )
    fi
    make -C "$E_BUILD" -j "${OPENWAVE_BUILD_JOBS:-4}"
    make -C "$E_BUILD" DESTDIR="$SYSROOT" install
fi
pkg-config --exists 'libudev >= 251' || die "libudev.pc missing or libudev version below 251 after installation"
[[ -f "$SYSROOT/usr/include/libudev.h" ]] || die 'libudev.h missing from target sysroot'
log "Target libudev $(pkg-config --modversion libudev) ready"

if ! pkg-config --exists glib-2.0 gobject-2.0; then
    log 'Target GLib/GObject development files are not installed in sysroot.'
    log 'libudev is ready. GStreamer can build its bundled libgudev-238 against its bundled GLib.'
    log 'For standalone libgudev installation, first install target GLib/GObject, then rerun this script.'
    exit 0
fi

[[ -f "$GU_SOURCE/meson.build" ]] || die "Missing libgudev 238 source tree: $GU_SOURCE"
[[ -f "$OPENWAVE_MESON_TOOLCHAIN" ]] || die "Missing Meson cross-file: $OPENWAVE_MESON_TOOLCHAIN"

log 'Configuring libgudev 238 against target libudev and GLib'
args=(--cross-file "$OPENWAVE_MESON_TOOLCHAIN" --prefix=/usr --libdir=lib64
      --buildtype=release --wrap-mode=nofallback
      -Dtests=disabled -Dintrospection=disabled -Dvapi=disabled -Dgtk_doc=false)
if [[ -f "$GU_BUILD/build.ninja" ]]; then
    meson setup --reconfigure "$GU_BUILD" "$GU_SOURCE" "${args[@]}"
else
    meson setup "$GU_BUILD" "$GU_SOURCE" "${args[@]}"
fi
meson compile -C "$GU_BUILD" -j "${OPENWAVE_BUILD_JOBS:-4}"
DESTDIR="$SYSROOT" meson install -C "$GU_BUILD" --no-rebuild
pkg-config --exists gudev-1.0 || die 'gudev-1.0.pc missing from target sysroot'
log "Target libgudev $(pkg-config --modversion gudev-1.0) ready"

