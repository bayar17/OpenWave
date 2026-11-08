#!/usr/bin/env bash
# Build only PipeWire for the OpenWave x86_64 target sysroot.
# Run with: bash build-pipewire-openwave.sh
# Does not build userspace/kernel, assemble rootfs, or generate an image.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# The downloaded script may live outside the repository; allow an explicit root.
ROOT="${OPENWAVE_REPO_ROOT:-$ROOT}"
if [[ ! -f "$ROOT/env.sh" ]]; then
    echo "Cannot locate OpenWave env.sh. Set OPENWAVE_REPO_ROOT=/mnt/aosp/ThunderOS" >&2
    exit 2
fi
source "$ROOT/env.sh" --build

SOURCE="$ROOT/third_party/pipewire"
BUILD="$OPENWAVE_DEPENDENCY_BUILD/pipewire"
SYSROOT="$OPENWAVE_SYSROOT"
CROSS="$OPENWAVE_MESON_TOOLCHAIN"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"

for input in "$SOURCE/meson.build" "$SOURCE/src/pipewire/keys.h" "$CROSS"; do
    if [[ ! -f "$input" ]]; then
        echo "Missing required input: $input" >&2
        exit 2
    fi
done

# Keep the same source modifications as the existing build_audio() recipe.
apply_patch_once() {
    local patch="$1"
    if [[ ! -f "$patch" ]]; then
        echo "Missing OpenWave PipeWire patch: $patch" >&2
        return 1
    fi
    if git -C "$SOURCE" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "Already patched: $(basename "$patch")"
    elif git -C "$SOURCE" apply --check "$patch"; then
        git -C "$SOURCE" apply "$patch"
        echo "Applied: $(basename "$patch")"
    else
        echo "Patch conflicts with PipeWire source: $patch" >&2
        return 1
    fi
}
apply_patch_once "$ROOT/userspace/audio-patches/pipewire-1.4.9-clang-const.patch"
apply_patch_once "$ROOT/userspace/audio-patches/pipewire-1.4.9-openwave-app-access.patch"

if [[ -x "$ROOT/out/host-tools/bin/gdbus-codegen" ]]; then
    export PATH="$ROOT/out/host-tools/bin:$PATH"
fi
# Restrict pkg-config to TARGET dependencies; never consume host libraries.
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=""

opts=(
  -Ddocs=disabled -Dman=disabled -Dexamples=disabled -Dtests=disabled
  -Dinstalled_tests=disabled -Dgstreamer=disabled -Dsystemd=disabled
  -Dlogind=disabled -Dspa-plugins=enabled -Dalsa=enabled -Ddbus=enabled
  -Dudev=enabled -Dpipewire-alsa=enabled
  -Dpipewire-jack=disabled -Djack=disabled -Dlibpulse=disabled
  -Dlibcamera=disabled -Dv4l2=disabled -Dgsettings=disabled
  -Dreadline=disabled -Dpw-cat=disabled '-Dsession-managers=[]'
)

if [[ -f "$BUILD/build.ninja" ]]; then
    meson setup --reconfigure "$BUILD" "$SOURCE" "${opts[@]}"
else
    meson setup "$BUILD" "$SOURCE" \
        --cross-file "$CROSS" --prefix=/usr --libdir=lib64 "${opts[@]}"
fi

meson compile -C "$BUILD" -j "$JOBS"
DESTDIR="$SYSROOT" meson install -C "$BUILD" --no-rebuild

if [[ ! -f "$SYSROOT/usr/include/pipewire-0.3/pipewire/keys.h" ]]; then
    echo "ERROR: Meson install finished but keys.h was not staged in the sysroot" >&2
    exit 1
fi
if [[ ! -e "$SYSROOT/usr/lib64/libpipewire-0.3.so" ]]; then
    echo "ERROR: PipeWire library missing from the target sysroot" >&2
    exit 1
fi
printf '\nPASS: PipeWire headers and shared library installed in %s\n' "$SYSROOT"
printf 'Now compile audiod with: cmake --build out/audiod-only-build --target audiod --parallel 4\n'
