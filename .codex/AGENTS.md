# AGENTS.md — OpenWave image packing and repository notes

This repository contains **OpenWave**, a Linux-based phone operating system under active development.
The current reference platform is **x86_64 in QEMU**, with ARM64 planned as a first-class target.

Codex agents working in this repository must treat `OpenWave.img` as a deployable target root filesystem image, not as a build directory.

---

## Repository root

Assume the repository root is:

```text
/mnt/aosp/ThunderOS
```

Use a variable instead of hard-coding it repeatedly:

```bash
ROOT=/mnt/aosp/ThunderOS
```

Important paths:

```text
$ROOT/
├── OpenWave.img
├── rootfs/
├── toolchain/
├── linux/
├── out/
├── userspace/
├── third_party/
├── cross/
└── run-openwave
```

Current x86_64 kernel image:

```text
$ROOT/out/linux/arch/x86/boot/bzImage
```

Current target root filesystem staging tree:

```text
$ROOT/rootfs
```

Current disk image:

```text
$ROOT/OpenWave.img
```

Current OpenWave target triple:

```text
x86_64-unknown-openwave
```

The target dynamic linker is expected to be:

```text
/lib64/ld-linux-x86-64.so.2
```

---

# Critical rule: `rootfs` is the source tree for the runtime image

`rootfs/` is the runtime-only staging tree used to assemble OpenWave.

Compilation uses `toolchain/sysroot/<target-triple>`, exported target Qt in
`toolchain/sysroot/<target-triple>/usr`, and host tools in `toolchain/bin`.
Never compile against rootfs: it intentionally has no development headers.
The library assembler is `scripts/assemble-openwave-rootfs.py`; userspace installs
first into `out/staging/<target-triple>/userspace`. Public client libraries are
built by `build-openwave.sh sdk` into the shared SDK headers/CMake prefix and
target sysroot libraries.

When target software is installed, it should normally land under paths such as:

```text
rootfs/
├── init
├── lib64/
├── usr/
│   ├── bin/
│   ├── lib64/
│   ├── qml/
│   └── plugins/
├── etc/
├── dev/
├── proc/
├── sys/
├── run/
└── tmp/
```

Examples:

```text
rootfs/init
rootfs/usr/bin/openwave-shell
rootfs/usr/lib64/libQt6Core.so.6
rootfs/usr/lib64/libEGL.so
rootfs/usr/lib64/dri/
rootfs/usr/qml/
```

Do **not** install target files directly into the mounted image unless there is a specific reason.

Prefer:

```text
build -> install into rootfs -> sync rootfs into OpenWave.img
```

This keeps `rootfs` authoritative and makes the image reproducible.

---

# Host vs target files

Do not mix host-side tools with OpenWave target binaries.

Examples of **host-only** files:

```text
$ROOT/toolchain/bin/
$ROOT/out/qt-host/
```

These contain programs such as:

```text
moc
rcc
qsb
qmlcachegen
```

They run on the development host and must **not** be copied into `OpenWave.img` merely because they are part of the Qt build process.

Examples of **target** files belong under:

```text
$ROOT/rootfs/
```

When unsure, inspect an ELF file:

```bash
file <path>
```

and, for target executables:

```bash
$ROOT/toolchain/bin/llvm-readelf -l <path> | grep interpreter
```

For the current x86_64 target, an OpenWave dynamic executable should normally use:

```text
/lib64/ld-linux-x86-64.so.2
```

---

# OpenWave.img format

`OpenWave.img` is currently a raw **ext4 filesystem image**.

A typical initial creation is:

```bash
cd "$ROOT"

truncate -s 2G OpenWave.img
mkfs.ext4 -F -L OpenWave OpenWave.img
```

Do not reformat an existing image unless the task explicitly requires creating a fresh image.

Reformatting destroys its contents.

---

# Standard packing workflow

Use a dedicated mount point:

```bash
MOUNT=/mnt/openwave-root
```

Create it if needed:

```bash
sudo mkdir -p "$MOUNT"
```

Before mounting, verify that the image exists:

```bash
test -f "$ROOT/OpenWave.img"
```

