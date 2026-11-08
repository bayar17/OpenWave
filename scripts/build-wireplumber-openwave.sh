#!/usr/bin/env bash
# Standalone WirePlumber 0.5.x build for OpenWave. Stages only in the target SDK sysroot.
# Usage: OPENWAVE_REPO_ROOT=/mnt/aosp/ThunderOS bash build-wireplumber-openwave.sh
set -Eeuo pipefail

ROOT="${OPENWAVE_REPO_ROOT:-$(pwd)}"
if [[ ! -f "$ROOT/env.sh" ]]; then
    printf 'ERROR: cannot find %s/env.sh; set OPENWAVE_REPO_ROOT\n' "$ROOT" >&2
    exit 2
fi
source "$ROOT/env.sh" --build

SOURCE="$ROOT/third_party/wireplumber"
BUILD="$OPENWAVE_DEPENDENCY_BUILD/wireplumber"
SYSROOT="$OPENWAVE_SYSROOT"
CROSS="$OPENWAVE_MESON_TOOLCHAIN"
JOBS="${OPENWAVE_BUILD_JOBS:-4}"

for file in "$SOURCE/meson.build" "$CROSS" "$SYSROOT/usr/include/pipewire-0.3/pipewire/keys.h"; do
    if [[ ! -f "$file" ]]; then
        printf 'ERROR: missing prerequisite: %s\n' "$file" >&2
        exit 2
    fi
done

# All Meson dependency discovery is constrained to OpenWave's target sysroot.
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=""
if ! pkg-config --exists 'libpipewire-0.3 >= 1.0.2'; then
    echo 'ERROR: PipeWire development package missing from OpenWave sysroot.' >&2
    echo 'Build and install PipeWire to the sysroot before WirePlumber.' >&2
    exit 2
fi

OPTIONS=(
    -Dintrospection=disabled -Ddoc=disabled -Dtests=false
    -Ddbus-tests=false -Dsystem-lua=false -Dsystemd=disabled
    -Delogind=disabled -Dmodules=true -Ddaemon=true -Dtools=true
)
if [[ -f "$BUILD/build.ninja" ]]; then
    meson setup --reconfigure "$BUILD" "$SOURCE" "${OPTIONS[@]}"
else
    meson setup "$BUILD" "$SOURCE" \
        --cross-file "$CROSS" --prefix=/usr --libdir=lib64 \
        "${OPTIONS[@]}"
fi
meson compile -C "$BUILD" -j "$JOBS"
DESTDIR="$SYSROOT" meson install -C "$BUILD" --no-rebuild

HEADER="$SYSROOT/usr/include/wireplumber-0.5/wp/wp.h"
if [[ ! -f "$HEADER" ]]; then
    printf 'ERROR: WirePlumber header not installed: %s\n' "$HEADER" >&2
    exit 1
fi
if ! compgen -G "$SYSROOT/usr/lib64/libwireplumber-0.5.so*" > /dev/null; then
    echo 'ERROR: WirePlumber shared library missing from target sysroot.' >&2
    exit 1
fi
printf '\nPASS: WirePlumber headers and libraries installed into %s\n' "$SYSROOT"
printf 'Next: cmake --build out/audiod-only-build --target audiod --parallel 4\n'
