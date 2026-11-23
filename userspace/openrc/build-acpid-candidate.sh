#!/usr/bin/env bash
set -Eeuo pipefail

# --stage-only builds and verifies the pinned upstream daemon and installs its
# OpenWave service/rule into OUTPUT/stage-target. It does not require a base
# root, copy a rootfs, or assemble an image; other candidate builders consume
# this stage without duplicating the acpid build recipe.
STAGE_ONLY=0
case "${1:-}" in
    --stage-only) STAGE_ONLY=1; shift ;;
    --help|-h)
        echo "Usage: $0 [--stage-only]"
        echo "--stage-only: build/test acpid and populate OPENWAVE_ACPID_OUTPUT/stage-target only"
        exit 0 ;;
esac
if [[ "$#" != 0 ]]; then
    echo "Usage: $0 [--stage-only]" >&2
    exit 2
fi

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET=x86_64-unknown-openwave
SYSROOT="$ROOT/toolchain/sysroot/$TARGET"
PIN_FILE="$ROOT/userspace/openrc/acpid-source.env"
source "$PIN_FILE"
OUTPUT="${OPENWAVE_ACPID_OUTPUT:-$ROOT/out/openrc-acpid-runtime-20261029}"
BASE_ROOT="${OPENWAVE_ACPID_BASE_ROOT:-$ROOT/out/openrc-runtime-repair-20261029/rootfs}"
IMAGE_NAME="${OPENWAVE_ACPID_IMAGE_NAME:-OpenWave-openrc-acpid-runtime.img}"
IMAGE="$OUTPUT/$IMAGE_NAME"
ARCHIVE="$OUTPUT/download/acpid-$ACPID_VERSION.tar.xz"
SOURCE_DIR="$OUTPUT/source/acpid-$ACPID_VERSION"
TARGET_BUILD="$OUTPUT/build-target"
TARGET_STAGE="$OUTPUT/stage-target"
HOST_BUILD="$OUTPUT/build-host-test"
CANDIDATE_ROOT="$OUTPUT/rootfs"

case "$OUTPUT" in "$ROOT"/out/*) ;; *) echo "Output must be under $ROOT/out" >&2; exit 2 ;; esac
test -d "$SYSROOT"
if [[ "$STAGE_ONLY" == 0 ]]; then
    case "$BASE_ROOT" in "$ROOT"/out/*) ;; *) echo "Base root must be a disposable tree under $ROOT/out" >&2; exit 2 ;; esac
    [[ "$IMAGE_NAME" == "$(basename -- "$IMAGE_NAME")" && -n "$IMAGE_NAME" ]] || exit 2
    test -d "$BASE_ROOT"
    test ! -e "$IMAGE"
    for utility in openrc-init openrc-shutdown; do test -x "$BASE_ROOT/sbin/$utility"; done
    test -x "$BASE_ROOT/usr/bin/mount"
    test -x "$BASE_ROOT/usr/bin/umount"
fi

mkdir -p "$OUTPUT/download" "$OUTPUT/source"
if [[ ! -f "$ARCHIVE" ]]; then
    curl -fL --retry 2 "$ACPID_SOURCE_URL" -o "$ARCHIVE"
fi
echo "$ACPID_SOURCE_SHA256  $ARCHIVE" | sha256sum -c -
if [[ ! -d "$SOURCE_DIR" ]]; then
    tar -xf "$ARCHIVE" -C "$OUTPUT/source"
fi
test "$(sed -n 's/^AC_INIT(\[acpid\], \[\([^]]*\)\]).*/\1/p' "$SOURCE_DIR/configure.ac")" = "$ACPID_VERSION"

rm -rf -- "$TARGET_BUILD" "$TARGET_STAGE" "$HOST_BUILD"
mkdir -p "$TARGET_BUILD" "$TARGET_STAGE" "$HOST_BUILD"
if [[ "$STAGE_ONLY" == 0 ]]; then
    rm -rf -- "$CANDIDATE_ROOT"
    mkdir -p "$CANDIDATE_ROOT"
fi