Mount:

```bash
sudo mount -o loop "$ROOT/OpenWave.img" "$MOUNT"
```

Verify the mount before copying anything:

```bash
mountpoint "$MOUNT"
findmnt "$MOUNT"
```

Then copy the staged root filesystem into the image:

```bash
sudo cp -a "$ROOT/rootfs"/. "$MOUNT"/
```

Flush writes:

```bash
sync
```

Unmount:

```bash
sudo umount "$MOUNT"
```

The image is now ready for QEMU.

---

# Preferred safe packing sequence

For scripted work, use error handling and a cleanup trap.

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT=/mnt/aosp/ThunderOS
ROOTFS="$ROOT/rootfs"
IMAGE="$ROOT/OpenWave.img"
MOUNT=/mnt/openwave-root

test -d "$ROOTFS"
test -f "$IMAGE"

sudo mkdir -p "$MOUNT"

cleanup() {
    if mountpoint -q "$MOUNT"; then
        sync
        sudo umount "$MOUNT"
    fi
}

trap cleanup EXIT

if mountpoint -q "$MOUNT"; then
    echo "Refusing to continue: $MOUNT is already mounted."
    exit 1
fi

sudo mount -o loop "$IMAGE" "$MOUNT"

mountpoint -q "$MOUNT"

sudo cp -a "$ROOTFS"/. "$MOUNT"/

sync
sudo umount "$MOUNT"

trap - EXIT

echo "Packed OpenWave.img successfully."
```

Agents may create a repository helper script from this logic if requested.

---

# Exact mirror packing

Normal development packing uses:

```bash
sudo cp -a "$ROOTFS"/. "$MOUNT"/
```

This updates and adds files but does not remove stale files already present in the image.

If an **exact mirror** is explicitly needed, `rsync` is preferable:

```bash
sudo rsync -aHAX --numeric-ids --delete \
    "$ROOTFS"/ \
    "$MOUNT"/
```

However:

- use `--delete` only when the mounted destination has been verified;
- never run it against an unverified path;
- never assume `/mnt/openwave-root` is mounted correctly;
- do not use `--delete` by default in exploratory work.

A mistaken `--delete` can destroy unrelated host files.

---

# Updating only one file

During rapid development it is acceptable to update a single target file in the image, but the corresponding copy in `rootfs` must remain authoritative.

Example for the Ocean/OpenWave shell:

```bash
install -m755 \
    "$ROOT/out/openwave-shell/openwave-shell" \
    "$ROOT/rootfs/usr/bin/openwave-shell"
```

Then mount the image and update that file:

```bash
sudo mount -o loop "$ROOT/OpenWave.img" /mnt/openwave-root

sudo install -m755 \
    "$ROOT/rootfs/usr/bin/openwave-shell" \
    /mnt/openwave-root/usr/bin/openwave-shell

sync
sudo umount /mnt/openwave-root
```

Do not update the image while forgetting to update `rootfs`.

---

# Updating `/init`

OpenWave currently uses a custom PID 1 at:

```text
rootfs/init
```

A typical rebuild is:

```bash
ROOT=/mnt/aosp/ThunderOS
TOOLCHAIN="$ROOT/toolchain"
ROOTFS="$ROOT/rootfs"

"$TOOLCHAIN/bin/clang" \
    --target=x86_64-unknown-openwave \
    --sysroot="$ROOT/toolchain/sysroot/x86_64-unknown-openwave" \
    "$ROOT/userspace/init.c" \
    -o "$ROOTFS/init"
```

After rebuilding `/init`, repack the image before testing.

The kernel boot arguments use:

```text
init=/init
```

so an old `/init` inside `OpenWave.img` will make source changes appear to have no effect.

---

# Ocean launcher / Qt Quick deployment

The OpenWave UI shell is expected under:

```text
/usr/bin/openwave-shell
```

inside the target rootfs:

```text
rootfs/usr/bin/openwave-shell
```

Qt runtime assets may include:

```text
/usr/lib64/libQt6*.so*
/usr/lib64/qt6/plugins/
/usr/qml/
/usr/plugins/
```

Exact locations depend on the Qt installation layout.

When QML files, Qt plugins, image assets, SVGs, or QML modules change, ensure the built/installed result is copied into `rootfs` before repacking.

Do not assume editing a source QML file automatically changes the version embedded in `OpenWave.img`.

---

# Current OpenWave graphics stack

The current working reference graphics path is:

```text
Linux DRM/KMS
    ↓
