#!/usr/bin/env bash
# Separate kernel output; never changes the reference configuration or image.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/env.sh" --build
[[ "$OPENWAVE_TARGET_TRIPLE" == x86_64-unknown-openwave ]] || exit 2
BUILD="${1:-$ROOT/out/linux-waydroid}"
BASE_CONFIG="${2:-$ROOT/out/linux/.config}"
test -f "$BASE_CONFIG"
[[ "$(realpath -m "$BUILD")" != "$(realpath "$ROOT/out/linux")" ]] || {
    echo 'Refusing to replace the reference kernel output' >&2; exit 2;
}
mkdir -p "$BUILD"
export PATH="$OPENWAVE_SDK/bin:$PATH"
if [[ ! -f "$BUILD/.config" ]]; then
    cp "$BASE_CONFIG" "$BUILD/.config"
fi
"$ROOT/linux/scripts/kconfig/merge_config.sh" -m -O "$BUILD" \
    "$BUILD/.config" "$ROOT/userspace/waydroid/kernel.config"
make -C "$ROOT/linux" O="$BUILD" ARCH=x86 LLVM=1 olddefconfig
python3 - "$BUILD/.config" "$ROOT/userspace/waydroid/kernel.config" <<'PY'
from pathlib import Path
import sys
actual = set(Path(sys.argv[1]).read_text().splitlines())
missing = [line for line in Path(sys.argv[2]).read_text().splitlines()
           if line.startswith('CONFIG_') and line not in actual]
if missing:
    raise SystemExit('Missing kernel requirements: ' + ', '.join(missing))
PY
make -C "$ROOT/linux" O="$BUILD" ARCH=x86 LLVM=1 \
    -j "${OPENWAVE_BUILD_JOBS:-4}" bzImage