# Build and run the host-only event test using the same pristine upstream source.
(cd "$HOST_BUILD" && \
    "$SOURCE_DIR/configure" --prefix=/usr --sbindir=/usr/sbin --bindir=/usr/bin \
        --sysconfdir=/etc --localstatedir=/var)
make -C "$HOST_BUILD" -j"$(nproc)"
python3 "$ROOT/userspace/openrc/tests/test_acpid_integration.py" \
    --acpid "$HOST_BUILD/acpid"

# Cross-compile target acpid without changing its upstream source tree.
(cd "$TARGET_BUILD" && \
    CC="$ROOT/toolchain/bin/clang --target=$TARGET --sysroot=$SYSROOT" \
    CFLAGS="--target=$TARGET --sysroot=$SYSROOT -O2 -I$SYSROOT/usr/include" \
    LDFLAGS="--target=$TARGET --sysroot=$SYSROOT -L$SYSROOT/usr/lib64 -L$SYSROOT/lib64" \
    "$SOURCE_DIR/configure" --host=x86_64-linux-gnu --prefix=/usr \
        --sbindir=/usr/sbin --bindir=/usr/bin --sysconfdir=/etc \
        --localstatedir=/var)
make -C "$TARGET_BUILD" -j"$(nproc)"
install -D -m 0755 "$TARGET_BUILD/acpid" "$TARGET_STAGE/usr/sbin/acpid"
install -D -m 0755 "$ROOT/userspace/openrc/etc/init.d/acpid" \
    "$TARGET_STAGE/etc/init.d/acpid"
install -D -m 0644 "$ROOT/userspace/openrc/etc/acpi/events/power" \
    "$TARGET_STAGE/etc/acpi/events/power"
mkdir -p "$TARGET_STAGE/etc/runlevels/boot"
ln -sfn /etc/init.d/acpid "$TARGET_STAGE/etc/runlevels/boot/acpid"

"$ROOT/toolchain/bin/llvm-readelf" -h "$TARGET_STAGE/usr/sbin/acpid" > "$OUTPUT/acpid.target-elf.txt"
"$ROOT/toolchain/bin/llvm-readelf" -l "$TARGET_STAGE/usr/sbin/acpid" > "$OUTPUT/acpid.target-program-headers.txt"
"$ROOT/toolchain/bin/llvm-readelf" -d "$TARGET_STAGE/usr/sbin/acpid" > "$OUTPUT/acpid.target-dynamic.txt"
grep -Fq 'Advanced Micro Devices X86-64' "$OUTPUT/acpid.target-elf.txt"
grep -Fq '[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]' \
    "$OUTPUT/acpid.target-program-headers.txt"

if [[ "$STAGE_ONLY" == 1 ]]; then
    printf 'acpid upstream=%s sha256=%s\ntarget stage=%s\n' \
        "$ACPID_VERSION" "$ACPID_SOURCE_SHA256" "$TARGET_STAGE"
    exit 0
fi

cp -a "$BASE_ROOT/." "$CANDIDATE_ROOT/"
cp -a "$TARGET_STAGE/." "$CANDIDATE_ROOT/"

OPENWAVE_OPENRC_ROOT="$CANDIDATE_ROOT" \
    python3 "$ROOT/userspace/openrc/validate_openrc.py"
diff -qr "$BASE_ROOT/etc/selinux" "$CANDIDATE_ROOT/etc/selinux"

test "$(git -C "$ROOT/third_party/openrc-0.64.1" rev-parse HEAD)" = \
    83fb7385f4e9115a1c804b1c035bfbad04ebc46a
test -z "$(git -C "$ROOT/third_party/openrc-0.64.1" status --porcelain)"

"$ROOT/build-openwave" image "$CANDIDATE_ROOT" "$IMAGE"
sha256sum "$IMAGE" | tee "$OUTPUT/image.sha256"
e2fsck -fn "$IMAGE" > "$OUTPUT/e2fsck-readonly.log" 2>&1
printf 'acpid upstream=%s sha256=%s\ncandidate root=%s\ncandidate image=%s\n' \
    "$ACPID_VERSION" "$ACPID_SOURCE_SHA256" "$CANDIDATE_ROOT" "$IMAGE"