virtio-gpu
    ↓
libdrm
    ↓
GBM
    ↓
Mesa / virgl
    ↓
EGL / OpenGL ES
    ↓
Qt EGLFS KMS
    ↓
Qt Quick / QML
    ↓
Ocean
```

Important target libraries are installed in `rootfs`, including:

```text
libdrm
Mesa
GBM
EGL
GLES
zlib
libpng
libjpeg-turbo
FreeType
HarfBuzz
PCRE2
Qt 6
```

Do not replace these with host libraries.

---

# QEMU testing

Use the repository helper:

```bash
cd "$ROOT"
./run-openwave run
```

Current x86_64 boot flow is approximately:

```text
bzImage
  ↓
OpenWave.img
  ↓
/init
  ↓
/usr/bin/openwave-shell
  ↓
Qt EGLFS/KMS
```

Typical kernel command line:

```text
root=/dev/vda rw rootfstype=ext4 init=/init console=tty0 console=ttyS0,115200 loglevel=7
```

Current storage device is normally VirtIO block.

Graphics is normally VirtIO GPU with OpenGL enabled on the QEMU host.

---

# QEMU display / phone reference resolution

Ocean currently uses a phone-oriented reference design resolution of:

```text
768x1366
```

The QEMU GPU can be configured accordingly, for example:

```text
virtio-vga-gl,xres=768,yres=1366
```

Qt EGLFS/KMS can also use a configuration file under the OpenWave rootfs, for example:

```text
/etc/openwave/kms.json
```

Do not assume the connector name. Check:

```text
/sys/class/drm/
```

inside the guest when necessary.

---

# Filesystem ownership and permissions

Preserve existing target metadata whenever possible.

Use:

```bash
cp -a
```

rather than plain `cp -r`.

Do not blindly run:

```bash
chown -R root:root
```

over the entire staging tree unless the repository has explicitly standardized that policy.

Future OpenWave services may require non-root ownership, capabilities, setuid bits, or other metadata.

For ordinary target executables:

```bash
chmod 0755
```

is typical.

For ordinary configuration/data files:

```bash
chmod 0644
```

is typical.

---

# Never pack these by accident

Do not copy the following wholesale into the target image:

```text
.git/
out/
third_party/
qt-host/
toolchain/
linux/
cross/
host build artifacts
macOS .DS_Store files
__MACOSX/
```

Only installed target runtime content belongs in `rootfs`.

---

# Free-space checks

Before large Qt/Mesa updates, inspect available image space:

```bash
sudo mount -o loop "$ROOT/OpenWave.img" /mnt/openwave-root
df -h /mnt/openwave-root
sudo umount /mnt/openwave-root
```

If the image becomes too small, grow it rather than silently recreating it.

Example:

```bash
truncate -s +1G "$ROOT/OpenWave.img"
e2fsck -f "$ROOT/OpenWave.img"
resize2fs "$ROOT/OpenWave.img"
```

Do not resize a mounted filesystem image.

---

# Image verification

After packing, a basic host-side consistency check is:

```bash
e2fsck -fn "$ROOT/OpenWave.img"
```

`-n` means do not modify the image.

Do not run a modifying filesystem check against a mounted image.

For a quick content check:

```bash
sudo mount -o loop "$ROOT/OpenWave.img" /mnt/openwave-root

ls -l /mnt/openwave-root/init
ls -l /mnt/openwave-root/usr/bin/openwave-shell

