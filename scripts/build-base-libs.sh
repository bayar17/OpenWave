#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build

SYSROOT="$OPENWAVE_SYSROOT"
DEST="${OPENWAVE_BASELIB_DESTDIR:-$SYSROOT}"
BUILD_ROOT="$OPENWAVE_DEPENDENCY_BUILD/base-libs"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"
TRIPLE="$OPENWAVE_TARGET_TRIPLE"
log() { printf '[base-libs] %s\n' "$*"; }
die() { printf '[base-libs] ERROR: %s\n' "$*" >&2; exit 1; }

[[ -x "$OPENWAVE_SDK/bin/clang" ]] || die "no target Clang in $OPENWAVE_SDK/bin"
[[ -f "$SYSROOT/usr/include/zlib.h" ]] || die "zlib is not in the sysroot (./build-openwave graphics)"
mkdir -p "$BUILD_ROOT" "$DEST"

export CC="$OPENWAVE_SDK/bin/clang --target=$TRIPLE --sysroot=$SYSROOT -rtlib=compiler-rt"
export CXX="$OPENWAVE_SDK/bin/clang++ --target=$TRIPLE --sysroot=$SYSROOT -rtlib=compiler-rt"
export AR="$OPENWAVE_SDK/bin/llvm-ar" RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib" NM="$OPENWAVE_SDK/bin/llvm-nm" \
       STRIP="$OPENWAVE_SDK/bin/llvm-strip" LD="$OPENWAVE_SDK/bin/ld.lld"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=

source_of() { local dir="$ROOT/third_party/$1"; [[ -d "$dir" ]] || die "missing source: $dir"; printf '%s' "$dir"; }
autotools() {
    local name="$1" source="$2"; shift 2
    local build="$BUILD_ROOT/$name"
    rm -rf "$build"; mkdir -p "$build"
    (cd "$build" && "$source/configure" --host="$OPENWAVE_AUTOTOOLS_HOST" --prefix=/usr --libdir=/usr/lib64 "$@")
    make -C "$build" -j "$JOBS"
    make -C "$build" DESTDIR="$DEST" install
}

build_openssl() {
    local source build="$BUILD_ROOT/openssl"; source="$(source_of openssl-3.5.9)"
    rm -rf "$build"; mkdir -p "$build"
    (cd "$build" && "$source/Configure" linux-x86_64 --prefix=/usr --openssldir=/etc/ssl --libdir=lib64 \
        shared no-tests no-docs no-module no-fips)
    make -C "$build" -j "$JOBS"
    make -C "$build" DESTDIR="$DEST" install_sw install_ssldirs
}

build_pcre2() {
    autotools pcre2 "$(source_of pcre2)" --enable-static --disable-shared --with-pic \
        --enable-pcre2-16 --enable-pcre2-32 --disable-pcre2grep-libz --disable-pcre2grep-libbz2 --disable-pcre2test-libedit
}

build_libxcrypt() {
    autotools libxcrypt "$(source_of libxcrypt-4.4.38)" --disable-static --enable-hashes=strong,glibc \
        --enable-obsolete-api=glibc --disable-werror
}

build_libffi() {
    local version=3.5.2 sha=f3a3082a23b37c293a4fcd1053147b371f2ff91fa7ea1b2a52e335676bac82dc
    local archive="$ROOT/out/downloads/libffi-$version.tar.gz" source="$BUILD_ROOT/libffi-$version"
    mkdir -p "$ROOT/out/downloads"
    if [[ ! -f "$archive" ]]; then
        curl --proto '=https' --tlsv1.2 --fail --location --retry 3 -o "$archive.partial" \
            "https://github.com/libffi/libffi/releases/download/v$version/libffi-$version.tar.gz"
        mv -- "$archive.partial" "$archive"
    fi
    printf '%s  %s\n' "$sha" "$archive" | sha256sum -c - >/dev/null || die "libffi $version checksum mismatch"
    rm -rf "$source"; tar -xzf "$archive" -C "$BUILD_ROOT"
    autotools libffi "$source" --disable-static --disable-docs --disable-multi-os-directory
}

build_libpng() {
    autotools libpng "$(source_of libpng)" --disable-static
}

build_libjpeg_turbo() {
    local source build="$BUILD_ROOT/libjpeg-turbo"; source="$(source_of libjpeg-turbo)"
    rm -rf "$build"
    cmake -S "$source" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$OPENWAVE_CMAKE_TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64 \
        -DENABLE_STATIC=OFF -DENABLE_SHARED=ON -DWITH_TURBOJPEG=ON -DWITH_JPEG8=1 -DWITH_JAVA=OFF
    cmake --build "$build" --parallel "$JOBS"
    DESTDIR="$DEST" cmake --install "$build"
}

case "${1:-all}" in
    openssl) build_openssl ;;
    pcre2) build_pcre2 ;;
    libxcrypt) build_libxcrypt ;;
    libffi) build_libffi ;;
    libpng) build_libpng ;;
    libjpeg-turbo) build_libjpeg_turbo ;;
    all) build_openssl; build_pcre2; build_libxcrypt; build_libffi; build_libpng; build_libjpeg_turbo ;;
    -h|--help) sed -n 2,11p "$0" | sed 's/^# \{0,1\}//' ;;
    *) die "unknown library: $1" ;;
esac
log "done; installed into $DEST"
