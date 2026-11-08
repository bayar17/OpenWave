#!/usr/bin/env bash
# OpenWave BusyBox 1.37.0: build, verify and optionally deploy without image creation.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "$ROOT/env.sh" --build
TARGET="${OPENWAVE_TARGET_TRIPLE:-x86_64-unknown-openwave}"
SDK="${OPENWAVE_SDK:-$ROOT/toolchain}"
SYSROOT="${OPENWAVE_SYSROOT:-$SDK/sysroot/$TARGET}"
SOURCE="${OPENWAVE_BUSYBOX_SOURCE:-$ROOT/third_party/busybox-1.37.0}"
CONFIG="${OPENWAVE_BUSYBOX_CONFIG:-$ROOT/userspace/openrc/busybox.config}"
OUT="${OPENWAVE_DEPENDENCY_BUILD:-$ROOT/out/sdk-dependencies/$TARGET}/busybox-openwave"
BUILD="$OUT/build"
RAW="$OUT/raw-install"
STAGE="$OUT/stage"
ROOTFS="${OPENWAVE_ROOTFS:-$ROOT/rootfs}"
VERSION=1.37.0
SHA256=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
APPLETS=(awk cat chmod cp dmesg fsck grep hostname hwclock ifconfig ip kbd_mode ln
         md5sum mkdir mktemp modprobe mv od ps readlink rm rmdir sed sha256sum
         sleep sort stat sync tail touch tr uname xxd)

usage() {
    cat <<'EOF'
Usage: scripts/build-busybox.sh build [--clean]
       scripts/build-busybox.sh check
       scripts/build-busybox.sh install-rootfs [--replace-sh]

Build stages into out/sdk-dependencies/<target>/busybox-openwave/stage.
install-rootfs deploys BusyBox and curated applets into /bin (or /usr/bin on merged-/usr rootfs).
It never calls the image assembler and will not overwrite unrelated applets.
--replace-sh explicitly replaces an existing non-BusyBox /bin/sh after backup.
EOF
}

check_environment() {
    [[ -f "$CONFIG" ]] || { echo "Missing OpenWave BusyBox config: $CONFIG" >&2; exit 1; }
    [[ -x "$SDK/bin/clang" && -x "$SDK/bin/llvm-readelf" ]] || {
        echo "Missing OpenWave Clang or llvm-readelf under $SDK/bin" >&2; exit 1;
    }
    [[ -d "$SYSROOT/usr/include" ]] || { echo "Missing target sysroot: $SYSROOT" >&2; exit 1; }
}

fetch_source() {
    if [[ -f "$SOURCE/Makefile" ]]; then return; fi
    if [[ -e "$SOURCE" ]]; then
        echo "Source path exists but has no Makefile: $SOURCE" >&2
        exit 1
    fi
    local archive="$OUT/downloads/busybox-$VERSION.tar.bz2"
    mkdir -p "$(dirname "$archive")" "$(dirname "$SOURCE")"
    if [[ ! -f "$archive" ]]; then
        echo "Downloading pinned BusyBox $VERSION source..."
        curl --fail --location --retry 3 --output "$archive" \
            "https://www.busybox.net/downloads/busybox-$VERSION.tar.bz2"
    fi
    printf '%s  %s\n' "$SHA256" "$archive" | sha256sum --check --status || {
        echo "BusyBox source SHA-256 verification failed: $archive" >&2
        exit 1
    }
    # The verified upstream archive contains the busybox-1.37.0/ top-level directory.
    tar -xjf "$archive" -C "$(dirname "$SOURCE")"
    [[ -f "$SOURCE/Makefile" ]] || {
        echo "Expected BusyBox sources at $SOURCE after extraction" >&2; exit 1;
    }
}

make_busybox() {
    make -C "$SOURCE" O="$BUILD" \
        CC="$SDK/bin/clang --target=$TARGET --sysroot=$SYSROOT -rtlib=compiler-rt -fuse-ld=lld" \
        HOSTCC="${HOSTCC:-cc}" \
        AR="$SDK/bin/llvm-ar" \
        STRIP="$SDK/bin/llvm-strip" \
        CROSS_COMPILE= "$@"
}