sudo umount /mnt/openwave-root
```

---

# Failure mode: "I changed code but QEMU still shows the old version"

Check, in this order:

1. Was the target rebuilt?
2. Was the new target installed into `rootfs`?
3. Was `OpenWave.img` mounted successfully?
4. Was `rootfs` copied into the mounted image?
5. Was the image unmounted cleanly?
6. Is QEMU booting the expected `OpenWave.img`?
7. Is `/init` launching the intended executable?
8. Are QML resources embedded at build time and therefore requiring a rebuild?

Do not assume source edits are visible to the guest without repacking.

---

# Failure mode: image will not unmount

First inspect processes using the mount:

```bash
sudo fuser -vm /mnt/openwave-root
```

Then terminate only the relevant process if safe.

Avoid lazy unmounts unless there is a clear reason:

```bash
sudo umount -l ...
```

A normal clean unmount is preferred.

---

# Architecture policy

Current working target:

```text
x86_64-unknown-openwave
```

Planned phone-class target:

```text
aarch64-unknown-openwave
```

Do not reuse a single target sysroot for both architectures.

As the ARM64 port is introduced, prefer a structure such as:

```text
toolchain/sysroot/
├── x86_64-unknown-openwave/
└── aarch64-unknown-openwave/
```

and:

```text
out/
├── x86_64/
└── aarch64/
```

Architecture-specific images should also be clearly named, for example:

```text
OpenWave-x86_64.img
OpenWave-aarch64.img
```

The current x86_64 runtime staging path is:

```text
rootfs/
```

and the current image remains:

```text
OpenWave.img
```

Do not silently rename or reorganize these paths without updating build scripts and documentation.

---

# Build-system rule

OpenWave uses several build systems:

```text
Linux kernel    -> Kbuild
Qt              -> CMake/Ninja
Mesa/libdrm     -> Meson/Ninja
glibc           -> configure/make
other libraries -> CMake or Meson
```

The final image packing step is independent of the library build system.

The invariant is:

```text
target build
    ↓
install into OpenWave target staging root
    ↓
pack staging root into OpenWave.img
    ↓
boot QEMU
```

---

# LLVM environment caveat

Never export:

```bash
LLVM=/path/to/llvm
```

globally while working with the Linux kernel.

Linux Kbuild treats `LLVM` specially.

For kernel builds use:

```bash
PATH="$ROOT/toolchain/bin:$PATH" \
make -C "$ROOT/linux" \
    O="$ROOT/out/linux" \
    ARCH=x86 \
    LLVM=1 \
    -j8
```

If a path variable is needed, use something like:

```bash
LLVM_BUILD="$ROOT/out/llvm-build"
```

instead of assigning a pathname to `LLVM`.

---

# Safety rules for Codex

Before modifying or packing `OpenWave.img`:

1. Confirm `ROOT`.
2. Confirm `rootfs` exists.
3. Confirm `OpenWave.img` exists.
4. Confirm the intended architecture.
5. Confirm the mount point is not already in use.
6. Mount and verify with `mountpoint` or `findmnt`.
7. Copy only target runtime files.
8. `sync`.
9. Cleanly unmount.
10. Never leave the image mounted after completing the task.

Never:

- format an existing image without explicit instruction;
- run `rsync --delete` against an unverified destination;
- copy host libraries into the target sysroot;
- overwrite target libraries with `/usr/lib` files from the development host;
- assume QEMU sees changes that were never packed;
- use the mounted image itself as the primary development sysroot;
- delete `rootfs` as part of a normal repack.

---

# Expected agent behavior

When asked to modify OpenWave and test it:

1. edit source;
2. rebuild the affected target;
3. install it into `rootfs`;
4. pack `rootfs` into `OpenWave.img`;
5. unmount cleanly;
6. run or provide the QEMU command if testing is requested;
7. report the exact files changed and whether the image was repacked.

When a command fails, stop at the failed stage instead of continuing to installation or packing with stale artifacts.

For example, if:

```bash
cmake --build ...
```

fails, do **not** run:

```bash
cmake --install ...
```

and do not repack `OpenWave.img`.

---

# Current project identity

```text
OpenWave
    Linux-based phone operating system

Ocean
    OpenWave launcher / system shell
```

The x86_64 QEMU environment is the current development/reference environment.

The long-term direction is to add ARM64 and real phone hardware support without making Android the foundation of OpenWave.
