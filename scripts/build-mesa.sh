#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build
: "${OPENWAVE_SDK:=$ROOT/toolchain}"
: "${OPENWAVE_TARGET_TRIPLE:=x86_64-unknown-openwave}"
: "${OPENWAVE_SYSROOT:=$OPENWAVE_SDK/sysroot/$OPENWAVE_TARGET_TRIPLE}"
: "${OPENWAVE_MESON_TOOLCHAIN:=$OPENWAVE_SDK/cmake/openwave-${OPENWAVE_TARGET_TRIPLE%%-*}.ini}"
: "${OPENWAVE_DEPENDENCY_BUILD:=$ROOT/out/sdk-dependencies/$OPENWAVE_TARGET_TRIPLE}"
source_dir="${OPENWAVE_MESA_SOURCE:-$ROOT/third_party/mesa}"
build_dir="${OPENWAVE_MESA_BUILD:-$OPENWAVE_DEPENDENCY_BUILD/mesa-wayland-openwave}"
sysroot="$OPENWAVE_SYSROOT"

# Reuse only a verified install with the compositor's required Wayland support.
mesa_ready() {
    [[ -f "$sysroot/usr/include/EGL/egl.h" && -f "$sysroot/usr/include/GLES2/gl2.h" ]] || return 1
    PKG_CONFIG_SYSROOT_DIR="$sysroot" \
    PKG_CONFIG_LIBDIR="$sysroot/usr/lib64/pkgconfig:$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig" \
    PKG_CONFIG_PATH= pkg-config --exists egl glesv2 || return 1
    [[ -e "$sysroot/usr/lib64/libEGL.so" || -e "$sysroot/usr/lib/libEGL.so" ]] || return 1
    [[ -e "$sysroot/usr/lib64/libGLESv2.so" || -e "$sysroot/usr/lib/libGLESv2.so" ]] || return 1
    python3 - "$sysroot" <<'CHECK'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
try:
    record = json.loads((root / 'usr/share/openwave/build/mesa.json').read_text())
    assert 'wayland' in record['platforms'] and 'virgl' in record['drivers']
    assert record['bind_wayland_display']
    for name, digest in record['libraries'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest
except (OSError, ValueError, KeyError, AssertionError):
    raise SystemExit(1)
CHECK
}
if mesa_ready && [[ "${OPENWAVE_MESA_FORCE_REBUILD:-0}" != 1 ]]; then
    echo 'Mesa EGL/GLES2 development files found in OpenWave sysroot; reusing.'
    exit 0
fi

if [[ ! -f "$source_dir/meson.build" || ! -f "$source_dir/meson.options" && ! -f "$source_dir/meson_options.txt" ]]; then
    echo "Missing Mesa source tree: $source_dir" >&2
    echo 'Populate third_party/mesa with a pinned Mesa release before building graphics.' >&2
    exit 1
fi
[[ -f "$OPENWAVE_MESON_TOOLCHAIN" ]] || { echo "Missing Meson cross-file: $OPENWAVE_MESON_TOOLCHAIN" >&2; exit 1; }

# Only target pkg-config .pc files are allowed to satisfy dependencies.
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib64/pkgconfig:$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"
unset PKG_CONFIG_PATH
for dependency in zlib expat libdrm; do
    if ! pkg-config --exists "$dependency"; then
        echo "Missing target dependency '$dependency' in $sysroot; build and install it before Mesa." >&2
        exit 1
    fi
done

# Keep VirGL/GBM and softpipe, adding Mesa Wayland EGL for Ocean clients.
# Modern Mesa enables DRM/surfaceless through GBM/EGL rather than platform names.
# Qt 6.10 uses EGL_WL_bind_wayland_display, optional in Mesa 26.
options=(
    -Dplatforms=wayland
    -Dlegacy-wayland=bind-wayland-display
    -Dgallium-drivers=virgl,softpipe
    -Dgbm=enabled
    -Dvulkan-drivers=
    -Degl=enabled
    -Dgles2=enabled
    -Dglx=disabled
    -Dllvm=disabled
    -Dbuild-tests=false
)
# Fail early if a pinned Mesa revision lacks required options.
option_file="$source_dir/meson.options"
[[ -f "$option_file" ]] || option_file="$source_dir/meson_options.txt"
for arg in "${options[@]}"; do
    key="${arg%%=*}"; key="${key#-D}"
    if ! grep -Eq "^[[:space:]]*['\"]$key['\"]|^[[:space:]]*option\(['\"]$key['\"]" "$option_file"; then
        # Mesa often formats option( and option name on separate lines; use a robust Python check.
        if ! python3 - "$option_file" "$key" <<'PY'
import pathlib, re, sys
source = pathlib.Path(sys.argv[1]).read_text()
name = sys.argv[2]
if not re.search(r"\boption\s*\(\s*['\"]" + re.escape(name) + r"['\"]", source):
    raise SystemExit(1)
PY
        then
            echo "Mesa revision does not declare Meson option: $key" >&2
            exit 1
        fi
    fi
done
mkdir -p "$OPENWAVE_DEPENDENCY_BUILD"
native_file="$OPENWAVE_DEPENDENCY_BUILD/mesa-build-machine.ini"
cat >"$native_file" <<EOF
[binaries]
pkg-config = ['env', '-u', 'PKG_CONFIG_SYSROOT_DIR', 'PKG_CONFIG_LIBDIR=$OPENWAVE_SDK/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig', 'PKG_CONFIG_PATH=', '/usr/bin/pkg-config']
wayland-scanner = '$OPENWAVE_SDK/bin/wayland-scanner'
[properties]
sys_root = '/'
pkg_config_libdir = ['$OPENWAVE_SDK/lib/pkgconfig', '/usr/lib/x86_64-linux-gnu/pkgconfig', '/usr/lib/pkgconfig', '/usr/share/pkgconfig']
EOF
export PKG_CONFIG_LIBDIR_FOR_BUILD="$OPENWAVE_SDK/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"
if [[ -f "$build_dir/build.ninja" ]]; then
    meson setup --reconfigure "$build_dir" "$source_dir" \
        --native-file "$native_file" --cross-file "$OPENWAVE_MESON_TOOLCHAIN" --prefix=/usr --libdir=lib64 \
        --buildtype=release "${options[@]}" -Dwayland-protocols:tests=false
else
    meson setup "$build_dir" "$source_dir" \
        --native-file "$native_file" --cross-file "$OPENWAVE_MESON_TOOLCHAIN" --prefix=/usr --libdir=lib64 \
        --buildtype=release "${options[@]}" -Dwayland-protocols:tests=false
fi
ninja -C "$build_dir" -j "${OPENWAVE_BUILD_JOBS:-4}"
DESTDIR="$sysroot" meson install -C "$build_dir" --no-rebuild
python3 - "$sysroot" "$build_dir" <<'RECORD'
import hashlib, json, pathlib, sys
root, build = map(pathlib.Path, sys.argv[1:])
options = {item['name']: item['value'] for item in json.loads((build / 'meson-info/intro-buildoptions.json').read_text())}
assert 'wayland' in options['platforms'] and 'bind-wayland-display' in options['legacy-wayland']
record = dict(platforms=options['platforms'], drivers=options['gallium-drivers'], bind_wayland_display=True, libraries={})
for name in ('libEGL.so.1', 'libGLESv2.so.2', 'libgbm.so.1'):
    paths = [root / kind / name for kind in ('usr/lib64', 'usr/lib')]
    library = next(path for path in paths if path.is_file())
    record['libraries'][str(library.relative_to(root))] = hashlib.sha256(library.read_bytes()).hexdigest()
output = root / 'usr/share/openwave/build/mesa.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(record, indent=2) + '\n')
RECORD
mesa_ready || { echo 'Mesa installed but EGL/GLES2 verification failed.'  >&2; exit 1; }
echo 'Mesa EGL/GLES2 installed in target sysroot.'