check_config() {
    [[ -f "$BUILD/.config" ]] || { echo 'Missing generated BusyBox configuration' >&2; exit 1; }
    for option in CONFIG_BUSYBOX=y CONFIG_SH_IS_ASH=y CONFIG_IP=y; do
        grep -Fxq "$option" "$BUILD/.config" || {
            echo "Required BusyBox setting missing: $option" >&2; exit 1;
        }
    done
    for option in 'CONFIG_MOUNT' 'CONFIG_UMOUNT' 'CONFIG_START_STOP_DAEMON'; do
        if grep -Fxq "${option}=y" "$BUILD/.config"; then
            echo "Refusing unexpected applet $option (OpenWave supplies separate utility)" >&2
            exit 1
        fi
    done
    for applet in "${APPLETS[@]}"; do
        grep -Fxq "CONFIG_${applet^^}=y" "$BUILD/.config" || {
            echo "Required OpenWave applet disabled: $applet" >&2; exit 1;
        }
    done
}

check_stage() {
    [[ -f "$STAGE/bin/busybox" && -x "$STAGE/bin/busybox" ]] || {
        echo "BusyBox not staged: $STAGE/bin/busybox" >&2; exit 1;
    }
    [[ -L "$STAGE/bin/sh" && $(readlink "$STAGE/bin/sh") == busybox ]] || {
        echo 'Required /bin/sh -> busybox link is missing in stage' >&2; exit 1;
    }
    for applet in "${APPLETS[@]}"; do
        [[ -L "$STAGE/bin/$applet" && $(readlink "$STAGE/bin/$applet") == busybox ]] || {
            echo "Missing staged applet: $applet" >&2; exit 1;
        }
    done
    "$SDK/bin/llvm-readelf" -h "$STAGE/bin/busybox" | grep -Fq 'Advanced Micro Devices X86-64' || {
        echo 'BusyBox is not an x86-64 target ELF' >&2; exit 1;
    }
    echo "Verified target BusyBox and ${#APPLETS[@]} curated applets."
    "$SDK/bin/llvm-readelf" -d "$STAGE/bin/busybox" | grep NEEDED || true
}

