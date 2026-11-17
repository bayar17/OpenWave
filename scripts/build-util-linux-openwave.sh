#!/usr/bin/env bash
set -Eeuo pipefail

# Standalone OpenWave util-linux mount/umount builder.
# Build stage never modifies rootfs or produces a disk image.
# Stage-rootfs is explicit and refuses to overwrite different existing files.

usage() {
    cat <<'USAGE'
Usage: OPENWAVE_REPO_ROOT=/path/to/ThunderOS bash build-util-linux-openwave.sh build
       OPENWAVE_REPO_ROOT=/path/to/ThunderOS bash build-util-linux-openwave.sh stage-rootfs
       OPENWAVE_REPO_ROOT=/path/to/ThunderOS bash build-util-linux-openwave.sh use-existing-stage /absolute/path/to/util-linux-stage

build: cross-build util-linux mount/umount and their 3 libraries to out/sdk-dependencies/.../util-linux-openwave/stage
stage-rootfs: explicitly copy that stage into rootfs (after checking for collisions)
use-existing-stage: inspect and copy a previously built OpenWave util-linux stage into the dedicated stage, without building or changing rootfs
USAGE
}

ACTION="${1:-}"
case "$ACTION" in
    build|stage-rootfs|use-existing-stage) shift ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

ROOT="${OPENWAVE_REPO_ROOT:-$PWD}"
ROOT="$(cd -- "$ROOT" && pwd -P)"
[[ -f "$ROOT/build-openwave" && -d "$ROOT/userspace" ]] || { echo 'Not the OpenWave repository; set OPENWAVE_REPO_ROOT' >&2; exit 1; }
TARGET="${OPENWAVE_TARGET_TRIPLE:-x86_64-unknown-openwave}"
SDK="${OPENWAVE_SDK:-$ROOT/toolchain}"
SYSROOT="$SDK/sysroot/$TARGET"
SOURCE="${OPENWAVE_UTIL_LINUX_SOURCE:-$ROOT/third_party/util-linux}"
OUTPUT="$ROOT/out/sdk-dependencies/$TARGET/util-linux-openwave"
BUILD="$OUTPUT/build"
STAGE="$OUTPUT/stage"
ROOTFS="$ROOT/rootfs"
LIBRARIES=(libuuid.so.1.3.0 libblkid.so.1.1.0 libmount.so.1.1.0)

fail() { echo "util-linux: $*" >&2; exit 1; }

check_stage() {
    local origin="$1" file
    for file in usr/bin/mount usr/bin/umount; do
        [[ -f "$origin/$file" && -x "$origin/$file" ]] || fail "missing $origin/$file"
        "$SDK/bin/llvm-readelf" -h "$origin/$file" | grep -Fq 'Advanced Micro Devices X86-64' || fail "wrong ELF architecture: $file"
        "$SDK/bin/llvm-readelf" -l "$origin/$file" | grep -Fq '/lib64/ld-linux-x86-64.so.2' || fail "wrong runtime interpreter: $file"
    done
    for file in "${LIBRARIES[@]}"; do
        [[ -f "$origin/usr/lib64/$file" ]] || fail "missing $origin/usr/lib64/$file"
    done
    for file in libuuid.so.1 libblkid.so.1 libmount.so.1; do
        [[ -L "$origin/usr/lib64/$file" ]] || fail "missing SONAME symlink $file"
    done
}

if [[ "$ACTION" == build ]]; then
    [[ -x "$SDK/bin/clang" && -x "$SDK/bin/llvm-ar" && -x "$SDK/bin/llvm-ranlib" && -x "$SDK/bin/llvm-readelf" ]] || fail 'incomplete OpenWave LLVM toolchain'
    [[ -d "$SYSROOT/usr/include" ]] || fail "missing target sysroot: $SYSROOT"
    [[ -f "$SOURCE/configure" ]] || fail "missing util-linux source configure script: $SOURCE/configure"
    for tool in make pkg-config install; do command -v "$tool" >/dev/null || fail "missing host tool $tool"; done
    mkdir -p "$BUILD" "$STAGE/usr/bin" "$STAGE/usr/lib64"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
    export PKG_CONFIG_PATH=''
    (
        cd "$BUILD"
        CC="$SDK/bin/clang --target=$TARGET --sysroot=$SYSROOT" \
        AR="$SDK/bin/llvm-ar" \
        RANLIB="$SDK/bin/llvm-ranlib" \
        CFLAGS="--target=$TARGET --sysroot=$SYSROOT -O2 -I$SYSROOT/usr/include" \
        LDFLAGS="--target=$TARGET --sysroot=$SYSROOT -L$SYSROOT/usr/lib64 -L$SYSROOT/lib64" \
        "$SOURCE/configure" \
            --host=x86_64-linux-gnu --prefix=/usr --bindir=/usr/bin --sbindir=/sbin \
            --libdir=/usr/lib64 --disable-nls --disable-all-programs \
            --enable-mount --enable-libmount --enable-libblkid --enable-libuuid
    )
    make -C "$BUILD" -j "${OPENWAVE_BUILD_JOBS:-4}"
    install -m 0755 "$BUILD/.libs/mount" "$STAGE/usr/bin/mount"
    install -m 0755 "$BUILD/.libs/umount" "$STAGE/usr/bin/umount"
    for file in "${LIBRARIES[@]}"; do
        install -m 0755 "$BUILD/.libs/$file" "$STAGE/usr/lib64/$file"
    done
    ln -sfn libuuid.so.1.3.0 "$STAGE/usr/lib64/libuuid.so.1"
    ln -sfn libblkid.so.1.1.0 "$STAGE/usr/lib64/libblkid.so.1"
    ln -sfn libmount.so.1.1.0 "$STAGE/usr/lib64/libmount.so.1"
    check_stage "$STAGE"
    echo "PASS: util-linux mount/umount compiled and staged at $STAGE (rootfs untouched)"

