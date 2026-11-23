#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
ROOTFS="${OPENWAVE_ROOTFS:-$ROOT/rootfs}"
DATA="$ROOT/out/appdeploy-test"
case "${1:-}" in
stage)
    [[ -d "$DATA/packages/signed" && -d "$DATA/keys" ]] || { echo "run tests/packaging/make-test-packages.py first" >&2; exit 1; }
    inbox="$ROOTFS/var/lib/appdeploy/inbox/test"
    rm -rf "$inbox"; mkdir -p "$inbox/packages"
    cp "$DATA"/packages/signed/*.rpm "$inbox/packages/"
    find "$inbox" -type d -exec chmod 755 {} +; find "$inbox" -type f -exec chmod 644 {} +
    mkdir -p "$ROOTFS/opt/openwave-test" && printf 'precious\n' > "$ROOTFS/opt/openwave-test/unowned.txt"
    install -m 644 "$DATA/keys/application/app.asc" "$ROOTFS/etc/appdeploy/keys/application/test-application.asc"
    install -m 644 "$DATA/keys/system/system.asc" "$ROOTFS/etc/appdeploy/keys/system/test-system.asc"
    install -m 755 "$ROOT/userspace/openrc/qa/appdeploy.start" "$ROOTFS/etc/local.d/zz-openwave-appdeploy.start"
    "$ROOT/userspace/appdeploy/tests/make-qa-webapp.py" "$ROOTFS" "http://127.0.0.1/" "QA Site" >"$DATA/qa-webapp-id"
    echo "staged appdeploy QA data in $ROOTFS" ;;
unstage)
    [[ ! -f "$DATA/qa-webapp-id" ]] || rm -rf "$ROOTFS/Applications/$(cat "$DATA/qa-webapp-id")"
    rm -rf "$ROOTFS/opt/openwave-test" "$ROOTFS/var/lib/appdeploy/inbox/test" "$ROOTFS/etc/appdeploy/keys/application/test-application.asc" \
           "$ROOTFS/etc/appdeploy/keys/system/test-system.asc" "$ROOTFS/etc/local.d/zz-openwave-appdeploy.start"
    echo "removed appdeploy QA data from $ROOTFS" ;;
*) echo "usage: stage-qa.sh stage|unstage" >&2; exit 2 ;;
esac
