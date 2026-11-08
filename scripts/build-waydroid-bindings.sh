#!/usr/bin/env bash
# Keep PyGObject, GIRepository and libffi on the same target SDK ABI.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh" --build
[[ "$OPENWAVE_TARGET_TRIPLE" == x86_64-unknown-openwave ]] || {
    echo 'Waydroid bindings currently support x86_64 only' >&2; exit 2;
}
PYTHON_SOURCE="$ROOT/third_party/cpython-3.14.9"
PYTHON_HEADERS="$OPENWAVE_DEPENDENCY_BUILD/python-waydroid-headers"
BINDINGS_SOURCE="$ROOT/third_party/pygobject-3.54.5"
BINDINGS_BUILD="$OPENWAVE_DEPENDENCY_BUILD/pygobject-waydroid"
test -f "$PYTHON_SOURCE/configure"
test -f "$BINDINGS_SOURCE/meson.build"
mkdir -p "$PYTHON_HEADERS"
export PKG_CONFIG_SYSROOT_DIR="$OPENWAVE_SYSROOT"
export PKG_CONFIG_LIBDIR="$OPENWAVE_SYSROOT/usr/lib64/pkgconfig:$OPENWAVE_SYSROOT/usr/lib/pkgconfig:$OPENWAVE_SYSROOT/usr/share/pkgconfig"
export PKG_CONFIG_PATH=
if [[ ! -f "$PYTHON_HEADERS/Makefile" ]]; then
    (
        cd "$PYTHON_HEADERS"
        CC="$OPENWAVE_SDK/bin/clang --target=$OPENWAVE_TARGET_TRIPLE --sysroot=$OPENWAVE_SYSROOT" \
        AR="$OPENWAVE_SDK/bin/llvm-ar" RANLIB="$OPENWAVE_SDK/bin/llvm-ranlib" \
        ac_cv_buggy_getaddrinfo=no ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no \
        "$PYTHON_SOURCE/configure" --host=x86_64-unknown-linux-gnu \
            --build=x86_64-linux-gnu --prefix=/usr --libdir=/usr/lib64 \
            --enable-shared --without-ensurepip --disable-test-modules \
            --with-build-python=/usr/bin/python3
    )
fi
# Extension development headers only; no host interpreter or headers enter SDK.
make -C "$PYTHON_HEADERS" inclinstall DESTDIR="$OPENWAVE_SYSROOT"
install -m644 "$PYTHON_HEADERS/Misc/python.pc" \
    "$OPENWAVE_SYSROOT/usr/lib64/pkgconfig/python-3.14.pc"
setup=()
[[ ! -f "$BINDINGS_BUILD/build.ninja" ]] || setup+=(--wipe)
meson setup "${setup[@]}" "$BINDINGS_BUILD" "$BINDINGS_SOURCE" \
    --cross-file "$OPENWAVE_MESON_TOOLCHAIN" --prefix=/usr --libdir=lib64 \
    --wrap-mode=nofallback -Dtests=false -Dpycairo=disabled \
    -Dpython.platlibdir=lib64/python3.14/site-packages \
    -Dpython.purelibdir=lib64/python3.14/site-packages
python3 - "$BINDINGS_BUILD/build.ninja" "$OPENWAVE_SYSROOT" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
if '-I/usr/include/python' in text or sys.argv[2] + '/usr/include/python3.14' not in text:
    raise SystemExit('Refusing PyGObject build without target SDK Python headers')
PY
meson compile -C "$BINDINGS_BUILD" -j "${OPENWAVE_BUILD_JOBS:-4}"
DESTDIR="$OPENWAVE_SYSROOT" meson install -C "$BINDINGS_BUILD" --no-rebuild
DESTDIR="$OPENWAVE_RUNTIME_STAGE" meson install -C "$BINDINGS_BUILD" --no-rebuild