elif [[ "$ACTION" == use-existing-stage ]]; then
    [[ $# -eq 1 ]] || fail 'pass exactly one existing absolute stage path'
    ORIGIN="$1"
    [[ "$ORIGIN" == /* ]] || fail 'stage must be an absolute path'
    [[ "$ORIGIN" == "$ROOT"/out/* ]] || fail 'stage must be under OpenWave out/'
    [[ "$ORIGIN" != "$STAGE" ]] || fail 'stage already selected'
    [[ -x "$SDK/bin/llvm-readelf" ]] || fail 'missing llvm-readelf'
    check_stage "$ORIGIN"
    mkdir -p "$STAGE/usr/bin" "$STAGE/usr/lib64"
    for file in mount umount; do
        [[ ! -e "$STAGE/usr/bin/$file" || -f "$STAGE/usr/bin/$file" && ! -L "$STAGE/usr/bin/$file" ]] || fail "unsafe staging path: $file"
        install -m 0755 "$ORIGIN/usr/bin/$file" "$STAGE/usr/bin/$file"
    done
    for file in "${LIBRARIES[@]}"; do
        [[ ! -L "$STAGE/usr/lib64/$file" ]] || fail "unsafe staging library: $file"
        install -m 0755 "$ORIGIN/usr/lib64/$file" "$STAGE/usr/lib64/$file"
    done
    for file in libuuid.so.1 libblkid.so.1 libmount.so.1; do
        [[ ! -e "$STAGE/usr/lib64/$file" || -L "$STAGE/usr/lib64/$file" ]] || fail "unsafe staging SONAME path $file"
        ln -sfn "$(readlink "$ORIGIN/usr/lib64/$file")" "$STAGE/usr/lib64/$file"
    done
    check_stage "$STAGE"
    echo "PASS: reusable util-linux stage at $STAGE (rootfs untouched)"

elif [[ "$ACTION" == stage-rootfs ]]; then
    [[ -d "$ROOTFS" && ! -L "$ROOTFS" ]] || fail 'missing or symlinked rootfs'
    [[ -x "$SDK/bin/llvm-readelf" ]] || fail 'missing llvm-readelf'
    check_stage "$STAGE"
    for part in usr usr/bin usr/lib64; do
        [[ ! -L "$ROOTFS/$part" ]] || fail "refusing symlinked rootfs/$part"
    done
    # No silent replacement of existing runtime files or libraries. This is
    # important when other services already depend on libmount/libuuid.
    for file in usr/bin/mount usr/bin/umount; do
        [[ ! -L "$ROOTFS/$file" ]] || fail "refusing existing symlink $file"
        if [[ -e "$ROOTFS/$file" ]]; then
            cmp -s "$STAGE/$file" "$ROOTFS/$file" || fail "existing rootfs/$file differs; inspect it manually"
        fi
    done
    for file in "${LIBRARIES[@]}"; do
        localpath="usr/lib64/$file"
        [[ ! -L "$ROOTFS/$localpath" ]] || fail "refusing existing symlink $localpath"
        if [[ -e "$ROOTFS/$localpath" ]]; then
            cmp -s "$STAGE/$localpath" "$ROOTFS/$localpath" || fail "existing rootfs/$localpath differs; inspect it manually"
        fi
    done
    for file in libuuid.so.1 libblkid.so.1 libmount.so.1; do
        link="$ROOTFS/usr/lib64/$file"
        if [[ -e "$link" || -L "$link" ]]; then
            [[ -L "$link" && "$(readlink "$link")" == "$(readlink "$STAGE/usr/lib64/$file")" ]] || fail "existing SONAME differs: $link"
        fi
    done
    mkdir -p "$ROOTFS/usr/bin" "$ROOTFS/usr/lib64"
    for file in mount umount; do
        install -m 0755 "$STAGE/usr/bin/$file" "$ROOTFS/usr/bin/$file"
    done
    for file in "${LIBRARIES[@]}"; do
        install -m 0755 "$STAGE/usr/lib64/$file" "$ROOTFS/usr/lib64/$file"
    done
    for file in libuuid.so.1 libblkid.so.1 libmount.so.1; do
        ln -sfn "$(readlink "$STAGE/usr/lib64/$file")" "$ROOTFS/usr/lib64/$file"
    done
    echo 'PASS: rootfs now contains util-linux mount/umount and matching SONAME libraries'
fi
