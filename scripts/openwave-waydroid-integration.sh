#!/usr/bin/env bash
# OpenWave Waydroid/LXC security integration: one public CLI entrypoint.
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
[[ -f "$ROOT/scripts/openwave-waydroid-integration.py" ]] || {
    printf 'Missing integration backend in %s\n' "$ROOT" >&2
    exit 2
}
command -v python3 >/dev/null 2>&1 || {
    echo 'Missing python3 interpreter' >&2
    exit 2
}
exec python3 -I "$ROOT/scripts/openwave-waydroid-integration.py" "$@"
