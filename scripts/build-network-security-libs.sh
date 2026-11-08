#!/usr/bin/env bash
# OpenWave target libsepol/libselinux/libmnl dependencies for iproute2.
# No rootfs assembly, image creation, policy generation, or host installation.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build
: "${OPENWAVE_SDK:=$ROOT/toolchain}"
: "${OPENWAVE_TARGET_TRIPLE:=x86_64-unknown-openwave}"
: "${OPENWAVE_SYSROOT:=$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE}"
: "${OPENWAVE_DEPENDENCY_BUILD:=$ROOT/out/sdk-dependencies/$OPENWAVE_TARGET_TRIPLE}"

SELINUX_VERSION=3.11
MNL_VERSION=1.0.5
SEPOL_SHA256=79f3d2c88f44b7eb5cf54d9792e03232297e17f97a179163f2750099a00f164d
SELINUX_SHA256=73d419c6e20e874adaa4019372cbd097eecf4d276e13f27ec5e67d35c0bd203c
MNL_SHA256=274b9b919ef3152bfb3da3a13c950dd60d6e2bcd54230ffeca298d03b40d0525

export PKG_CONFIG_SYSROOT_DIR="$OPENWAVE_SYSROOT"
export PKG_CONFIG_LIBDIR="$OPENWAVE_SYSROOT/usr/lib64/pkgconfig:$OPENWAVE_SYSROOT/usr/lib/pkgconfig:$OPENWAVE_SYSROOT/usr/share/pkgconfig"
unset PKG_CONFIG_PATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH

has_pkg() { pkg-config --exists "$1"; }
verify() {
    local failed=0 package
    for package in libpcre2-8 libsepol libselinux libmnl; do
        if has_pkg "$package"; then
            printf '%-14s %s\n' "$package" "$(pkg-config --modversion "$package")"
        else
            printf '%-14s %s\n' "$package" MISSING >&2
            failed=1
        fi
    done
    for package in libselinux libmnl; do
        if [[ ! -f "$OPENWAVE_SYSROOT/usr/lib64/$package.so" ]]; then
            echo "Missing linker library: $OPENWAVE_SYSROOT/usr/lib64/$package.so" >&2
            failed=1
        fi
    done
    return "$failed"
}

case "${1:-}" in
    --check) verify; exit $? ;;
    --help|-h)
        echo "Usage: scripts/build-network-security-libs.sh [--check]"
        echo 'Build libsepol 3.11, libselinux 3.11 and libmnl 1.0.5 into the target sysroot only.'
        exit 0 ;;
    '') ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo 'Too many arguments' >&2; exit 2; }

command -v curl >/dev/null
command -v sha256sum >/dev/null
command -v pkg-config >/dev/null
command -v make >/dev/null
[[ -x "$OPENWAVE_SDK/bin/clang" ]] || { echo 'Target Clang not found' >&2; exit 1; }
[[ -d "$OPENWAVE_SYSROOT/usr/include" ]] || { echo 'Target sysroot headers missing' >&2; exit 1; }
if ! has_pkg libpcre2-8; then
    echo 'Target PCRE2 (libpcre2-8.pc, header and library) must be installed before libselinux.' >&2
    exit 1
fi
command -v flex >/dev/null || { echo 'Host flex is required to build libselinux.' >&2; exit 1; }

fetch() {
    local archive="$1" url="$2" checksum="$3"
    if [[ ! -f "$archive" ]]; then
        curl -fL --retry 3 "$url" -o "$archive.partial"
        printf '%s  %s\n' "$checksum" "$archive.partial" | sha256sum -c -
        mv -- "$archive.partial" "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum -c -
}

# Use OpenWave's existing, proven SELinux 3.11 library build recipe.
selinux_build="$OPENWAVE_DEPENDENCY_BUILD/selinux-$SELINUX_VERSION"
mkdir -p "$selinux_build" "$ROOT/third_party"