build() {
    check_environment
    if [[ ${1:-} == --clean ]]; then
        case "$BUILD" in "$ROOT"/out/*) rm -rf -- "$BUILD" "$RAW" "$STAGE" ;; *) exit 2 ;; esac
    elif [[ $# -ne 0 ]]; then usage >&2; exit 2; fi
    fetch_source
    mkdir -p "$BUILD"
    # Keep the authoritative, intentional OpenWave OpenRC applet selection.
    if ! cmp -s "$CONFIG" "$BUILD/.config"; then
        cp "$CONFIG" "$BUILD/.config"
    fi
    # BusyBox 1.37.0 supports oldconfig, not Linux kernel's olddefconfig.
    # Feed EOF so any newly introduced Kconfig prompts take their defaults
    # rather than hanging unattended cross-builds.
    make_busybox oldconfig </dev/null
    check_config
    make_busybox -j "${OPENWAVE_BUILD_JOBS:-4}"

    # Let the upstream installer prove all applets were generated, but do not
    # deploy its complete set into OpenWave, where it could shadow other tools.
    rm -rf -- "$RAW" "$STAGE"
    mkdir -p "$RAW" "$STAGE/bin"
    make_busybox CONFIG_PREFIX="$RAW" install
    [[ -x "$RAW/bin/busybox" ]] || { echo 'Upstream BusyBox install failed' >&2; exit 1; }
    for applet in sh "${APPLETS[@]}"; do
        if ! find "$RAW" -type l -name "$applet" -print -quit | grep -q .; then
            echo "Upstream BusyBox installer did not create applet: $applet" >&2
            exit 1
        fi
    done
    install -m 0755 "$RAW/bin/busybox" "$STAGE/bin/busybox"
    ln -s busybox "$STAGE/bin/sh"
    for applet in "${APPLETS[@]}"; do ln -s busybox "$STAGE/bin/$applet"; done
    sha256sum "$STAGE/bin/busybox" > "$OUT/busybox.sha256"
    "$SDK/bin/llvm-readelf" -h "$STAGE/bin/busybox" > "$OUT/busybox.elf.txt"
    "$SDK/bin/llvm-readelf" -d "$STAGE/bin/busybox" > "$OUT/busybox.dynamic.txt"
    check_stage
    echo "BusyBox stage complete: $STAGE"
}

install_rootfs() {
    check_stage
    local replace=0
    if [[ ${1:-} == --replace-sh ]]; then replace=1; shift; fi
    [[ $# == 0 ]] || { usage >&2; exit 2; }
    [[ -d "$ROOTFS" && ! -L "$ROOTFS" ]] || {
        echo "Missing/unsafe rootfs: $ROOTFS" >&2; exit 1;
    }
    # A merged-/usr layout normally has /bin -> usr/bin or /bin -> /usr/bin.
    # Never follow an absolute symlink on the build host: /usr/bin there is
    # the HOST directory, not the rootfs. Map known guest paths manually.
    local bin_dir="$ROOTFS/bin"
    if [[ -L "$ROOTFS/bin" ]]; then
        case "$(readlink -- "$ROOTFS/bin")" in
            usr/bin|/usr/bin)
                [[ -d "$ROOTFS/usr" && ! -L "$ROOTFS/usr" &&
                   -d "$ROOTFS/usr/bin" && ! -L "$ROOTFS/usr/bin" ]] || {
                    echo 'Unsafe or missing rootfs/usr/bin for merged-/usr rootfs' >&2
                    exit 1
                }
                # A guest-absolute link (/bin -> /usr/bin) resolves into the
                # HOST /usr/bin during host-side validation and staging.
                # Normalize it to the equivalent guest-relative merged-/usr
                # link so both host validators and the guest see the same tree.
                if [[ $(readlink -- "$ROOTFS/bin") == /usr/bin ]]; then
                    rm -- "$ROOTFS/bin"
                    ln -s usr/bin "$ROOTFS/bin"
                fi
                bin_dir="$ROOTFS/usr/bin"
                ;;
            *)
                echo "Refusing unexpected rootfs/bin symlink: $(readlink -- "$ROOTFS/bin")" >&2
                exit 1
                ;;
        esac
    elif [[ -e "$ROOTFS/bin" && ! -d "$ROOTFS/bin" ]]; then
        echo 'Refusing non-directory rootfs/bin' >&2
        exit 1
    fi
    if [[ -L "$bin_dir/busybox" ]]; then
        target="$(readlink "$bin_dir/busybox")"

        if [[ "$target" == "../usr/bin/busybox" &&
            "$bin_dir" == "$ROOTFS/bin" &&
            -f "$ROOTFS/usr/bin/busybox" &&
            ! -L "$ROOTFS/usr/bin/busybox" ]]; then
        # Valid compatibility link to canonical BusyBox.
        rm -- "$bin_dir/busybox"
        else
            echo "Refusing unexpected BusyBox symlink: $target" >&2
            exit 1
        fi
    fi
    if [[ -e "$bin_dir/sh" || -L "$bin_dir/sh" ]]; then
        if [[ ! -L "$bin_dir/sh" || $(readlink "$bin_dir/sh") != busybox ]]; then
            if [[ $replace != 1 ]]; then
                echo 'Existing /bin/sh is not BusyBox. To replace it explicitly, use:' >&2
                echo '  scripts/build-busybox.sh install-rootfs --replace-sh' >&2
                exit 1
            fi
            [[ ! -e "$bin_dir/sh.pre-busybox" && ! -L "$bin_dir/sh.pre-busybox" ]] || {
                echo 'Backup path /bin/sh.pre-busybox already exists; refusing overwrite' >&2; exit 1;
            }
        fi
    fi
    mkdir -p "$bin_dir"
    if [[ -e "$bin_dir/sh" || -L "$bin_dir/sh" ]] && \
       [[ ! -L "$bin_dir/sh" || $(readlink "$bin_dir/sh") != busybox ]]; then
        mv -- "$bin_dir/sh" "$bin_dir/sh.pre-busybox"
    fi
    install -m 0755 "$STAGE/bin/busybox" "$bin_dir/busybox"
    if [[ ! -e "$bin_dir/sh" && ! -L "$bin_dir/sh" ]]; then
        ln -s busybox "$bin_dir/sh"
    fi
    local applet current
    for applet in "${APPLETS[@]}"; do
        current="$bin_dir/$applet"
        if [[ ! -e "$current" && ! -L "$current" ]]; then
            ln -s busybox "$current"
        elif [[ -L "$current" && $(readlink "$current") == busybox ]]; then
            : # already correctly linked
        else
            echo "Keeping existing /bin/$applet (not overwritten)."
        fi
    done
    echo "Installed BusyBox in $bin_dir (guest /bin; no image created)."
    echo 'Review ELF NEEDED entries in out/sdk-dependencies/.../busybox-openwave/busybox.dynamic.txt.'
}

command="${1:-build}"
if [[ $# -gt 0 ]]; then shift; fi
case "$command" in
    build) build "$@" ;;
    check) [[ $# -eq 0 ]] || { usage >&2; exit 2; }; check_stage ;;
    install-rootfs) install_rootfs "$@" ;;
    -h|--help|help) usage ;;
    *) usage >&2; exit 2 ;;
esac
