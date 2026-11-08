#!/usr/bin/env bash
set -Eeuo pipefail

# Build and stage upstream OpenRC for OpenWave. This script does NOT assemble,
# populate, or modify a rootfs and never creates a filesystem/disk image.
# Install this script at userspace/openrc/build-openrc.sh.

usage() {
    cat <<'HELP'
Usage: build-openrc.sh [--clean] [--help]
  --clean   discard the dedicated OpenRC Meson build directory before building

Output:
  out/sdk-dependencies/<target>/openrc-openwave/build   (Meson build)
  out/sdk-dependencies/<target>/openrc-openwave/stage   (DESTDIR install tree)

Environment overrides:
  OPENWAVE_TARGET_TRIPLE, OPENWAVE_SDK, OPENWAVE_SYSROOT,
  OPENWAVE_OPENRC_SOURCE, OPENWAVE_OPENRC_OUTPUT, OPENWAVE_BUILD_JOBS
HELP
}

CLEAN=0
while (( $# )); do
    case "$1" in
        --clean) CLEAN=1 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
TARGET="${OPENWAVE_TARGET_TRIPLE:-x86_64-unknown-openwave}"
SDK="${OPENWAVE_SDK:-$ROOT/toolchain}"
SYSROOT="${OPENWAVE_SYSROOT:-$SDK/sysroot/$TARGET}"
SOURCE="${OPENWAVE_OPENRC_SOURCE:-$ROOT/third_party/openrc-0.64.1}"
OUTPUT="${OPENWAVE_OPENRC_OUTPUT:-$ROOT/out/sdk-dependencies/$TARGET/openrc-openwave}"
BUILD="$OUTPUT/build"
STAGE="$OUTPUT/stage"
CROSS_FILE="$OUTPUT/openwave-openrc-cross.ini"
PIN=83fb7385f4e9115a1c804b1c035bfbad04ebc46a
JOBS="${OPENWAVE_BUILD_JOBS:-4}"

fail() { echo "OpenRC build: $*" >&2; exit 1; }

# All generated files must stay inside OpenWave's output area. Refuse an
# external or symlinked output directory before removing anything there.
case "$OUTPUT" in
    "$ROOT"/out/*) ;;
    *) fail "OPENWAVE_OPENRC_OUTPUT must be under $ROOT/out" ;;
esac
[[ "$OUTPUT" != *'/../'* && "$OUTPUT" != */.. && "$OUTPUT" != *'/./'* ]] || fail "Unsafe output path: $OUTPUT"
[[ ! -L "$OUTPUT" && ! -L "$BUILD" && ! -L "$STAGE" ]] || fail "Refusing symlinked output/build/stage directory"
[[ -d "$SYSROOT" ]] || fail "Missing OpenWave sysroot: $SYSROOT"
[[ -f "$SOURCE/meson.build" ]] || fail "Missing OpenRC Meson sources: $SOURCE"
[[ -d "$SOURCE/.git" ]] || fail "Expected pinned OpenRC Git checkout: $SOURCE"
[[ "$(git -C "$SOURCE" rev-parse HEAD)" == "$PIN" ]] || fail "OpenRC source commit differs from pinned $PIN"
[[ -z "$(git -C "$SOURCE" status --porcelain)" ]] || fail "OpenRC upstream checkout is modified; keep it pristine"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail "OPENWAVE_BUILD_JOBS must be a positive integer"

for cmd in meson ninja pkg-config git; do
    command -v "$cmd" >/dev/null || fail "Missing host build tool: $cmd"
done
for binary in clang llvm-ar llvm-strip llvm-readelf; do
    [[ -x "$SDK/bin/$binary" ]] || fail "Missing SDK tool: $SDK/bin/$binary"
done

mkdir -p "$OUTPUT"
if (( CLEAN )); then
    rm -rf -- "$BUILD"
fi
# Use a fresh DESTDIR on each build; never stage over potentially stale files.
rm -rf -- "$STAGE"
mkdir -p "$STAGE"

cat > "$CROSS_FILE" <<EOF_CROSS
[binaries]
c = '$SDK/bin/clang'
ar = '$SDK/bin/llvm-ar'
strip = '$SDK/bin/llvm-strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['--target=$TARGET', '--sysroot=$SYSROOT', '-O2']
c_link_args = ['--target=$TARGET', '--sysroot=$SYSROOT', '-rtlib=compiler-rt', '-fuse-ld=lld']

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = ['$SYSROOT/usr/lib64/pkgconfig', '$SYSROOT/usr/lib/pkgconfig', '$SYSROOT/usr/share/pkgconfig']
needs_exe_wrapper = true
EOF_CROSS

# Never satisfy target dependencies from the host's pkg-config directories.
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=''

options=(
    --cross-file "$CROSS_FILE"
    --prefix=/usr
    --bindir=/usr/bin
    --sbindir=/sbin
    --libdir=/lib64
    --libexecdir=/lib
    --sysconfdir=/etc
    --localstatedir=/var
    --buildtype=release
    -Dselinux=disabled
    -Daudit=disabled
    -Dnewnet=false
    -Dpam=false
    -Dpkgconfig=false
    -Dbash-completions=false
    -Dzsh-completions=false
    -Dshell=/bin/sh
    -Duucp_group=messagebus
)

if [[ -f "$BUILD/build.ninja" ]]; then
    meson setup --reconfigure "$BUILD" "$SOURCE" "${options[@]}"
else
    meson setup "$BUILD" "$SOURCE" "${options[@]}"
fi

meson compile -C "$BUILD" -j "$JOBS"
DESTDIR="$STAGE" meson install -C "$BUILD" --no-rebuild

# Validate the standalone OpenRC staged install, never the rootfs.
for exe in \
    sbin/openrc-init sbin/openrc sbin/openrc-run sbin/rc-service \
    sbin/rc-update sbin/openrc-shutdown sbin/supervise-daemon \
    usr/bin/rc-status lib/rc/bin/checkpath; do
    [[ -x "$STAGE/$exe" ]] || fail "Missing installed OpenRC executable: $STAGE/$exe"
done

"$SDK/bin/llvm-readelf" -h "$STAGE/sbin/openrc-init" > "$OUTPUT/openrc-init.elf.txt"
grep -Fq 'Advanced Micro Devices X86-64' "$OUTPUT/openrc-init.elf.txt" || \
    fail 'openrc-init has the wrong target architecture'
"$SDK/bin/llvm-readelf" -l "$STAGE/sbin/openrc-init" > "$OUTPUT/openrc-init.program-headers.txt"
grep -Fq '/lib64/ld-linux-x86-64.so.2' "$OUTPUT/openrc-init.program-headers.txt" || \
    fail 'openrc-init has an unexpected ELF interpreter'
"$SDK/bin/llvm-readelf" -d "$STAGE/sbin/openrc-init" > "$OUTPUT/openrc-init.dynamic.txt"

printf '%s\n' \
    'OpenRC compiled and installed into a standalone staging directory.' \
    "Source: $SOURCE ($PIN)" \
    "Build:  $BUILD" \
    "Stage:  $STAGE" \
    'Rootfs assembled: NO' \
    'Image created:  NO'