printf -v cc '%q --target=%q --sysroot=%q' "$OPENWAVE_SDK/bin/clang" "$OPENWAVE_TARGET_TRIPLE" "$OPENWAVE_SYSROOT"
printf -v ar '%q' "$OPENWAVE_SDK/bin/llvm-ar"
printf -v ranlib '%q' "$OPENWAVE_SDK/bin/llvm-ranlib"
cflags='-O2 -std=gnu17 -fPIC -fstack-protector-strong'
case "$OPENWAVE_TARGET_TRIPLE" in x86_64-*) cflags+=' -mstack-protector-guard=tls' ;; esac
selinux_options=("CC=$cc" "AR=$ar" "RANLIB=$ranlib" "CFLAGS=$cflags"
    'CPPFLAGS=-I../../install/usr/include' 'LDFLAGS=-Wl,-z,relro,-z,now'
    OS=Linux "ARCH=${OPENWAVE_TARGET_TRIPLE%%-*}"
    PREFIX=/usr LIBDIR=/usr/lib64 SHLIBDIR=/usr/lib64)

for package in libsepol libselinux; do
    case "$package" in
        libsepol) checksum="$SEPOL_SHA256" ;;
        libselinux) checksum="$SELINUX_SHA256" ;;
    esac
    archive="$selinux_build/$package-$SELINUX_VERSION.tar.gz"
    fetch "$archive" \
        "https://github.com/SELinuxProject/selinux/releases/download/$SELINUX_VERSION/$package-$SELINUX_VERSION.tar.gz" \
        "$checksum"
    if [[ ! -d "$ROOT/third_party/$package-$SELINUX_VERSION" ]]; then
        tar -xf "$archive" -C "$ROOT/third_party"
    fi
    if [[ ! -d "$selinux_build/$package-$SELINUX_VERSION" ]]; then
        cp -a "$ROOT/third_party/$package-$SELINUX_VERSION" "$selinux_build/"
    fi
    source_build="$selinux_build/$package-$SELINUX_VERSION"
    make -C "$source_build/src" "${selinux_options[@]}" clean
    if [[ "$package" == libsepol ]]; then
        make -C "$source_build/src" "${selinux_options[@]}" -j "${OPENWAVE_BUILD_JOBS:-4}"
        make -C "$source_build/src" "${selinux_options[@]}" DESTDIR=../../install install
        make -C "$source_build/include" "${selinux_options[@]}" DESTDIR=../../install install
    else
        make -C "$source_build" "${selinux_options[@]}" \
            EXTRA_LD_FLAGS=--undefined-version SUBDIRS=src -j "${OPENWAVE_BUILD_JOBS:-4}"
        make -C "$source_build" "${selinux_options[@]}" \
            EXTRA_LD_FLAGS=--undefined-version SUBDIRS='src include' \
            DESTDIR=../../install install
    fi
done
# Import only the target library/header stage; do not generate policy or rootfs.
cp -a "$selinux_build/install/usr/." "$OPENWAVE_SYSROOT/usr/"

# libmnl is independent of SELinux but required by the iproute2 link flags.
mnl_build="$OPENWAVE_DEPENDENCY_BUILD/libmnl-$MNL_VERSION"
mnl_archive="$mnl_build/libmnl-$MNL_VERSION.tar.bz2"
mkdir -p "$mnl_build" "$ROOT/third_party"
fetch "$mnl_archive" \
    "https://www.netfilter.org/projects/libmnl/files/libmnl-$MNL_VERSION.tar.bz2" \
    "$MNL_SHA256"
mnl_source="$ROOT/third_party/libmnl-$MNL_VERSION"
if [[ ! -d "$mnl_source" ]]; then
    tar -xjf "$mnl_archive" -C "$ROOT/third_party"
fi
mnl_out="$mnl_build/build"
mkdir -p "$mnl_out"
(
    cd "$mnl_out"
    CC="$OPENWAVE_SDK/bin/clang --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$OPENWAVE_SYSROOT" \
    AR="$OPENWAVE_SDK/bin/llvm-ar" \
    RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib" \
    CFLAGS='-O2 -fPIC' \
    LDFLAGS='-fuse-ld=lld -rtlib=compiler-rt' \
    "$mnl_source/configure" \
        --host="${OPENWAVE_TARGET_TRIPLE%%-*}-unknown-linux-gnu" \
        --prefix=/usr --libdir=/usr/lib64 \
        --enable-shared --disable-static
    make -j "${OPENWAVE_BUILD_JOBS:-4}"
    make DESTDIR="$OPENWAVE_SYSROOT" install
)

echo 'Target networking/security libraries:'
verify
printf '\nDone. No OpenWave rootfs or image was assembled.\n'
